// glazeassets — kommandoradsverktyget för assets.bin: pack / list /
// verify / extract. Det egna formatets motsvarighet till sqlite3-CLI:t —
// när en committad assets.bin beter sig konstigt är det HÄR man tittar.
//
//   glazeassets pack    <manifest.txt> <ut.bin>
//   glazeassets list    <fil.bin>
//   glazeassets verify  <fil.bin>          # full strukturkontroll + CRC
//   glazeassets extract <fil.bin> <katalog>
//   glazeassets icon    <fil.bin> <lista>:<nyckel> [--small <lista>:<nyckel>]
//                       <utkatalog> <bas>
//   glazeassets embed   <fil.bin> <utbas> [--ns <ns>] [--symbol <namn>]
//
// `icon` gör programfilens ikoner ur EN bild i
// assets.bin, och valfritt en egen liten vid sidan av: <bas>.ico (ramarna i
// glzassets::kIconSizes), <bas>.icns och <bas>-<N>.png (hicolor-storlekarna
// för Linux). Varje ram tar källan ur valregeln (pickIconSource) och skalas
// med resizeRgba; filerna går genom ramverkets egen PNG-codec, så samma
// indata ger samma byte på alla OS. Ingen ram skalas UPP förbi
// max(256, största källan): en uppskalad ram bär ingen ny information och
// OS:et skalar själv — så en 256-källa ger inga 512/1024-ramar. Källorna
// måste vara PNG.
//
// `embed` skriver <utbas>.h och <utbas>.cpp med filens bytes som en C-array
// — `glazelanguage embed`s form — så ramverkets egna bilder bäddas in i
// libglaze (framework_assets.bin → src/assets_builtin.cpp, committad).
//
// Manifestet är radbaserat (# = kommentar, tomrader ignoreras; tokens
// whitespace-separerade — inga mellanslag i namn/sökvägar):
//
//   list toolbar 16          # ny lista, storleksklass i LOGISKA px
//   img  ok      ok.png @2x  # post i senaste listan; @1x/@2x = density
//
// Sökvägar tolkas relativt manifestets katalog. origName = filnamnet —
// det är vad extract skriver tillbaka, byte-identiskt per konstruktion.
//
// KONSOLPROGRAM med flit (inget WIN32, inget MACOSX_BUNDLE) — apparna är
// WIN32-subsystem och deras utskrift syns inte; det här verktyget finns
// för att synas. Länkar BARA glaze_assets — aldrig glaze (skulle dra in
// en render-backend i ett CLI).

#include "glaze_assets/assets_file.h"
#include "glaze_assets/icon_file.h"
#include "glaze_assets/image.h"
#include "glaze_assets/png.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int fail(const std::string& msg)
{
    std::fprintf(stderr, "glazeassets: %s\n", msg.c_str());
    return 1;
}

// Skriver ATOMÄRT (temp + rename), som pack: de genererade filerna är
// committad källkod och en avbruten skrivning får aldrig lämna en halv fil.
bool writeAtomic(const fs::path& path, const std::vector<std::uint8_t>& bytes,
                 std::string& err)
{
    const fs::path tmp = fs::path(path.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "kan inte skriva " + tmp.string();
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  std::streamsize(bytes.size()));
        if (!out) {
            err = "skrivfel i " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        err = "kan inte ersätta " + path.string();
        return false;
    }
    return true;
}

std::vector<std::uint8_t> toBytes(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

// En ikonkälla ur filen: "<lista>:<nyckel>" → avkodad RGBA.
bool loadSource(const glzassets::File& f, const std::string& ref,
                glzassets::Rgba& out, std::string& err)
{
    const std::size_t colon = ref.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == ref.size()) {
        err = "väntade <lista>:<nyckel>, fick '" + ref + "'";
        return false;
    }
    const std::string list = ref.substr(0, colon);
    const std::string key  = ref.substr(colon + 1);
    const glzassets::EntryView* e = f.find(list, key);
    if (!e) {
        err = "'" + list + "/" + key + "' finns inte i filen";
        return false;
    }
    std::string why;
    if (!glzassets::decodePng(f.data(*e), e->dataSize, out, why)) {
        err = "'" + list + "/" + key + "': " + why;
        return false;
    }
    return true;
}

// Ramen för sidan `side` ur källorna enligt valregeln.
glzassets::Rgba iconFrame(const std::vector<glzassets::Rgba>& sources, int side)
{
    std::vector<int> sides;
    for (const glzassets::Rgba& s : sources)
        sides.push_back(std::max(s.width, s.height));
    const int i = glzassets::pickIconSource(sides, side);
    return i < 0 ? glzassets::Rgba{} : glzassets::fitSquare(sources[std::size_t(i)], side);
}

// C-arrayen som `embed` skriver — `glazelanguage embed`s form. Tolv bytes
// per rad: filen är committad källkod och ska gå att diffa.
std::string bytesTU(const std::vector<std::uint8_t>& b, const std::string& ns,
                    const std::string& symbol, const std::string& header)
{
    std::string s;
    s += "// GENERERAD av `glazeassets embed` — committad (regel 7) och\n";
    s += "// kompilerad som vanlig källkod. Bygget genererar ingenting.\n";
    s += "//\n";
    s += "// Ramverkets egna bilder (programikonens förval) får aldrig ligga i\n";
    s += "// en fil som kan saknas — en app som inte sätter någon ikon ska få\n";
    s += "// GlazeUI:s. Därför bäddas framework_assets.bin in som bytes här.\n";
    s += "#include \"" + header + "\"\n\n";
    s += "namespace " + ns + " {\n\n";
    s += "extern const unsigned char " + symbol + "[] = {";
    char buf[8];
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i % 12 == 0)
            s += "\n    ";
        // VERSALA hexsiffror med flit: `0xcb` innehåller "xcb", ett av
        // backend-grindens sökord (rot-CLAUDE.md) — en genererad fil ska
        // aldrig ge en falsk träff i en grind.
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
    s += "// GENERERAD av `glazeassets embed` — se .cpp-filen.\n\n";
    s += "namespace " + ns + " {\n\n";
    s += "extern const unsigned char " + symbol + "[];\n";
    s += "extern const unsigned long " + symbol + "Size;\n\n";
    s += "} // namespace " + ns + "\n";
    return s;
}

bool readManifest(const std::string& path,
                  std::vector<glzassets::PackList>& lists,
                  std::vector<glzassets::PackEntry>& entries,
                  std::string& err)
{
    std::ifstream in(path);
    if (!in) {
        err = "kan inte läsa " + path;
        return false;
    }
    const fs::path base = fs::path(path).parent_path();

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line.erase(hash);
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd))
            continue;                       // tomrad/kommentar

        if (cmd == "list") {
            glzassets::PackList l;
            unsigned sz = 0;
            if (!(ss >> l.name >> sz)) {
                err = "rad " + std::to_string(lineNo) +
                      ": väntade 'list <namn> <sizeClass>'";
                return false;
            }
            l.sizeClass = std::uint16_t(sz);
            lists.push_back(std::move(l));
        } else if (cmd == "img") {
            if (lists.empty()) {
                err = "rad " + std::to_string(lineNo) +
                      ": 'img' före första 'list'";
                return false;
            }
            glzassets::PackEntry e;
            std::string rel, dens;
            if (!(ss >> e.key >> rel)) {
                err = "rad " + std::to_string(lineNo) +
                      ": väntade 'img <key> <sökväg> [@1x|@2x]'";
                return false;
            }
            if (ss >> dens) {
                if (dens == "@1x")      e.density = glzassets::kDensity1x;
                else if (dens == "@2x") e.density = glzassets::kDensity2x;
                else {
                    err = "rad " + std::to_string(lineNo) +
                          ": okänd density '" + dens + "' (@1x eller @2x)";
                    return false;
                }
            }
            const fs::path src = base / rel;
            std::ifstream f(src, std::ios::binary);
            if (!f) {
                err = "rad " + std::to_string(lineNo) + ": kan inte läsa " +
                      src.string();
                return false;
            }
            e.bytes.assign(std::istreambuf_iterator<char>(f), {});
            e.list     = lists.back().name;
            e.origName = src.filename().string();
            e.typeTag  = glzassets::kTypeImage;   // radtypen ÄR typen
            entries.push_back(std::move(e));
        } else {
            err = "rad " + std::to_string(lineNo) + ": okänt kommando '" +
                  cmd + "'";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    std::string err;

    if (cmd == "pack" && argc == 4) {
        std::vector<glzassets::PackList>  lists;
        std::vector<glzassets::PackEntry> entries;
        if (!readManifest(argv[2], lists, entries, err))
            return fail(err);
        if (!glzassets::pack(lists, entries, argv[3], err))
            return fail(err);
        std::printf("skrev %s (%zu listor, %zu poster)\n",
                    argv[3], lists.size(), entries.size());
        return 0;
    }
    if (cmd == "list" && argc == 3) {
        glzassets::File f;
        if (!f.openFile(argv[2], err))
            return fail(err);
        std::fputs(glzassets::dump(f).c_str(), stdout);
        return 0;
    }
    if (cmd == "verify" && argc == 3) {
        glzassets::File f;
        if (!f.openFile(argv[2], err))
            return fail(err);        // orsaken ÄR verify-diagnosen
        std::printf("%s OK (%zu listor, %zu poster)\n",
                    argv[2], f.lists().size(), f.entries().size());
        return 0;
    }
    if (cmd == "extract" && argc == 4) {
        glzassets::File f;
        if (!f.openFile(argv[2], err))
            return fail(err);
        std::error_code ec;
        fs::create_directories(argv[3], ec);
        if (ec)
            return fail("kan inte skapa katalogen " + std::string(argv[3]));
        for (const glzassets::EntryView& e : f.entries())
            if (!glzassets::extract(f, e, argv[3], err))
                return fail(err);
        std::printf("exporterade %zu poster till %s\n",
                    f.entries().size(), argv[3]);
        return 0;
    }

    // icon och embed tar flaggor — de plockas ur argv före de positionella,
    // så ordningen inte blir ett osynligt kontrakt (glazelanguage-formen).
    std::string              small, ns, symbol;
    std::vector<std::string> pos;
    for (int i = 2; i < argc && (cmd == "icon" || cmd == "embed"); ++i) {
        const std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc)
                return false;
            out = argv[++i];
            return true;
        };
        if (a == "--small" && cmd == "icon") {
            if (!value(small)) return fail("--small saknar värde");
        } else if (a == "--ns" && cmd == "embed") {
            if (!value(ns)) return fail("--ns saknar värde");
        } else if (a == "--symbol" && cmd == "embed") {
            if (!value(symbol)) return fail("--symbol saknar värde");
        } else if (a.rfind("--", 0) == 0) {
            return fail("okänd flagga '" + a + "'");
        } else {
            pos.push_back(a);
        }
    }

    if (cmd == "icon" && pos.size() == 4) {
        glzassets::File f;
        if (!f.openFile(pos[0], err))
            return fail(err);
        std::vector<glzassets::Rgba> sources(1);
        if (!loadSource(f, pos[1], sources[0], err))
            return fail(err);
        if (!small.empty()) {
            sources.emplace_back();
            if (!loadSource(f, small, sources.back(), err))
                return fail(err);
        }
        // Taket för uppskalning: max(256, största källan) — se filhuvudet.
        int cap = 256;
        for (const glzassets::Rgba& s : sources)
            cap = std::max(cap, std::max(s.width, s.height));

        const fs::path dir = pos[2];
        const std::string base = pos[3];
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            return fail("kan inte skapa katalogen " + dir.string());

        std::vector<glzassets::Rgba> ico, icns;
        for (int s : glzassets::kIconSizes)
            ico.push_back(iconFrame(sources, s));
        for (int s : glzassets::kIcnsSizes)
            if (s <= cap)
                icns.push_back(iconFrame(sources, s));

        std::vector<std::uint8_t> bytes;
        if (!glzassets::writeIco(ico, bytes, err) ||
            !writeAtomic(dir / (base + ".ico"), bytes, err))
            return fail(err);
        if (!glzassets::writeIcns(icns, bytes, err) ||
            !writeAtomic(dir / (base + ".icns"), bytes, err))
            return fail(err);
        int pngs = 0;
        for (int s : glzassets::kHicolorSizes) {
            if (s > cap)
                continue;
            if (!writeAtomic(dir / (base + "-" + std::to_string(s) + ".png"),
                             glzassets::encodePng(iconFrame(sources, s)), err))
                return fail(err);
            ++pngs;
        }
        std::printf("skrev %s.ico (%zu ramar), %s.icns (%zu ramar) och %d "
                    "PNG:er i %s\n",
                    base.c_str(), ico.size(), base.c_str(), icns.size(), pngs,
                    dir.string().c_str());
        return 0;
    }
    if (cmd == "embed" && pos.size() == 2) {
        std::ifstream in(pos[0], std::ios::binary);
        if (!in)
            return fail("kan inte läsa " + pos[0]);
        std::vector<std::uint8_t> bin((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
        // Bara en fil som validerar bäddas in — samma kontroll som verify.
        glzassets::File f;
        if (!f.openBytes(bin, err))
            return fail(pos[0] + ": " + err);
        const std::string n   = ns.empty() ? std::string("glaze") : ns;
        const std::string sym = symbol.empty() ? std::string("kAssetsBin") : symbol;
        const fs::path h = pos[1] + ".h";
        const fs::path c = pos[1] + ".cpp";
        if (!writeAtomic(h, toBytes(bytesHeader(n, sym)), err))
            return fail(err);
        if (!writeAtomic(c, toBytes(bytesTU(bin, n, sym, h.filename().string())),
                         err))
            return fail(err);
        std::printf("skrev %s och %s (%zu byte)\n", h.string().c_str(),
                    c.string().c_str(), bin.size());
        return 0;
    }

    std::fputs("glazeassets — verktyg för assets.bin\n"
               "  glazeassets pack    <manifest.txt> <ut.bin>\n"
               "  glazeassets list    <fil.bin>\n"
               "  glazeassets verify  <fil.bin>\n"
               "  glazeassets extract <fil.bin> <katalog>\n"
               "  glazeassets icon    <fil.bin> <lista>:<nyckel> "
               "[--small <lista>:<nyckel>] <utkatalog> <bas>\n"
               "  glazeassets embed   <fil.bin> <utbas> [--ns <ns>] "
               "[--symbol <namn>]\n", stderr);
    return 1;
}
