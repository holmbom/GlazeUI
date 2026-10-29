// lang_gate — SPRÅKPAKETETS grind.
//
// DEL 1 (den här filen fram till `del1`) mäter FORMATET och länkar bara
// `glaze_language` — ren std, ingen glaze, inget fönster.
//
// FACIT ÄR HANDSKRIVET: en minimal tvåspråksfil monteras BYTE FÖR BYTE i
// testet, med testets EGEN CRC32-implementation, och `packBytes` måste
// producera exakt de bytesen. Ingen förväntan läses ur packaren — det är
// vad som gör grinden icke-cirkulär. Saboterar man en offsetberäkning i
// language_file.cpp faller `facit`-jämförelsen med byteposition.
//
// SABOTAGEDELEN är beviset för att CRC-TÄCKNINGEN är rätt vald: fyra
// olika bytes muteras var för sig (defaultLang, ett tabelloffset, ett id
// och ett langIndex) och ALLA FYRA ska ge CRC-avslag. Med assets
// blob-CRC hade tre av fyra passerat.
//
// Exit 0 = allt grönt; 1 = minst ett fall föll.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include <gfx/platform.h>
#include <glaze/glaze.h>

#include "glaze_language/language_file.h"
#include "lang_builtin.h"

namespace {

int fel = 0;

void ok(bool cond, const std::string& vad)
{
    std::printf("%-6s %s\n", cond ? "OK" : "FEL", vad.c_str());
    if (!cond)
        ++fel;
}

// ---------------------------------------------------- testets EGET orakel
//
// Andra implementationen av CRC32 och av byte-skrivningen. Delas INTE med
// language_file.cpp — en delad hjälpare hade gjort grinden cirkulär.

std::uint32_t testCrc(const std::vector<std::uint8_t>& v, std::size_t hal)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const std::uint8_t b = (i >= hal && i < hal + 4) ? 0 : v[i];
        crc ^= b;
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

void u16(std::vector<std::uint8_t>& v, unsigned x)
{
    v.push_back(std::uint8_t(x & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
}

void u32(std::vector<std::uint8_t>& v, unsigned long x)
{
    v.push_back(std::uint8_t(x & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
    v.push_back(std::uint8_t((x >> 16) & 0xFF));
    v.push_back(std::uint8_t((x >> 24) & 0xFF));
}

void txt(std::vector<std::uint8_t>& v, const char* s)
{
    for (const char* p = s; *p; ++p)
        v.push_back(std::uint8_t(*p));
}

// FACIT: språken sv "Sv" och en "En", posterna sv/1 = "Ja" och en/1 =
// "Yes". Varje offset är RÄKNAD FÖR HAND här och inte hämtad någonstans:
//
//   pool:  "sv"@0  "Sv"@3  "en"@6  "En"@9  "Ja"@12  "Yes"@15   → 19 byte
//   langOffset   = 64
//   entryOffset  = 64 + 2*24 = 112
//   stringOffset = 112 + 2*16 = 144
//   fileSize     = 144 + 19 = 163
std::vector<std::uint8_t> facit(unsigned defaultLang, unsigned flags)
{
    std::vector<std::uint8_t> v;
    txt(v, "GLZLANGS");
    u16(v, 1);            //  8 versionMajor
    u16(v, 0);            // 10 versionMinor
    u32(v, 64);           // 12 headerSize
    u32(v, 2);            // 16 langCount
    u32(v, 64);           // 20 langOffset
    u32(v, 24);           // 24 langRecSize
    u32(v, 2);            // 28 entryCount
    u32(v, 112);          // 32 entryOffset
    u32(v, 16);           // 36 entryRecSize
    u32(v, 144);          // 40 stringOffset
    u32(v, 19);           // 44 stringBytes
    u32(v, 0);            // 48 fileCrc32 — fylls i sist
    u32(v, 1000);         // 52 appIdFloor
    u16(v, defaultLang);  // 56
    u16(v, flags);        // 58
    u32(v, 163);          // 60 fileSize

    // Språkposterna (24 byte styck)
    u32(v, 0);  u32(v, 2);  u32(v, 3);  u32(v, 2);  u32(v, 0); u32(v, 1);
    u32(v, 6);  u32(v, 2);  u32(v, 9);  u32(v, 2);  u32(v, 1); u32(v, 1);

    // Entryposterna (16 byte styck)
    u32(v, 1);  u32(v, 0);  u32(v, 12); u32(v, 2);
    u32(v, 1);  u32(v, 1);  u32(v, 15); u32(v, 3);

    // Strängtabellen
    txt(v, "sv"); v.push_back(0);
    txt(v, "Sv"); v.push_back(0);
    txt(v, "en"); v.push_back(0);
    txt(v, "En"); v.push_back(0);
    txt(v, "Ja"); v.push_back(0);
    txt(v, "Yes"); v.push_back(0);

    const std::uint32_t crc = testCrc(v, 48);
    v[48] = std::uint8_t(crc & 0xFF);
    v[49] = std::uint8_t((crc >> 8) & 0xFF);
    v[50] = std::uint8_t((crc >> 16) & 0xFF);
    v[51] = std::uint8_t((crc >> 24) & 0xFF);
    return v;
}

// Muterar EN byte och räknar om CRC:n — för de STRUKTURELLA fallen, där
// filen ska avvisas på sitt innehåll och inte på checksumman.
std::vector<std::uint8_t> medOmraknadCrc(std::vector<std::uint8_t> v)
{
    const std::uint32_t crc = testCrc(v, 48);
    v[48] = std::uint8_t(crc & 0xFF);
    v[49] = std::uint8_t((crc >> 8) & 0xFF);
    v[50] = std::uint8_t((crc >> 16) & 0xFF);
    v[51] = std::uint8_t((crc >> 24) & 0xFF);
    return v;
}

void skrivU32(std::vector<std::uint8_t>& v, std::size_t off, unsigned long x)
{
    v[off + 0] = std::uint8_t(x & 0xFF);
    v[off + 1] = std::uint8_t((x >> 8) & 0xFF);
    v[off + 2] = std::uint8_t((x >> 16) & 0xFF);
    v[off + 3] = std::uint8_t((x >> 24) & 0xFF);
}

bool avvisas(std::vector<std::uint8_t> v, const char* vad,
             const char* vantadDel = nullptr)
{
    glzlang::File f;
    std::string   err;
    const bool    öppnad = f.openBytes(std::move(v), err);
    bool          bra    = !öppnad;
    if (bra && vantadDel)
        bra = err.find(vantadDel) != std::string::npos;
    ok(bra, std::string(vad) + (öppnad ? " (ÖPPNADES!)" : " → " + err));
    return bra;
}

// De två språken som pack-indata — samma innehåll som `facit`.
std::vector<glzlang::PackLang> tvaSprak()
{
    return {{"sv", "Sv"}, {"en", "En"}};
}

std::vector<glzlang::PackEntry> tvaPoster()
{
    return {{"sv", 1, "Ja"}, {"en", 1, "Yes"}};
}

// ============================================================== DEL 1

void del1()
{
    std::printf("\n--- del 1: formatet (glaze_language, ren std) ---\n");

    // 1. PACKAREN MOT HANDSKRIVET FACIT — byte för byte.
    std::vector<std::uint8_t> bin;
    std::string               err;
    ok(glzlang::packBytes(tvaSprak(), tvaPoster(), "sv", false, bin, err),
       "packBytes: minimal tvåspråksfil (" + err + ")");
    const std::vector<std::uint8_t> f = facit(0, 0);
    if (bin.size() != f.size()) {
        ok(false, "facit: storleken är " + std::to_string(bin.size()) +
                      ", handräknad " + std::to_string(f.size()));
    } else {
        std::size_t forstaDiff = f.size();
        for (std::size_t i = 0; i < f.size(); ++i)
            if (bin[i] != f[i]) { forstaDiff = i; break; }
        ok(forstaDiff == f.size(),
           forstaDiff == f.size()
               ? "facit: 163 byte BYTE-IDENTISKA med den handräknade filen"
               : "facit: första avvikelsen vid byte " +
                     std::to_string(forstaDiff));
    }

    // 2. pack → open → innehållet.
    {
        glzlang::File file;
        ok(file.openBytes(bin, err), "open: den packade filen (" + err + ")");
        ok(file.langs().size() == 2 && file.langs()[0].tag == "sv" &&
               file.langs()[1].name == "En",
           "open: språktabellen");
        const glzlang::EntryView* e = file.find(0, 1);
        ok(e && e->text == "Ja", "find: sv/1 == \"Ja\"");
        e = file.find(1, 1);
        ok(e && e->text == "Yes", "find: en/1 == \"Yes\"");
        ok(file.findLang("SV_se") == -1 && file.findLang("SV") == 0,
           "findLang: normaliserar gemener, avvisar okänd tagg");
        ok(!file.isFramework() && file.defaultLang() == 0,
           "open: flaggor och defaultLang");
    }

    // 3. TRUNKERING.
    {
        std::vector<std::uint8_t> t(bin.begin(), bin.begin() + 40);
        avvisas(t, "trunkerad fil (40 byte)");
        std::vector<std::uint8_t> t2(bin.begin(), bin.end() - 1);
        avvisas(t2, "trunkerad fil (en byte kort)", "fileSize");
    }

    // 4. FEL MAGIC.
    {
        std::vector<std::uint8_t> v = bin;
        v[0] = 'X';
        avvisas(medOmraknadCrc(v), "fel magic", "magic");
    }

    // 5. FEL CRC — en byte i STRÄNGTABELLEN, utan omräkning.
    {
        std::vector<std::uint8_t> v = bin;
        v[145] = std::uint8_t(v[145] ^ 0x01);
        avvisas(v, "muterad strängbyte utan omräknad CRC", "CRC");
    }

    // 6. SABOTAGE — FYRA bytes, var för sig, ALLA ska ge CRC-avslag.
    //    Det är beviset för att CRC:n täcker HELA filen: med assets
    //    blob-CRC hade defaultLang, tabelloffsetet och langIndex passerat.
    {
        std::vector<std::uint8_t> v = bin;
        v[56] = 1;                                   // defaultLang 0 → 1
        avvisas(v, "SABOTAGE defaultLang ⇒ CRC-avslag", "CRC");
        v = bin;
        skrivU32(v, 32, 113);                        // entryOffset
        avvisas(v, "SABOTAGE tabelloffset ⇒ CRC-avslag", "CRC");
        v = bin;
        skrivU32(v, 112, 7);                         // första postens id
        avvisas(v, "SABOTAGE id ⇒ CRC-avslag", "CRC");
        v = bin;
        skrivU32(v, 112 + 4, 1);                     // första postens langIndex
        avvisas(v, "SABOTAGE langIndex ⇒ CRC-avslag", "CRC");
    }

    // 7. STRUKTURFELEN — CRC:n räknas OM, så filen avvisas på innehållet.
    {
        std::vector<std::uint8_t> v = bin;
        skrivU32(v, 112 + 4, 1);                     // langIndex fel svit
        avvisas(medOmraknadCrc(v), "langIndex utanför språkets svit",
                "langIndex");

        v = bin;
        skrivU32(v, 112, 0);                         // id == 0
        avvisas(medOmraknadCrc(v), "id 0 är reserverat", "id 0");

        v = bin;
        skrivU32(v, 52, 999);                        // appIdFloor
        avvisas(medOmraknadCrc(v), "appIdFloor != 1000", "appIdFloor");

        v = bin;
        v[58] = 0x04;                                // okänd flaggbit
        avvisas(medOmraknadCrc(v), "okänd flaggbit", "flagg");

        v = bin;
        v[56] = 5;                                   // defaultLang >= langCount
        avvisas(medOmraknadCrc(v), "defaultLang utanför langCount",
                "defaultLang");

        v = bin;
        skrivU32(v, 64 + 20, 2);                     // sv entryCount 1 → 2
        avvisas(medOmraknadCrc(v), "felaktig entryCount-summa");

        v = bin;
        skrivU32(v, 64 + 16, 1);                     // sv firstEntry 0 → 1
        avvisas(medOmraknadCrc(v), "överlappande språksviter", "överlapp");

        v = bin;
        skrivU32(v, 40, 100);                        // stringOffset in i entrytabellen
        avvisas(medOmraknadCrc(v), "överlappande sektioner", "överlapp");

        v = bin;
        skrivU32(v, 20, 60);                         // langOffset in i headern
        avvisas(medOmraknadCrc(v), "språktabellen överlappar headern",
                "headern");
    }

    // 8. OSORTERADE ID — kräver tre poster i samma språk.
    {
        std::vector<glzlang::PackEntry> p = {
            {"sv", 1, "a"}, {"sv", 2, "b"}, {"sv", 3, "c"}, {"en", 1, "A"}};
        std::vector<std::uint8_t> v;
        ok(glzlang::packBytes(tvaSprak(), p, "sv", false, v, err),
           "packBytes: tre poster i sv");
        // Entrytabellen: 64+2*24 = 112, sv har posterna 0..2.
        // Byt id på första posten till 9 ⇒ 9, 2, 3 är inte stigande.
        skrivU32(v, 112 + 0 * 16, 9);
        avvisas(medOmraknadCrc(v), "osorterade id inom ett språk", "stigande");
    }

    // 9. PACKARENS EGNA AVVISNINGAR.
    {
        std::vector<std::uint8_t> v;
        ok(!glzlang::packBytes(tvaSprak(), {{"sv", 1, "a"}, {"sv", 1, "b"}},
                               "sv", false, v, err),
           "pack: dubblerat id i samma språk avvisas");
        ok(!glzlang::packBytes({{"sv", "Sv"}, {"SV", "Igen"}}, {}, "sv",
                               false, v, err),
           "pack: dubblerad tagg (normaliserad) avvisas");
        ok(!glzlang::packBytes(tvaSprak(), {{"sv", 0, "a"}}, "sv", false, v,
                               err),
           "pack: id 0 avvisas");
        ok(!glzlang::packBytes(tvaSprak(), {{"de", 1, "a"}}, "sv", false, v,
                               err),
           "pack: post på okänt språk avvisas");
        ok(!glzlang::packBytes(tvaSprak(), {}, "de", false, v, err),
           "pack: okänt defaultspråk avvisas");
        ok(!glzlang::packBytes({{"s", "Sv"}}, {}, "", false, v, err),
           "pack: ogiltig BCP-47-form avvisas");
        ok(!glzlang::packBytes({{"sv", ""}}, {}, "", false, v, err),
           "pack: språk utan självbenämning avvisas");
    }

    // 10. TOM STRÄNG ÄR EN GILTIG TEXT — och betyder INTE "saknas".
    {
        std::vector<glzlang::PackEntry> p = {{"sv", 1, ""}, {"en", 1, "Yes"}};
        std::vector<std::uint8_t>       v;
        ok(glzlang::packBytes(tvaSprak(), p, "sv", false, v, err),
           "pack: tom text (" + err + ")");
        glzlang::File file;
        ok(file.openBytes(v, err), "open: fil med tom text (" + err + ")");
        const glzlang::EntryView* e = file.find(0, 1);
        ok(e != nullptr && e->text.empty(),
           "tom sträng: posten FINNS och texten är tom");
        ok(file.find(0, 2) == nullptr,
           "saknat id ger nullptr — skilt från tom sträng");
    }

    // 11. SAKNAT ID I ETT SPRÅK + täckningen.
    {
        std::vector<glzlang::PackEntry> p = {
            {"sv", 1, "Ja"}, {"sv", 2, "Nej"}, {"en", 1, "Yes"}};
        std::vector<std::uint8_t> v;
        glzlang::packBytes(tvaSprak(), p, "sv", false, v, err);
        glzlang::File file;
        ok(file.openBytes(v, err), "open: ojämn täckning (" + err + ")");
        ok(file.allIds().size() == 2, "allIds: unionen är {1, 2}");
        ok(file.find(1, 2) == nullptr, "en saknar id 2");
        ok(glzlang::dump(file).find("saknar: 2") != std::string::npos,
           "dump: täckningsmatrisen namnger det saknade id:t");
    }

    // 12. SCOPE-KONTROLLEN.
    {
        std::vector<std::uint8_t> v;
        glzlang::packBytes(tvaSprak(), tvaPoster(), "sv", false, v, err);
        glzlang::File appfil;
        appfil.openBytes(v, err);
        ok(!glzlang::requireAppScope(appfil, err),
           "appfil med id < 1000 avvisas av requireAppScope (" + err + ")");
        ok(!glzlang::requireFrameworkScope(appfil, err),
           "fil utan ramverksbit avvisas av requireFrameworkScope");

        glzlang::packBytes(tvaSprak(), tvaPoster(), "sv", true, v, err);
        glzlang::File ramfil;
        ramfil.openBytes(v, err);
        ok(ramfil.isFramework() && glzlang::requireFrameworkScope(ramfil, err),
           "ramverksfil med id < 1000 godtas");
        ok(!glzlang::requireAppScope(ramfil, err),
           "ramverksfil avvisas av requireAppScope (flaggan)");

        glzlang::packBytes(tvaSprak(), {{"sv", 1000, "a"}, {"en", 1000, "b"}},
                           "sv", false, v, err);
        glzlang::File riktigApp;
        riktigApp.openBytes(v, err);
        ok(glzlang::requireAppScope(riktigApp, err),
           "appfil med id >= 1000 godtas");
        ok(!glzlang::requireFrameworkScope(riktigApp, err),
           "ramverksfil med id >= 1000 avvisas");
    }

    // 13. MINOR-HOPPET — nyare minor med STÖRRE recordstorlekar läses på
    //     DEKLARERAD längd. Filen byggs för hand här (packaren skriver
    //     alltid vår minor), med 28-byte språkposter och 20-byte
    //     entryposter, och läsaren ska hoppa de extra bytesen.
    {
        std::vector<std::uint8_t> v;
        const unsigned langRec = 28, entryRec = 20;
        const unsigned langOff = 64;
        const unsigned entOff  = langOff + 2 * langRec;      // 120
        const unsigned strOff  = entOff + 2 * entryRec;      // 160
        const unsigned strLen  = 19;
        txt(v, "GLZLANGS");
        u16(v, 1); u16(v, 7);            // versionMinor 7 — NYARE än vår
        u32(v, 64); u32(v, 2); u32(v, langOff); u32(v, langRec);
        u32(v, 2); u32(v, entOff); u32(v, entryRec);
        u32(v, strOff); u32(v, strLen);
        u32(v, 0); u32(v, 1000); u16(v, 0); u16(v, 0);
        u32(v, strOff + strLen);
        for (int i = 0; i < 2; ++i) {
            u32(v, i == 0 ? 0 : 6); u32(v, 2);
            u32(v, i == 0 ? 3 : 9); u32(v, 2);
            u32(v, unsigned(i)); u32(v, 1);
            u32(v, 0xDEADBEEF);          // okänt fält i den nyare minorn
        }
        for (int i = 0; i < 2; ++i) {
            u32(v, 1); u32(v, unsigned(i));
            u32(v, i == 0 ? 12 : 15); u32(v, i == 0 ? 2 : 3);
            u32(v, 0xCAFEBABE);          // okänt fält
        }
        txt(v, "sv"); v.push_back(0);
        txt(v, "Sv"); v.push_back(0);
        txt(v, "en"); v.push_back(0);
        txt(v, "En"); v.push_back(0);
        txt(v, "Ja"); v.push_back(0);
        txt(v, "Yes"); v.push_back(0);
        v = medOmraknadCrc(std::move(v));

        glzlang::File file;
        ok(file.openBytes(v, err),
           "minor-hopp: nyare minor med STÖRRE poster läses (" + err + ")");
        const glzlang::EntryView* e = file.find(1, 1);
        ok(e && e->text == "Yes", "minor-hopp: en/1 hittas på deklarerad längd");
    }

    // 14. MAJOR-BROTT ⇒ VÄGRA.
    {
        std::vector<std::uint8_t> v = bin;
        v[8] = 2;
        avvisas(medOmraknadCrc(v), "versionMajor 2 vägras", "versionMajor");
    }

    // 15. MANIFESTET — parse, pack, export, round-trip.
    {
        const std::string man =
            "# kommentar\n"
            "default en\n"
            "lang sv Svenska\n"
            "id 1 Yes \"Ja\"\n"
            "id 2 No \"Nej # inte kommentar\"\n"
            "lang en English\n"
            "id 1 Yes \"Yes\"\n"
            "id 2 \"No\"\n";
        glzlang::Manifest m;
        ok(glzlang::parseManifest(man, m, err),
           "manifest: parsas (" + err + ")");
        ok(m.langs.size() == 2 && m.entries.size() == 4 &&
               m.defaultLangTag == "en",
           "manifest: två språk, fyra poster, default en");
        ok(m.langs[0].name == "Svenska" && m.langs[1].name == "English",
           "manifest: självbenämningen är resten av raden");
        ok(m.entries[1].text == "Nej # inte kommentar",
           "manifest: '#' inuti en literal är TEXT, inte kommentar");
        ok(m.names.size() == 2 && m.names[0].name == "Yes" &&
               m.names[1].name == "No",
           "manifest: konstantnamnen plockas upp");

        std::vector<std::uint8_t> v;
        ok(glzlang::packBytes(m.langs, m.entries, m.defaultLangTag, false, v,
                              err),
           "manifest → packBytes (" + err + ")");
        glzlang::File file;
        ok(file.openBytes(v, err), "manifest: filen öppnas (" + err + ")");
        ok(file.defaultLang() == 1, "manifest: default en blev index 1");

        glzlang::Manifest m2;
        ok(glzlang::parseManifest(glzlang::exportManifest(file), m2, err),
           "exportManifest → parseManifest (" + err + ")");
        bool samma = m2.langs.size() == m.langs.size() &&
                     m2.entries.size() == m.entries.size() &&
                     m2.defaultLangTag == "en";
        for (std::size_t i = 0; samma && i < m2.entries.size(); ++i)
            samma = m2.entries[i].id == m.entries[i].id &&
                    m2.entries[i].text == m.entries[i].text &&
                    m2.entries[i].lang == m.entries[i].lang;
        ok(samma, "manifest: round-trip bevarar språk, id och texter");

        const std::string h = glzlang::headerText(m, "glaze::str", "en");
        ok(h.find("inline constexpr std::uint32_t Yes = 1;") !=
                   std::string::npos &&
               h.find("// \"Yes\"") != std::string::npos,
           "headerText: konstant + referensspråkets text som kommentar");

        // AVVISNINGARNA — escape-delmängden och formen.
        glzlang::Manifest tmp;
        ok(!glzlang::parseManifest("lang sv Svenska\nid 1 \"a\\x41\"\n", tmp,
                                   err),
           "manifest: okänd escape avvisas");
        ok(!glzlang::parseManifest("id 1 \"a\"\n", tmp, err),
           "manifest: 'id' före första 'lang' avvisas");
        ok(!glzlang::parseManifest("lang sv Svenska\nid 0 \"a\"\n", tmp, err),
           "manifest: id 0 avvisas");
        ok(!glzlang::parseManifest("lang sv Svenska\nid 1 a\n", tmp, err),
           "manifest: text utan citattecken avvisas");
        ok(!glzlang::parseManifest("lang sv Svenska\nid 1 1x \"a\"\n", tmp,
                                   err),
           "manifest: ogiltigt konstantnamn avvisas");
        ok(!glzlang::parseManifest(
               "lang sv Svenska\nid 1 A \"a\"\nid 1 B \"b\"\n", tmp, err),
           "manifest: två olika namn på samma id avvisas");
        ok(!glzlang::parseManifest("blaj sv\n", tmp, err),
           "manifest: okänt kommando avvisas");
        ok(glzlang::parseManifest("lang sv Svenska\nid 1 \"\"\n", tmp, err) &&
               tmp.entries.size() == 1 && tmp.entries[0].text.empty(),
           "manifest: tom literal är en giltig text");
    }
}

// ============================================================== DEL 2
//
// RUNTIME: `glaze::tr`, butiken, fallbackkontraktet och startspråket.
// ORAKLET för E5 är `glaze/strings.h` LÄST SOM TEXT — inte bin-filen,
// som annars hade fått godkänna sig själv. Lägger någon till en konstant
// utan att översätta den faller grinden med konstantens NAMN i felraden.

// `tr(id).str()` är ett KOMPILERINGSFEL med flit (`str() const&& =
// delete`) — den temporära TrText:en dör vid semikolonet och referensen
// hade dinglat. Grinden binder därför till en NAMNGIVEN TrText, precis
// som widgetkod gör när den lagrar sin `TrText` i ett fält.
std::string trS(std::uint32_t id)
{
    const glaze::TrText t = glaze::tr(id);
    return t.str();
}

std::string läsFil(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string rot()
{
    return std::string(GLAZE_TEST_ROOT) + "/";
}

// Plockar `inline constexpr std::uint32_t <Namn> = <tal>;` ur headern.
std::vector<std::pair<std::string, std::uint32_t>> konstanterUrHeadern()
{
    std::vector<std::pair<std::string, std::uint32_t>> ut;
    const std::string        text = läsFil(rot() + "Glaze/widgets/include/"
                                                   "glaze/strings.h");
    static const std::string kNyckel = "inline constexpr std::uint32_t ";
    std::size_t              i = 0;
    while ((i = text.find(kNyckel, i)) != std::string::npos) {
        i += kNyckel.size();
        const std::size_t lika = text.find('=', i);
        const std::size_t slut = text.find(';', i);
        if (lika == std::string::npos || slut == std::string::npos)
            break;
        std::string namn = text.substr(i, lika - i);
        while (!namn.empty() && (namn.back() == ' ' || namn.back() == '\t'))
            namn.pop_back();
        const std::string tal = text.substr(lika + 1, slut - lika - 1);
        i = slut;
        // Bara riktiga IDENTIFIERARE: headern beskriver sin egen form i
        // en kommentar (`… std::uint32_t <Namn> = <tal>;`), och den raden
        // är dokumentation och inte en konstant.
        bool giltig = !namn.empty();
        for (char c : namn)
            giltig = giltig && ((c >= 'A' && c <= 'Z') ||
                                (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '_');
        if (!giltig)
            continue;
        ut.push_back({namn, std::uint32_t(std::strtoul(tal.c_str(), nullptr,
                                                       10))});
    }
    return ut;
}

void del2()
{
    std::printf("\n--- del 2: runtime (glaze + glaze_language) ---\n");
    std::string err;

    // 1. INBÄDDNINGEN ÄR I TAKT MED DEN COMMITTADE FILEN. Anti-drift:
    //    `lang_builtin.cpp` genereras av `glazelanguage embed` och det
    //    finns ingenting i bygget som kör om den — glömmer man steget
    //    ritar appen gamla texter medan filen visar nya.
    const std::string binFil = läsFil(rot() + "Glaze/language/language.bin");
    ok(!binFil.empty(), "language.bin går att läsa ur repot");
    ok(binFil.size() == glaze::kLanguageBinSize &&
           std::memcmp(binFil.data(), glaze::kLanguageBin, binFil.size()) == 0,
       "lang_builtin.cpp är BYTE-IDENTISK med committade language.bin "
       "(kör `glazelanguage embed` om den faller)");

    // 2. E5 — VARJE konstant i strings.h i ALLA FYRA språken.
    glzlang::File ram;
    ok(ram.openBytes(std::vector<std::uint8_t>(binFil.begin(), binFil.end()),
                     err),
       "language.bin öppnas (" + err + ")");
    ok(glzlang::requireFrameworkScope(ram, err),
       "language.bin är en RAMVERKSFIL med id 1–999 (" + err + ")");

    const auto konst = konstanterUrHeadern();
    ok(konst.size() >= 23,
       "strings.h bär " + std::to_string(konst.size()) + " konstanter");
    const char* kSprak[] = {"sv", "en", "nb", "fi"};
    for (const char* tag : kSprak) {
        const int li = ram.findLang(tag);
        if (li < 0) {
            ok(false, std::string("språket '") + tag + "' saknas i filen");
            continue;
        }
        std::string saknade;
        for (const auto& kv : konst) {
            const glzlang::EntryView* e = ram.find(li, kv.second);
            if (!e || e->text.empty())
                saknade += " " + kv.first + "(" + std::to_string(kv.second) +
                           ")";
        }
        ok(saknade.empty(), std::string("E5: '") + tag +
                                "' bär varje konstant i strings.h" +
                                (saknade.empty() ? "" : " — SAKNAR:" + saknade));
    }
    // …och inga FÖRÄLDRALÖSA id i filen.
    {
        std::string extra;
        for (std::uint32_t id : ram.allIds()) {
            bool känt = false;
            for (const auto& kv : konst)
                känt = känt || kv.second == id;
            if (!känt)
                extra += " " + std::to_string(id);
        }
        ok(extra.empty(), "inga id i language.bin utan konstant i strings.h" +
                              (extra.empty() ? "" : " — EXTRA:" + extra));
    }

    // 3. STAVADE STICKPROV med facit HÄR i testet.
    ok(glaze::setLanguage("en"), "setLanguage(\"en\")");
    ok(trS(glaze::str::Cancel) == "Cancel",
       "en: tr(Cancel) == \"Cancel\" (" +
           trS(glaze::str::Cancel) + ")");
    ok(glaze::setLanguage("fi"), "setLanguage(\"fi\")");
    ok(trS(glaze::str::Cancel) == "Peruuta",
       "fi: tr(Cancel) == \"Peruuta\" (" +
           trS(glaze::str::Cancel) + ")");
    ok(trS(glaze::str::KeyBackspace) == "Askelpalautin",
       "fi: tr(KeyBackspace) == \"Askelpalautin\" (finskans stresstest)");
    ok(glaze::setLanguage("nb") &&
           trS(glaze::str::Retry) == "Prøv igjen",
       "nb: tr(Retry) == \"Prøv igjen\"");
    ok(glaze::setLanguage("NB-no") && glaze::language() == "nb",
       "setLanguage: tvåstegsmatchning 'NB-no' → nb");
    ok(!glaze::setLanguage("de") && glaze::language() == "nb",
       "setLanguage: okänd tagg ⇒ false + OFÖRÄNDRAT språk");

    // 4. PLATSHÅLLAREN och miss-loggen. 998 är i ramverkets intervall men
    //    finns inte i filen — det är en MISS och inte en app-sträng.
    ok(trS(998) == "⟦998⟧",
       "miss: tr(998) ger platshållaren ⟦998⟧ (" + trS(998) + ")");

    // 5. FALLBACKKEDJAN. Aktivt språk först, därefter fallbacken.
    //
    // ORAKLET ÄR FILEN, inte en literal: den initiala fallbacken ska komma
    // ur RAMVERKSFILENS `defaultLang`. Hårdkodas "sv" råkar det stämma och
    // döljer att `File::defaultLang()` aldrig läses. Genom att fråga den
    // inbäddade filen mäter vi KOPPLINGEN i stället för värdet.
    //
    // ÄRLIG GRÄNS: den inbäddade filen kan inte bytas ut i en grind, så
    // ett sabotage som hårdkodar exakt filens egen tagg passerar. Det
    // fallet fångas först den dag `framework.langtxt` packas med ett annat
    // default — och DÅ faller den här raden i stället för att tiga.
    {
        glzlang::File bi;
        std::string   e2;
        std::vector<std::uint8_t> b(glaze::kLanguageBin,
                                    glaze::kLanguageBin +
                                        glaze::kLanguageBinSize);
        ok(bi.openBytes(std::move(b), e2), "inbäddad fil öppnas (" + e2 + ")");
        const std::string filensDefault =
            bi.defaultLang() < bi.langs().size()
                ? bi.langs()[bi.defaultLang()].tag
                : std::string();
        ok(!filensDefault.empty() &&
               glaze::fallbackLanguage() == filensDefault,
           "fallbacken kommer ur ramverksfilens defaultLang (\"" +
               filensDefault + "\"), inte ur en literal i C++");
    }
    ok(glaze::setFallbackLanguage("en") && glaze::fallbackLanguage() == "en",
       "setFallbackLanguage(\"en\") — komplett i ramverksbutiken");
    ok(glaze::setFallbackLanguage("sv"), "fallbacken tillbaka till 'sv'");

    // 6. LIVSTIDSGRINDEN — referensen från `str()` avser STRÄNGOBJEKTET,
    //    som lever lika länge som TrText:en; innehållet skrivs om IN
    //    PLACE. Faller den här har någon börjat returnera en temporär.
    {
        glaze::setLanguage("en");
        glaze::TrText     t   = glaze::tr(glaze::str::Cancel);
        const std::string& r1 = t.str();
        ok(r1 == "Cancel", "livstid: första upplösningen");
        glaze::setLanguage("fi");
        const std::string& r2 = t.str();
        ok(&r1 == &r2 && r1 == "Peruuta",
           "livstid: SAMMA strängobjekt, nytt innehåll efter språkbyte");

        glaze::TrText     m   = glaze::tr(998);
        const std::string& m1 = m.str();
        glaze::setLanguage("en");
        const std::string& m2 = m.str();
        ok(&m1 == &m2 && m1 == "⟦998⟧",
           "livstid: samma objekt även över en MISS");
    }

    // 7. KOPIERINGSGRINDEN — `gen_` följer med, och efter ett byte blir
    //    båda lata och räknas om var för sig.
    {
        glaze::setLanguage("sv");
        glaze::TrText a = glaze::tr(glaze::str::Cancel);
        ok(a.str() == "Avbryt", "kopiering: originalet på svenska");
        glaze::TrText b = a;   // kopia MED ifylld cache
        glaze::setLanguage("en");
        ok(a.str() == "Cancel" && b.str() == "Cancel",
           "kopiering: båda räknas om var för sig efter språkbyte");
        glaze::TrText c;
        c = a;                 // tilldelning efter bytet
        ok(c.str() == "Cancel" && c.id() == glaze::str::Cancel,
           "kopiering: tilldelning bär id:t vidare");
    }

    // 8. TOKENSEMANTIKEN — id mot id, literal mot literal, ALDRIG blandat.
    {
        ok(glaze::tr(1) == glaze::tr(1), "token: samma id är lika");
        ok(!(glaze::tr(1) == glaze::tr(2)), "token: olika id är olika");
        ok(glaze::TrText("Ja") == glaze::TrText(std::string("Ja")),
           "token: samma literal är lika");
        ok(!(glaze::tr(glaze::str::Yes) == glaze::TrText("Yes")),
           "token: id och literal är ALDRIG lika, inte ens vid samma text");
        ok(glaze::TrText().empty() && !glaze::tr(1).empty(),
           "token: empty() mäter den UPPLÖSTA texten");
    }

    // 9. APPBUTIKEN — fallbackkontraktet över BÅDA butikerna (E6).
    {
        glaze::setLanguage("sv");
        std::vector<glzlang::PackLang> sprak = {
            {"sv", "Svenska"}, {"en", "English"}, {"nb", "Norsk bokmål"},
            {"fi", "Suomi"}};
        // A: sv och en KOMPLETTA, nb saknar 1001 med flit.
        std::vector<glzlang::PackEntry> a = {
            {"sv", 1000, "Hej"},  {"sv", 1001, "Hej då"},
            {"en", 1000, "Hi"},   {"en", 1001, "Bye"},
            {"nb", 1000, "Hei"},
            {"fi", 1000, "Moi"},  {"fi", 1001, "Heippa"}};
        std::vector<std::uint8_t> binA;
        ok(glzlang::packBytes(sprak, a, "sv", false, binA, err),
           "appfil A packad (" + err + ")");
        ok(glaze::loadLanguageBytes(binA.data(), binA.size()),
           "loadLanguageBytes(A) — sv (fallbacken) är komplett");
        ok(trS(1000) == "Hej", "app: tr(1000) == \"Hej\"");
        ok(trS(1001) == "Hej då", "app: tr(1001) == \"Hej då\"");

        // FALLBACKEN till nb ska NEKAS — nb saknar 1001 i APPBUTIKEN.
        ok(!glaze::setFallbackLanguage("nb") &&
               glaze::fallbackLanguage() == "sv",
           "setFallbackLanguage(\"nb\") nekas: ofullständig i APPBUTIKEN, "
           "fallbacken oförändrad");

        // ETT SPRÅK SOM SAKNAR ETT ID faller till fallbacken.
        ok(glaze::setLanguage("nb") && trS(1000) == "Hei",
           "nb: eget värde för 1000");
        ok(trS(1001) == "Hej då",
           "nb: 1001 saknas ⇒ FALLBACKEN sv (" + trS(1001) + ")");

        // B: sv saknar 1001 ⇒ filen ska AVSLÅS och A behållas.
        std::vector<glzlang::PackEntry> b = {
            {"sv", 1000, "B-sv"}, {"en", 1000, "B-en"}, {"en", 1001, "B-en2"}};
        std::vector<std::uint8_t> binB;
        glzlang::packBytes(sprak, b, "sv", false, binB, err);
        ok(!glaze::loadLanguageBytes(binB.data(), binB.size()),
           "loadLanguage(B) AVSLÅS: fallbacken 'sv' blir ofullständig");
        ok(trS(1000) == "Hei",
           "…och A är kvar (tr(1000) på nb är \"Hei\", inte \"B-sv\")");

        // ROUTINGEN: en appfil kan aldrig skugga ett ramverks-id — och en
        // fil med id under golvet avslås dessutom rakt av.
        std::vector<std::uint8_t> binC;
        glzlang::packBytes(sprak, {{"sv", 2, "KAPAD"}, {"en", 2, "HIJACK"},
                                   {"nb", 2, "x"},     {"fi", 2, "y"}},
                           "sv", false, binC, err);
        ok(!glaze::loadLanguageBytes(binC.data(), binC.size()),
           "appfil med id < 1000 AVSLÅS (requireAppScope)");
        glaze::setLanguage("sv");
        ok(trS(glaze::str::Cancel) == "Avbryt",
           "ramverkets id 2 är fortfarande \"Avbryt\"");

        // EXPLICIT VAL ÖVERLEVER ETT loadLanguage (E7/LanguageSource).
        ok(glaze::setLanguage("fi") &&
               glaze::languageSource() == glaze::LanguageSource::Explicit,
           "setLanguage märker källan Explicit");
        ok(glaze::loadLanguageBytes(binA.data(), binA.size()),
           "loadLanguageBytes(A) igen");
        ok(glaze::language() == "fi",
           "EXPLICIT språkval överlever ett efterföljande loadLanguage");

        // -----------------------------------------------------------
        // LÅSNINGEN mellan fil och fallback, och de TVÅ vägarna ur den.
        //
        // A har komplett sv men inte nb; B (nedan) har komplett nb men
        // inte sv. Med A laddad och fallback sv nekas BÅDE
        // `setFallbackLanguage("nb")` (A saknar nb) och `loadLanguage(B)`
        // (B saknar sv) — två giltiga lägen utan väg emellan: ett dödläge.
        glaze::setLanguage("sv");
        std::vector<glzlang::PackEntry> d = {
            {"nb", 1000, "D-nb"}, {"nb", 1001, "D-nb2"},
            {"en", 1000, "D-en"}, {"en", 1001, "D-en2"}};
        std::vector<std::uint8_t> binD;
        glzlang::packBytes(sprak, d, "nb", false, binD, err);

        ok(!glaze::loadLanguageBytes(binD.data(), binD.size()),
           "D AVSLÅS med gällande fallback 'sv' (D har ingen sv)");
        ok(!glaze::setFallbackLanguage("nb"),
           "…och fallbacken kan inte bytas först: A saknar nb ⇒ DÖDLÄGE");

        // VÄG 1 — transaktionen: fil och fallback byts TILLSAMMANS.
        ok(glaze::loadLanguageBytes(binD.data(), binD.size(), "nb"),
           "loadLanguage(D, \"nb\") LYCKAS — båda validerade före bytet");
        ok(glaze::fallbackLanguage() == "nb",
           "…och fallbacken följde med i samma transaktion (" +
               glaze::fallbackLanguage() + ")");
        ok(trS(1000) == "D-nb", "D är laddad (" + trS(1000) + ")");

        // …och den är ATOMISK: en transaktion som inte håller ändrar
        // ingenting alls — varken fil eller fallback.
        // A saknar 1001 i nb (se packlistan ovan) ⇒ transaktionen håller
        // inte, och då får INGENTING ändras — varken filen eller
        // fallbacken. Det är atomicitetskravet.
        ok(!glaze::loadLanguageBytes(binA.data(), binA.size(), "nb"),
           "loadLanguage(A, \"nb\") AVSLÅS: A saknar 1001 i nb");
        ok(glaze::fallbackLanguage() == "nb" && trS(1000) == "D-nb",
           "…och VARKEN fil eller fallback ändrades (atomiskt)");

        // VÄG 2 — släpp appbutiken: utan appfil finns inget att krocka
        // med, och ramverkets 1–999 är oberörda.
        glaze::unloadLanguage();
        ok(trS(1000).find("1000") != std::string::npos,
           "unloadLanguage: app-id 1000 blir platshållare igen (" +
               trS(1000) + ")");
        ok(trS(glaze::str::Cancel) == "Avbryt",
           "…medan ramverkets 1–999 är ORÖRDA (" +
               trS(glaze::str::Cancel) + ")");
        ok(glaze::setFallbackLanguage("fi"),
           "…och fallbacken kan nu bytas fritt (ingen appfil nekar)");
        ok(glaze::setFallbackLanguage("sv"), "återställ fallbacken");
        ok(glaze::loadLanguageBytes(binA.data(), binA.size()),
           "A laddas om för de följande fallen");
        glaze::setLanguage("sv");
    }

    // 10. SPRÅKLISTAN — underlaget för en språkväljare.
    {
        ok(glaze::languageCount() == 4, "languageCount() == 4");
        bool alla = true;
        std::string namn;
        for (int i = 0; i < glaze::languageCount(); ++i) {
            const glaze::LanguageInfo li = glaze::languageAt(i);
            namn += (i ? " · " : "") + li.name;
            alla = alla && !li.tag.empty() && !li.name.empty();
        }
        ok(alla, "languageAt: tagg och SJÄLVBENÄMNING för alla fyra — " + namn);
        ok(namn.find("Suomi") != std::string::npos &&
               namn.find("Norsk bokmål") != std::string::npos,
           "självbenämningarna är på SITT EGET språk (Suomi, Norsk bokmål)");
        ok(glaze::languageAt(99).tag.empty(),
           "languageAt utanför intervallet ⇒ tom post, aldrig krasch");
    }
}

// STARTSPRÅKSKEDJAN — `gfx::systemLocale()`s normalisering. Bara på
// Linux: Windows och macOS läser OS-API:er som inte går att injicera med
// en miljövariabel, och deras rader står i respektive `doc/os/*/TESTS.md`.
void del3()
{
    std::printf("\n--- del 3: startspråkskedjan (gfx::systemLocale) ---\n");
#if defined(_WIN32) || defined(__APPLE__)
    std::printf("SKIP   systemLocale läser OS-API:t på den här plattformen "
                "— raden står i doc/os/*/TESTS.md\n");
#else
    auto sattEnv = [](const char* n, const char* v) {
        if (v)
            setenv(n, v, 1);
        else
            unsetenv(n);
    };
    auto prova = [&](const char* lcAll, const char* lcMsg, const char* lang,
                     const std::string& facit, const std::string& vad) {
        sattEnv("LC_ALL", lcAll);
        sattEnv("LC_MESSAGES", lcMsg);
        sattEnv("LANG", lang);
        const std::string fick = gfx::systemLocale();
        ok(fick == facit, vad + " → \"" + fick + "\" (facit \"" + facit + "\")");
    };

    prova("sv_SE.UTF-8", nullptr, nullptr, "sv-SE", "sv_SE.UTF-8 normaliseras");
    prova("nb_NO@bokmal", nullptr, nullptr, "nb-NO",
          "variant efter '@' kapas");
    prova("fi", nullptr, nullptr, "fi", "naken tagg passerar");
    prova("C", nullptr, nullptr, "", "C är FRÅNVARO av val ⇒ tom sträng");
    prova("POSIX", nullptr, nullptr, "", "POSIX ⇒ tom sträng");
    prova(nullptr, nullptr, nullptr, "", "ingen variabel satt ⇒ tom sträng");
    prova("", nullptr, "sv_SE", "sv-SE", "TOM LC_ALL räknas som osatt");
    prova("en_US", "nb_NO", "fi_FI", "en-US", "LC_ALL slår LC_MESSAGES");
    prova(nullptr, "nb_NO", "fi_FI", "nb-NO", "LC_MESSAGES slår LANG");
    prova(nullptr, nullptr, "fi_FI", "fi-FI", "LANG är golvet");
#endif
}

// ============================================================== DEL 4
//
// HÅRDKODADE ANVÄNDARTEXTER. Ramverket ritar
// varenda pixel och äger därför varenda sträng — en etikett som står som en
// literal i widget-koden följer inte språket. Del 4 läser varje .cpp och .h
// under `Glaze/widgets/src` och `Glaze/widgets/include` som TEXT och fäller
// en strängliteral som SER UT SOM TEXT TILL EN MÄNNISKA:
//
//   A  en bokstav utanför ASCII (å, ä, ö, æ, ø, é …),
//   B  två ord om minst två bokstäver med blanksteg emellan ("Alla filer"),
//   C  en inledande VERSAL i ett ord om minst två bokstäver ("Avbryt", "OK").
//
// Det som INTE är användartext och därför inte prövas:
//   * kommentarer och preprocessorrader;
//   * argumenten till `gfx::log`, `std::printf`/`fprintf`/`snprintf` och
//     `static_assert` — loggtexter läses av utvecklare, över hur många rader
//     anropet än sträcker sig;
//   * en literal som JÄMFÖRS (`key == "text"`, `mod == "Ctrl"`) — den läses
//     in, den visas aldrig.
//
// UNDANTAG står i KODEN, vid det de undantar, och bär sitt skäl:
//   `// lang_gate:rad <skäl>`  — literalerna på samma rad,
//   `// lang_gate:av <skäl>` … `// lang_gate:på` — ett block.
// Ett undantag utan skäl, ett `av` utan `på` och ett `på` utan `av` fäller
// grinden — ett glömt `på` hade annars tystat resten av filen.
//
// GRÄNSEN, skriven: ett ord i GEMENER ("januari") ser ut som en
// egenskapsnyckel ("text", "left") och fångas inte. Reglerna fångar det en
// knapp, en titel, en tooltip och en mening bär. Självtestet nedan är
// facit — det fäller grinden om en regel eller en hoppregel slutar verka.

struct Fynd {
    std::string fil;
    int         rad = 0;
    std::string text;
};

bool identTecken(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Kommentarerna blir blanksteg (radbrytningarna står kvar, så radnumren
// gäller), literalerna står orörda.
std::string utanKommentarer(const std::string& s)
{
    std::string ut = s;
    std::size_t i  = 0;
    const std::size_t n = s.size();
    while (i < n) {
        if (s.compare(i, 2, "//") == 0) {
            while (i < n && s[i] != '\n')
                ut[i++] = ' ';
        } else if (s.compare(i, 2, "/*") == 0) {
            const std::size_t slut = s.find("*/", i + 2);
            const std::size_t e = slut == std::string::npos ? n : slut + 2;
            for (; i < e; ++i)
                if (s[i] != '\n')
                    ut[i] = ' ';
        } else if (s[i] == '"' || s[i] == '\'') {
            // Ett tecken-citat efter ett TAL är en sifferavskiljare (1'000).
            if (s[i] == '\'') {
                std::size_t b = i;
                while (b > 0 && identTecken(s[b - 1]))
                    --b;
                if (b < i && s[b] >= '0' && s[b] <= '9') {
                    ++i;
                    continue;
                }
            }
            const char q = s[i++];
            while (i < n && s[i] != q && s[i] != '\n')
                i += (s[i] == '\\') ? 2 : 1;
            ++i;
        } else {
            ++i;
        }
    }
    return ut;
}

// Literalens INNEHÅLL med escapes avkodade (\xHH, \n, \t, \\, \", \').
std::string avkoda(const std::string& r)
{
    std::string ut;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (r[i] != '\\' || i + 1 >= r.size()) {
            ut += r[i];
            continue;
        }
        const char e = r[++i];
        if (e == 'x') {
            unsigned v = 0;
            int      k = 0;
            while (k < 2 && i + 1 < r.size() &&
                   std::isxdigit(static_cast<unsigned char>(r[i + 1]))) {
                const char h = r[++i];
                v = v * 16 + unsigned(std::isdigit(static_cast<unsigned char>(h))
                                          ? h - '0'
                                          : (std::tolower(h) - 'a' + 10));
                ++k;
            }
            ut += char(v);
        } else if (e == 'n' || e == 't' || e == 'r') {
            ut += ' ';
        } else {
            ut += e;
        }
    }
    return ut;
}

// En BOKSTAV vid `i`: ASCII, eller Latin-1-tillägget/Latin Extended-A i
// UTF-8 (C3 80–BF utom × och ÷, C4–C5 + fortsättningsbyte). `len` är dess
// längd i byte, `versal` om den är stor.
bool bokstav(const std::string& s, std::size_t i, std::size_t& len,
             bool& versal)
{
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        len    = 1;
        versal = c <= 'Z';
        return true;
    }
    if (i + 1 >= s.size())
        return false;
    const unsigned char d = static_cast<unsigned char>(s[i + 1]);
    if (c == 0xC3 && d >= 0x80 && d <= 0xBF && d != 0x97 && d != 0xB7) {
        len    = 2;
        versal = d < 0x9F;
        return true;
    }
    if ((c == 0xC4 || c == 0xC5) && d >= 0x80 && d <= 0xBF) {
        len    = 2;
        versal = (d % 2) == 0;   // Latin Extended-A växlar stor/liten
        return true;
    }
    return false;
}

// A, B eller C ovan — med regelns bokstav i svaret, eller 0.
char serUtSomText(const std::string& t)
{
    // Orden: längd i bokstäver och om de står åtskilda av blanksteg.
    struct Ord {
        int  n;
        bool versal;
        bool blankFöre;
        bool först;
    };
    std::vector<Ord> ord;
    bool             utanförAscii = false;
    bool             blank        = false;
    bool             början       = true;   // inget annat än blanksteg än
    for (std::size_t i = 0; i < t.size();) {
        std::size_t len = 0;
        bool        v   = false;
        if (bokstav(t, i, len, v)) {
            utanförAscii = utanförAscii || len > 1;
            Ord o{0, v, blank, början};
            while (i < t.size() && bokstav(t, i, len, v)) {
                ++o.n;
                i += len;
                utanförAscii = utanförAscii || len > 1;
            }
            ord.push_back(o);
            blank  = false;
            början = false;
            continue;
        }
        blank  = t[i] == ' ';
        början = början && t[i] == ' ';
        ++i;
    }
    if (utanförAscii)
        return 'A';
    for (std::size_t k = 1; k < ord.size(); ++k)
        if (ord[k].blankFöre && ord[k].n >= 2 && ord[k - 1].n >= 2)
            return 'B';
    if (!ord.empty() && ord[0].först && ord[0].versal && ord[0].n >= 2)
        return 'C';
    return 0;
}

// Skannar EN källfils text. `undantagsfel` får fel i själva markörerna.
std::vector<Fynd> skanna(const std::string& fil, const std::string& text,
                         std::vector<Fynd>& undantagsfel)
{
    // 1. Markörerna läses ur RÅTEXTEN, rad för rad.
    std::vector<bool> undantagen;
    {
        bool        block = false;
        int         nr    = 0;
        std::size_t p     = 0;
        while (p <= text.size()) {
            const std::size_t e  = text.find('\n', p);
            std::string ln = text.substr(
                p, (e == std::string::npos ? text.size() : e) - p);
            if (!ln.empty() && ln.back() == '\r')
                ln.pop_back();
            ++nr;
            bool              denna = block;
            const std::size_t m     = ln.find("lang_gate:");
            const std::size_t kom   = ln.find("//");
            if (m != std::string::npos && kom != std::string::npos && kom < m) {
                std::string rest = ln.substr(m + 10);
                const auto  vad  = rest.substr(0, rest.find(' '));
                std::string skäl = rest.size() > vad.size()
                                       ? rest.substr(vad.size())
                                       : std::string();
                const bool harSkäl =
                    skäl.find_first_not_of(" \t\r—-") != std::string::npos;
                if (vad == "rad") {
                    denna = true;
                    if (!harSkäl)
                        undantagsfel.push_back({fil, nr, "rad utan skäl"});
                } else if (vad == "av") {
                    if (block)
                        undantagsfel.push_back({fil, nr, "av inuti av"});
                    if (!harSkäl)
                        undantagsfel.push_back({fil, nr, "av utan skäl"});
                    block = denna = true;
                } else if (vad == "på") {
                    if (!block)
                        undantagsfel.push_back({fil, nr, "på utan av"});
                    block = false;
                }
                // Något annat efter `lang_gate:` (en kommentar som NÄMNER
                // grinden, eller ett felstavat ord) undantar ingenting — och
                // det är den säkra riktningen: literalen prövas som vanligt.
            }
            undantagen.push_back(denna);
            if (e == std::string::npos)
                break;
            p = e + 1;
        }
        if (block)
            undantagsfel.push_back({fil, nr, "av utan på"});
    }

    // 2. Literalerna i texten UTAN kommentarer, med anropsdjupet räknat så
    //    att argumenten till ett loggande anrop kan hoppas över.
    static const char* const kTysta[] = {"gfx::log", "std::printf",
                                         "std::fprintf", "std::snprintf",
                                         "static_assert"};
    const std::string s = utanKommentarer(text);
    std::vector<Fynd> ut;
    int               rad    = 1;
    int               djup   = 0;
    int               tystTill = -1;   // djupet där ett tyst anrop öppnade
    bool              radStart = true;
    bool              preproc  = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\n') {
            ++rad;
            radStart = true;
            preproc  = false;
            continue;
        }
        if (radStart && c != ' ' && c != '\t' && c != '\r') {
            preproc  = c == '#';
            radStart = false;
        }
        if (c == '(') {
            // Står ett tyst namn omedelbart före (blanksteg tillåtna)?
            if (tystTill < 0) {
                std::size_t b = i;
                while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t'))
                    --b;
                for (const char* namn : kTysta) {
                    const std::size_t L = std::strlen(namn);
                    if (b >= L && s.compare(b - L, L, namn) == 0 &&
                        (b == L || !identTecken(s[b - L - 1]))) {
                        tystTill = djup;
                        break;
                    }
                }
            }
            ++djup;
            continue;
        }
        if (c == ')') {
            --djup;
            if (djup == tystTill)
                tystTill = -1;
            continue;
        }
        if (c == '\'') {   // teckenliteral eller sifferavskiljare
            std::size_t b = i;
            while (b > 0 && identTecken(s[b - 1]))
                --b;
            if (b < i && s[b] >= '0' && s[b] <= '9')
                continue;
            for (++i; i < s.size() && s[i] != '\'' && s[i] != '\n';)
                i += (s[i] == '\\') ? 2 : 1;
            continue;
        }
        if (c != '"')
            continue;
        // RÅ literal: R"avgr( … )avgr"
        const bool rå = i > 0 && s[i - 1] == 'R' &&
                        (i < 2 || !identTecken(s[i - 2]) || s[i - 2] == '8' ||
                         s[i - 2] == 'u' || s[i - 2] == 'U' ||
                         s[i - 2] == 'L');
        std::size_t start = i, slut = i;
        std::string innehåll;
        if (rå) {
            const std::size_t par = s.find('(', i);
            const std::string avgr = ")" + s.substr(i + 1, par - i - 1) + "\"";
            const std::size_t e = s.find(avgr, par);
            innehåll = s.substr(par + 1, e - par - 1);
            slut     = e + avgr.size() - 1;
        } else {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != '"' && s[j] != '\n')
                j += (s[j] == '\\') ? 2 : 1;
            innehåll = avkoda(s.substr(i + 1, j - i - 1));
            slut     = j;
        }
        const int startRad = rad;
        for (std::size_t k = start; k < slut && k < s.size(); ++k)
            if (s[k] == '\n')
                ++rad;
        i = slut;

        if (preproc || tystTill >= 0)
            continue;
        if (startRad - 1 < int(undantagen.size()) && undantagen[startRad - 1])
            continue;
        // Jämförs literalen? Granne `==`/`!=` på någon sida.
        std::size_t f = start;
        while (f > 0 && (s[f - 1] == ' ' || s[f - 1] == '\t'))
            --f;
        while (f > 0 && identTecken(s[f - 1]))   // prefixet (u8, L …)
            --f;
        while (f > 0 && (s[f - 1] == ' ' || s[f - 1] == '\t'))
            --f;
        std::size_t e = slut + 1;
        while (e < s.size() && (s[e] == ' ' || s[e] == '\t'))
            ++e;
        const bool jämförs =
            (f >= 2 && (s.compare(f - 2, 2, "==") == 0 ||
                        s.compare(f - 2, 2, "!=") == 0)) ||
            s.compare(e, 2, "==") == 0 || s.compare(e, 2, "!=") == 0;
        if (jämförs)
            continue;
        if (const char regel = serUtSomText(innehåll))
            ut.push_back({fil, startRad,
                          std::string(1, regel) + "  \"" + innehåll + "\""});
    }
    return ut;
}

void del4()
{
    std::printf("\n--- del 4: hårdkodade användartexter i Glaze/widgets ---\n");

    // SJÄLVTESTET — facit skrivet här. Tre ska fångas (en per regel), resten
    // ska passera: loggen över två rader, kommentaren, jämförelsen, de två
    // undantagsformerna, egenskapsnyckeln, ISO-formen, prefixet och
    // sifferavskiljaren som inte får öppna en teckenliteral.
    {
        const std::string prov =
            "void f() {\n"                                            // 1
            "    b.setText(\"Avbryt\");\n"                            // 2 C
            "    b.setToolTip(\"upp en nivå\");\n"                    // 3 A
            "    x = \"show hidden files\";\n"                        // 4 B
            "    gfx::log(\"Mappen finns inte: %s\",\n"               // 5
            "             \"Över flera rader\");\n"                   // 6
            "    // b.setText(\"I en kommentar\");\n"                 // 7
            "    if (key == \"Text\") {}\n"                           // 8
            "    y = \"Regular\"; // lang_gate:rad API-namnet\n"      // 9
            "    // lang_gate:av kanoniska namn\n"                    // 10
            "    z = \"Num Home\";\n"                                 // 11
            "    // lang_gate:på\n"                                   // 12
            "    w = \"selectedIndex\"; d = \"%Y-%m-%d %H:%M\";\n"    // 13
            "    k = \"k\"; n = 1'000; m = \"Mt\";\n"                 // 14 C
            "}\n";
        std::vector<Fynd> uf;
        const auto        f = skanna("prov", prov, uf);
        std::string       fick;
        for (const Fynd& x : f)
            fick += " " + std::to_string(x.rad) + x.text.substr(0, 1);
        ok(uf.empty() && fick == " 2C 3A 4B 14C",
           "del 4 självtest: fångar rad 2 (C), 3 (A), 4 (B), 14 (C) och "
           "inget annat — fick" + fick);

        std::vector<Fynd> uf2;
        skanna("prov2",
               "// lang_gate:av\nx = \"Ok\";\n// lang_gate:på\n"
               "// lang_gate:rad\n// lang_gate:av skäl\n",
               uf2);
        ok(uf2.size() == 3,
           "del 4 självtest: av utan skäl, rad utan skäl och av utan på "
           "fäller (" + std::to_string(uf2.size()) + " av 3)");
    }

    // DEN RIKTIGA SKANNINGEN.
    namespace fs = std::filesystem;
    std::vector<Fynd> alla, undantagsfel;
    int               filer = 0, undantag = 0;
    for (const char* under : {"Glaze/widgets/src", "Glaze/widgets/include"}) {
        std::vector<fs::path> sökvägar;
        for (const auto& e : fs::recursive_directory_iterator(rot() + under)) {
            const std::string ext = e.path().extension().string();
            const std::string namn = e.path().filename().string();
            if (!e.is_regular_file() || (ext != ".cpp" && ext != ".h") ||
                namn.rfind("lang_builtin", 0) == 0)
                continue;
            sökvägar.push_back(e.path());
        }
        std::sort(sökvägar.begin(), sökvägar.end());
        for (const fs::path& p : sökvägar) {
            const std::string text = läsFil(p.string());
            const std::string rel =
                fs::relative(p, rot()).generic_string();
            for (std::size_t k = text.find("lang_gate:"); k != std::string::npos;
                 k = text.find("lang_gate:", k + 1))
                ++undantag;
            const auto f = skanna(rel, text, undantagsfel);
            alla.insert(alla.end(), f.begin(), f.end());
            ++filer;
        }
    }
    for (const Fynd& x : undantagsfel)
        std::printf("       %s:%d: undantagsmarkören: %s\n", x.fil.c_str(),
                    x.rad, x.text.c_str());
    for (const Fynd& x : alla)
        std::printf("       %s:%d: %s\n", x.fil.c_str(), x.rad,
                    x.text.c_str());
    ok(filer > 50, "del 4 läste " + std::to_string(filer) + " filer");
    ok(undantagsfel.empty(),
       "undantagsmarkörerna är hela (" + std::to_string(undantag) +
           " markörer)");
    ok(alla.empty(),
       "ingen hårdkodad användartext i Glaze/widgets — " +
           std::to_string(alla.size()) +
           " fynd (gör texten till ett ramverks-id i glaze/strings.h, "
           "eller undanta den med `// lang_gate:rad <skäl>`)");
}

} // namespace

int main()
{
    // OS-LOKALEN SÄTTS FÖRE ALLT ANNAT: den automatiska upplösningen sker
    // vid FÖRSTA uppslaget och exakt en gång per process, så det här är
    // enda tillfället att mäta kedjans sista led. `de` finns inte i filen
    // ⇒ svaret ska bli ENGELSKA, inte svenska.
#if !defined(_WIN32) && !defined(__APPLE__)
    setenv("LC_ALL", "de_DE.UTF-8", 1);
    unsetenv("LC_MESSAGES");
    unsetenv("LANG");
#endif

    std::printf("=== lang_gate — språkpaketets grind ===\n");
    del1();

#if !defined(_WIN32) && !defined(__APPLE__)
    ok(glaze::languageSource() == glaze::LanguageSource::Automatic,
       "startspråk: källan är Automatic innan appen sagt något");
    ok(glaze::language() == "en",
       "startspråk: OKÄND OS-lokal (de) ⇒ ENGELSKA (" + glaze::language() +
           ")");
#endif

    del2();
    del3();
    del4();
    // EN LOGG PER MISS, MÄTT och inte påstådd. Kommer SIST, för den
    // omdirigerar stderr och kan inte lämna tillbaka den portabelt.
    // `resolve()` körs 6+ gånger per frame — en loggrad per uppslag hade
    // gjort en enda saknad översättning till loggspam.
    {
        const char* logg = "lang_gate_miss.log";
        std::fflush(stderr);
        if (!std::freopen(logg, "w", stderr)) {
            ok(false, "loggmätning: kunde inte omdirigera stderr");
        } else {
            for (int i = 0; i < 5; ++i)
                trS(997);                    // samma id, fem uppslag
            trS(996);                        // ett ANNAT id
            std::fflush(stderr);
#if defined(_WIN32)
            std::freopen("NUL", "w", stderr);
#else
            std::freopen("/dev/null", "w", stderr);
#endif
            const std::string text = läsFil(logg);
            auto räkna = [&](const char* nål) {
                int         n = 0;
                std::size_t i = 0;
                while ((i = text.find(nål, i)) != std::string::npos) {
                    ++n;
                    ++i;
                }
                return n;
            };
            const int n997 = räkna("997");
            const int n996 = räkna("996");
            ok(n997 == 1, "miss-loggen: FEM uppslag av id 997 gav " +
                              std::to_string(n997) + " loggrad(er), facit 1");
            ok(n996 == 1, "miss-loggen: ett ANNAT id loggas för sig (" +
                              std::to_string(n996) + ", facit 1)");
            std::remove(logg);
        }
    }

    std::printf(fel ? "\nlang_gate: %d fall FÖLL\n"
                    : "\nlang_gate: allt grönt (%d fel)\n",
                fel);
    return fel ? 1 : 0;
}
