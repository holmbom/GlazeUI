// language.bin-formatet — läsare, packare, manifest, dump. REN STD.
//
// Layout (allt little-endian; läses/skrivs BYTE FÖR BYTE via rd*/wr* —
// aldrig memcpy av packad struct: noll alignment-UB, noll #pragma pack,
// en framtida big-endian-port är en enda funktion). Formen speglar
// assets.bin med flit, men INTE i CRC-täckning och skrivsemantik — där
// har assets luckor ett semantiskt id→text-format inte får ärva.
//
//   Header — 64 byte
//     0  magic          "GLZLANGS" (8, ASCII, ingen NUL)
//     8  versionMajor   u16   != vår ⇒ VÄGRA (inkompatibel layout)
//    10  versionMinor   u16   > vår ⇒ acceptera, hoppa okänt PÅ LÄNGD
//    12  headerSize     u32   >= 64
//    16  langCount      u32   >= 1
//    20  langOffset     u32   absolut, >= headerSize
//    24  langRecSize    u32   >= 24
//    28  entryCount     u32   == Σ språkens entryCount
//    32  entryOffset    u32   absolut
//    36  entryRecSize   u32   >= 16
//    40  stringOffset   u32   absolut; UTF-8, NUL-terminerade
//    44  stringBytes    u32
//    48  fileCrc32      u32   CRC32 över HELA FILEN med bytes 48–51
//                             räknade som NOLL
//    52  appIdFloor     u32   exakt 1000 i version 1
//    56  defaultLang    u16   < langCount
//    58  flags          u16   bit 0 = ramverksfil; okända bitar ⇒ VÄGRA
//    60  fileSize       u32   == faktisk filstorlek
//
//   Språkpost — 24 byte: tagOffset/tagLen u32, nameOffset/nameLen u32,
//     firstEntry/entryCount u32. Offsets är RELATIVA strängtabellen.
//
//   Entrypost — 16 byte: id u32 (!= 0), langIndex u32 (redundant ⇒
//     VERIFIERBAR bakpekare), textOffset/textLen u32.
//
// KONTRAKTET utöver fältgränserna: entries stigande sorterade på id inom
// sitt språk; språkens sviter DISJUNKTA och LUCKFRIA över [0, entryCount).
//
// ---------------------------------------------------------------------
// CRC:N TÄCKER HELA FILEN, och den kontrolleras TIDIGT — två avvikelser
// från assets_file.cpp, båda medvetna:
//
//   * assets CRC:ar bara blobben, så headerns offsets, versionsfält och
//     defaultLang låg utanför skyddet. Här räknas HELA filen med
//     CRC-fältets fyra bytes som noll, alltså skyddas magic, samtliga
//     offsets, recordstorlekarna, golvet, defaultLang, flaggorna,
//     fileSize, tabellerna och strängarna. En framtida större headerSize
//     täcks i sin helhet, utan kodändring.
//   * assets kontrollerar CRC:n SIST ("strukturfel före CRC-fel"). Här
//     kommer den FÖRST, direkt efter magic/major/fileSize. Skälet är att
//     täckningen ovan gör varje muterad byte till ett CRC-fel: en
//     muterad `langIndex` skulle annars rapporteras som en inkonsistent
//     bakpekare, vilket är en HÄRLEDD diagnos av en TRASIG fil. Filen är
//     korrupt, inte felformad — och sabotagegrinden mäter exakt
//     det: fyra olika muterade bytes ska alla ge CRC-avslag.

#include "glaze_language/language_file.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace glzlang {
namespace {

constexpr std::uint32_t kHeaderSize   = 64;
constexpr std::uint32_t kLangRecSize  = 24;
constexpr std::uint32_t kEntryRecSize = 16;
constexpr std::uint32_t kCrcOffset    = 48;
constexpr char          kMagic[8]     = {'G','L','Z','L','A','N','G','S'};

// ------------------------------------------------------------ byte-läsning

std::uint16_t rd16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (std::uint32_t(p[1]) << 8));
}

std::uint32_t rd32(const std::uint8_t* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void wr16(std::vector<std::uint8_t>& v, std::uint16_t x)
{
    v.push_back(std::uint8_t(x & 0xFF));
    v.push_back(std::uint8_t(x >> 8));
}

void wr32(std::vector<std::uint8_t>& v, std::uint32_t x)
{
    v.push_back(std::uint8_t(x & 0xFF));
    v.push_back(std::uint8_t((x >> 8) & 0xFF));
    v.push_back(std::uint8_t((x >> 16) & 0xFF));
    v.push_back(std::uint8_t(x >> 24));
}

// Tabellfri CRC32 (polynom 0xEDB88320) — språkfiler är små, hastigheten
// spelar ingen roll; noll statiskt data. Samma polynom som assets, men
// med ETT HÅL: fyra bytes vid `hal` räknas som NOLL. Formen finns bara i
// EN funktion, så packaren och läsaren omöjligt kan räkna olika — den
// avvikelsen hade varit osynlig tills en fil vägrades.
std::uint32_t crc32MedHal(const std::uint8_t* p, std::size_t n,
                          std::size_t hal)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b =
            (i >= hal && i < hal + 4) ? std::uint8_t(0) : p[i];
        crc ^= b;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
std::string fmt(const char* f, ...)
{
    char    buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// Strängkontroll: (off, len) relativt strängtabellen — inom regionen och
// NUL-terminerad. En TOM sträng är giltig och kräver ändå sin NUL.
bool readStr(const std::uint8_t* strBase, std::uint64_t strBytes,
             std::uint32_t off, std::uint32_t len, std::string& out)
{
    if (std::uint64_t(off) + len >= strBytes)   // >= — NUL:en ska rymmas
        return false;
    if (strBase[off + len] != 0)
        return false;
    out.assign(reinterpret_cast<const char*>(strBase + off), len);
    return true;
}

// BCP-47-FORM (inte registret): subtaggar av ASCII-alnum skilda med '-',
// varje 1–8 tecken, den första 2–8 BOKSTÄVER. Vi validerar FORMEN och
// inte innehållet — "zz" är en laglig privat tagg, "sv--SE" är det inte.
bool giltigTagg(const std::string& t)
{
    if (t.empty() || t.size() > 35)
        return false;
    std::size_t i     = 0;
    bool        forsta = true;
    while (i < t.size()) {
        std::size_t j = t.find('-', i);
        if (j == std::string::npos)
            j = t.size();
        const std::size_t n = j - i;
        if (n < 1 || n > 8)
            return false;
        for (std::size_t k = i; k < j; ++k) {
            const char c = t[k];
            const bool alfa = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            const bool siffra = c >= '0' && c <= '9';
            if (!alfa && !siffra)
                return false;
            if (forsta && !alfa)
                return false;
        }
        if (forsta && n < 2)
            return false;
        forsta = false;
        if (j == t.size())
            break;
        i = j + 1;
        if (i == t.size())
            return false;   // avslutande '-'
    }
    return true;
}

} // namespace

// NORMALISERINGEN — gemener och '_' → '-'. Fri funktion i namespacet
// eftersom både läsaren, packaren och `findLang` måste göra EXAKT samma
// sak; två stavningar av samma tagg vore två olika språk.
std::string normaliseraTagg(const std::string& t)
{
    std::string ut;
    ut.reserve(t.size());
    for (char c : t) {
        if (c == '_')
            ut += '-';
        else if (c >= 'A' && c <= 'Z')
            ut += char(c - 'A' + 'a');
        else
            ut += c;
    }
    return ut;
}

// ---------------------------------------------------------------- läsaren

bool File::openFile(const std::string& path, std::string& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "kan inte läsa " + path;
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) {
        err = "läsfel i " + path;
        return false;
    }
    return openBytes(std::move(bytes), err);
}

bool File::openBytes(std::vector<std::uint8_t> bytes, std::string& err)
{
    err.clear();
    // Vid minsta fel: lämna objektet tomt — ladda ALLT eller INGET.
    bytes_.clear();
    langs_.clear();
    entries_.clear();
    verMajor_ = verMinor_ = 0;
    defaultLang_ = flags_ = 0;

    const std::uint64_t size = bytes.size();
    const std::uint8_t* p    = bytes.data();

    if (size < kHeaderSize) {
        err = fmt("trunkerad: %llu byte, headern kräver %u",
                  (unsigned long long)size, kHeaderSize);
        return false;
    }
    if (std::memcmp(p, kMagic, 8) != 0) {
        err = "fel magic — inte en language.bin";
        return false;
    }

    const std::uint16_t major = rd16(p + 8);
    const std::uint16_t minor = rd16(p + 10);
    if (major != kVersionMajor) {
        err = fmt("versionMajor %u stöds inte (denna läsare: %u)",
                  major, kVersionMajor);
        return false;
    }

    const std::uint32_t headerSize   = rd32(p + 12);
    const std::uint32_t langCount    = rd32(p + 16);
    const std::uint32_t langOffset   = rd32(p + 20);
    const std::uint32_t langRecSize  = rd32(p + 24);
    const std::uint32_t entryCount   = rd32(p + 28);
    const std::uint32_t entryOffset  = rd32(p + 32);
    const std::uint32_t entryRecSize = rd32(p + 36);
    const std::uint32_t stringOffset = rd32(p + 40);
    const std::uint32_t stringBytes  = rd32(p + 44);
    const std::uint32_t fileCrc      = rd32(p + kCrcOffset);
    const std::uint32_t appIdFloor   = rd32(p + 52);
    const std::uint16_t defaultLang  = rd16(p + 56);
    const std::uint16_t flags        = rd16(p + 58);
    const std::uint32_t fileSize     = rd32(p + 60);

    if (headerSize < kHeaderSize) {
        err = fmt("ogiltig headerSize %u (min %u)", headerSize, kHeaderSize);
        return false;
    }
    if (fileSize != size) {
        err = fmt("fileSize i headern (%u) matchar inte filens längd (%llu)",
                  fileSize, (unsigned long long)size);
        return false;
    }

    // CRC:N FÖRST — se filhuvudet. Täckningen är hela filen, så varje
    // muterad byte ÄR ett CRC-fel; en härledd strukturdiagnos hade dolt
    // orsaken.
    const std::uint32_t calc = crc32MedHal(p, std::size_t(size), kCrcOffset);
    if (calc != fileCrc) {
        err = fmt("CRC-fel (fil: %08x, beräknad: %08x)", fileCrc, calc);
        return false;
    }

    if (appIdFloor != kAppIdFloor) {
        err = fmt("appIdFloor %u — version 1 kräver exakt %u",
                  appIdFloor, kAppIdFloor);
        return false;
    }
    if (flags & ~std::uint16_t(kKnownFlags)) {
        err = fmt("okänd flaggbit i flags 0x%04x", flags);
        return false;
    }
    if (langCount < 1) {
        err = "langCount är 0 — filen bär inget språk";
        return false;
    }
    if (defaultLang >= langCount) {
        err = fmt("defaultLang %u utanför langCount %u",
                  defaultLang, langCount);
        return false;
    }
    if (langRecSize < kLangRecSize) {
        err = fmt("ogiltig langRecSize %u (min %u)", langRecSize, kLangRecSize);
        return false;
    }
    if (entryRecSize < kEntryRecSize) {
        err = fmt("ogiltig entryRecSize %u (min %u)",
                  entryRecSize, kEntryRecSize);
        return false;
    }

    // SEKTIONERNA — monotona och disjunkta, all matte i 64 bitar och med
    // de DEKLARERADE recordstorlekarna (en nyare minor med större poster
    // ska läsas rätt, inte mätas mot våra konstanter).
    const std::uint64_t langEnd =
        std::uint64_t(langOffset) + std::uint64_t(langCount) * langRecSize;
    const std::uint64_t entryEnd =
        std::uint64_t(entryOffset) + std::uint64_t(entryCount) * entryRecSize;
    const std::uint64_t strEnd = std::uint64_t(stringOffset) + stringBytes;
    if (langOffset < headerSize) {
        err = "språktabellen överlappar headern";
        return false;
    }
    if (langEnd > entryOffset) {
        err = "språktabellen överlappar entrytabellen";
        return false;
    }
    if (entryEnd > stringOffset) {
        err = "entrytabellen överlappar strängtabellen";
        return false;
    }
    if (strEnd > size) {
        err = "strängtabellen ligger utanför filen";
        return false;
    }
    if (stringBytes == 0) {
        err = "strängtabellen är tom";
        return false;
    }

    const std::uint8_t* strBase = p + stringOffset;

    // Språktabellen. Taggarna normaliseras och måste vara unika — två
    // stavningar av samma språk vore tyst tvetydiga.
    std::vector<LangView> langs;
    std::set<std::string> taggar;
    langs.reserve(langCount);
    for (std::uint32_t i = 0; i < langCount; ++i) {
        const std::uint8_t* rec = p + langOffset + std::uint64_t(i) * langRecSize;
        LangView l;
        const std::uint32_t tagOff  = rd32(rec + 0);
        const std::uint32_t tagLen  = rd32(rec + 4);
        const std::uint32_t nameOff = rd32(rec + 8);
        const std::uint32_t nameLen = rd32(rec + 12);
        l.firstEntry = rd32(rec + 16);
        l.entryCount = rd32(rec + 20);
        std::string ratagg;
        if (!readStr(strBase, stringBytes, tagOff, tagLen, ratagg)) {
            err = fmt("språk %u: taggen ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        if (!readStr(strBase, stringBytes, nameOff, nameLen, l.name)) {
            err = fmt("språk %u: namnet ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        l.tag = normaliseraTagg(ratagg);
        if (!giltigTagg(l.tag)) {
            err = "ogiltig språktagg '" + ratagg + "' (BCP-47-formen)";
            return false;
        }
        if (l.name.empty()) {
            err = "språket '" + l.tag + "' saknar självbenämning";
            return false;
        }
        if (std::uint64_t(l.firstEntry) + l.entryCount > entryCount) {
            err = "språket '" + l.tag +
                  "': firstEntry+entryCount utanför entrytabellen";
            return false;
        }
        if (!taggar.insert(l.tag).second) {
            err = "dubblerad språktagg '" + l.tag + "'";
            return false;
        }
        langs.push_back(std::move(l));
    }

    // Sviterna DISJUNKTA och LUCKFRIA: varje entry täcks av exakt ett
    // språk. Det ÄR "Σ språkens entryCount == entryCount" plus lite mer —
    // summan ensam kan stämma medan två sviter överlappar.
    {
        std::vector<std::uint8_t> tackt(entryCount, 0);
        std::uint64_t summa = 0;
        for (const LangView& l : langs) {
            summa += l.entryCount;
            for (std::uint32_t i = l.firstEntry;
                 i < l.firstEntry + l.entryCount; ++i) {
                if (tackt[i]) {
                    err = "språksviterna överlappar vid post " +
                          std::to_string(i);
                    return false;
                }
                tackt[i] = 1;
            }
        }
        if (summa != entryCount) {
            err = fmt("entryCount %u men språkens summa är %llu",
                      entryCount, (unsigned long long)summa);
            return false;
        }
        for (std::uint32_t i = 0; i < entryCount; ++i)
            if (!tackt[i]) {
                err = "post " + std::to_string(i) + " tillhör inget språk";
                return false;
            }
    }

    // Entrytabellen. `langIndex` är redundant mot sviterna — AVSIKTLIGT:
    // bakpekaren gör tabellen verifierbar.
    std::vector<EntryView> entries;
    entries.reserve(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint8_t* rec =
            p + entryOffset + std::uint64_t(i) * entryRecSize;
        EntryView e;
        e.id                        = rd32(rec + 0);
        e.langIndex                 = rd32(rec + 4);
        const std::uint32_t textOff = rd32(rec + 8);
        const std::uint32_t textLen = rd32(rec + 12);
        if (e.id == 0) {
            err = fmt("post %u: id 0 är reserverat för \"ingen "
                      "översättning\"", i);
            return false;
        }
        if (e.langIndex >= langCount) {
            err = fmt("post %u: langIndex %u utanför språktabellen",
                      i, e.langIndex);
            return false;
        }
        const LangView& l = langs[e.langIndex];
        if (i < l.firstEntry || i >= l.firstEntry + l.entryCount) {
            err = fmt("post %u: langIndex pekar på '%s' men posten ligger "
                      "utanför språkets svit", i, l.tag.c_str());
            return false;
        }
        if (!readStr(strBase, stringBytes, textOff, textLen, e.text)) {
            err = fmt("post %u: texten ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        entries.push_back(std::move(e));
    }

    // Stigande och unika id INOM sitt språk — uppslaget binärsöker.
    for (const LangView& l : langs)
        for (std::uint32_t i = l.firstEntry + 1;
             i < l.firstEntry + l.entryCount; ++i)
            if (entries[i].id <= entries[i - 1].id) {
                err = "språket '" + l.tag + "': id " +
                      std::to_string(entries[i].id) +
                      " står inte stigande efter " +
                      std::to_string(entries[i - 1].id);
                return false;
            }

    bytes_       = std::move(bytes);
    langs_       = std::move(langs);
    entries_     = std::move(entries);
    verMajor_    = major;
    verMinor_    = minor;
    defaultLang_ = defaultLang;
    flags_       = flags;
    return true;
}

int File::findLang(const std::string& tag) const
{
    const std::string n = normaliseraTagg(tag);
    for (std::size_t i = 0; i < langs_.size(); ++i)
        if (langs_[i].tag == n)
            return int(i);
    return -1;
}

const EntryView* File::find(int langIndex, std::uint32_t id) const
{
    if (langIndex < 0 || std::size_t(langIndex) >= langs_.size())
        return nullptr;
    const LangView& l = langs_[std::size_t(langIndex)];
    std::uint32_t   lo = l.firstEntry;
    std::uint32_t   hi = l.firstEntry + l.entryCount;   // exklusiv
    while (lo < hi) {
        const std::uint32_t mid = lo + (hi - lo) / 2;
        if (entries_[mid].id == id)
            return &entries_[mid];
        if (entries_[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

std::vector<std::uint32_t> File::allIds() const
{
    std::set<std::uint32_t> s;
    for (const EntryView& e : entries_)
        s.insert(e.id);
    return std::vector<std::uint32_t>(s.begin(), s.end());
}

// ------------------------------------------------------- scope-kontrollen

bool requireAppScope(const File& f, std::string& err)
{
    err.clear();
    if (f.flags() != 0) {
        err = "appfil förväntad men flaggorna är 0x" +
              fmt("%04x", f.flags());
        return false;
    }
    for (const EntryView& e : f.entries())
        if (e.id < kAppIdFloor) {
            err = "appfil med id " + std::to_string(e.id) + " under golvet " +
                  std::to_string(kAppIdFloor);
            return false;
        }
    return true;
}

bool requireFrameworkScope(const File& f, std::string& err)
{
    err.clear();
    if (f.flags() != kFlagFramework) {
        err = "ramverksfil förväntad men flaggorna är " +
              fmt("0x%04x", f.flags());
        return false;
    }
    for (const EntryView& e : f.entries())
        if (e.id >= kAppIdFloor) {
            err = "ramverksfil med id " + std::to_string(e.id) +
                  " på eller över golvet " + std::to_string(kAppIdFloor);
            return false;
        }
    return true;
}

// -------------------------------------------------------------- packaren

bool packBytes(const std::vector<PackLang>&  langs,
               const std::vector<PackEntry>& entries,
               const std::string& defaultLangTag, bool framework,
               std::vector<std::uint8_t>& out, std::string& err)
{
    err.clear();
    if (langs.empty()) {
        err = "inga språk att packa";
        return false;
    }
    // Indata-validering — samma krav som läsaren ställer, fast framåt.
    std::vector<std::string> taggar;
    std::set<std::string>    sedda;
    for (const PackLang& l : langs) {
        const std::string t = normaliseraTagg(l.tag);
        if (!giltigTagg(t)) {
            err = "ogiltig språktagg '" + l.tag + "'";
            return false;
        }
        if (l.name.empty()) {
            err = "språket '" + t + "' saknar självbenämning";
            return false;
        }
        if (!sedda.insert(t).second) {
            err = "dubblerad språktagg '" + t + "'";
            return false;
        }
        taggar.push_back(t);
    }

    std::uint16_t defaultIndex = 0;
    if (!defaultLangTag.empty()) {
        const std::string d = normaliseraTagg(defaultLangTag);
        const auto it = std::find(taggar.begin(), taggar.end(), d);
        if (it == taggar.end()) {
            err = "defaultspråket '" + defaultLangTag + "' finns inte i filen";
            return false;
        }
        defaultIndex = std::uint16_t(it - taggar.begin());
    }

    // Gruppera per språk i DEKLARATIONSORDNING och sortera stigande på id.
    std::vector<std::vector<const PackEntry*>> perLang(langs.size());
    for (const PackEntry& e : entries) {
        if (e.id == 0) {
            err = "post med id 0 (reserverat för \"ingen översättning\")";
            return false;
        }
        const std::string t  = normaliseraTagg(e.lang);
        const auto        it = std::find(taggar.begin(), taggar.end(), t);
        if (it == taggar.end()) {
            err = "posten " + std::to_string(e.id) +
                  " pekar på okänt språk '" + e.lang + "'";
            return false;
        }
        perLang[std::size_t(it - taggar.begin())].push_back(&e);
    }
    std::vector<const PackEntry*> ordnade;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sviter;
    for (std::size_t li = 0; li < langs.size(); ++li) {
        std::vector<const PackEntry*>& v = perLang[li];
        std::stable_sort(v.begin(), v.end(),
                         [](const PackEntry* a, const PackEntry* b) {
                             return a->id < b->id;
                         });
        for (std::size_t k = 1; k < v.size(); ++k)
            if (v[k]->id == v[k - 1]->id) {
                err = "dubblerat id " + std::to_string(v[k]->id) +
                      " i språket '" + taggar[li] + "'";
                return false;
            }
        sviter.push_back({std::uint32_t(ordnade.size()),
                          std::uint32_t(v.size())});
        ordnade.insert(ordnade.end(), v.begin(), v.end());
    }

    // Strängtabellen — dedupliceras (samma text lagras en gång).
    std::vector<std::uint8_t>            pool;
    std::map<std::string, std::uint32_t> poolOff;
    auto intern = [&](const std::string& s) -> std::uint32_t {
        auto it = poolOff.find(s);
        if (it != poolOff.end())
            return it->second;
        const std::uint32_t off = std::uint32_t(pool.size());
        pool.insert(pool.end(), s.begin(), s.end());
        pool.push_back(0);
        poolOff[s] = off;
        return off;
    };
    for (std::size_t li = 0; li < langs.size(); ++li) {
        intern(taggar[li]);
        intern(langs[li].name);
    }
    for (const PackEntry* e : ordnade)
        intern(e->text);

    const std::uint32_t nLangs   = std::uint32_t(langs.size());
    const std::uint32_t nEntries = std::uint32_t(ordnade.size());
    const std::uint32_t langOffset   = kHeaderSize;
    const std::uint32_t entryOffset  = langOffset + nLangs * kLangRecSize;
    const std::uint32_t stringOffset = entryOffset + nEntries * kEntryRecSize;
    const std::uint32_t stringBytes  = std::uint32_t(pool.size());
    const std::uint32_t fileSize     = stringOffset + stringBytes;

    std::vector<std::uint8_t> bin;
    bin.reserve(fileSize);
    bin.insert(bin.end(), kMagic, kMagic + 8);
    wr16(bin, kVersionMajor);
    wr16(bin, kVersionMinor);
    wr32(bin, kHeaderSize);
    wr32(bin, nLangs);
    wr32(bin, langOffset);
    wr32(bin, kLangRecSize);
    wr32(bin, nEntries);
    wr32(bin, entryOffset);
    wr32(bin, kEntryRecSize);
    wr32(bin, stringOffset);
    wr32(bin, stringBytes);
    wr32(bin, 0);                       // fileCrc32 — fylls i sist
    wr32(bin, kAppIdFloor);
    wr16(bin, defaultIndex);
    wr16(bin, framework ? kFlagFramework : std::uint16_t(0));
    wr32(bin, fileSize);

    for (std::size_t li = 0; li < langs.size(); ++li) {
        wr32(bin, poolOff[taggar[li]]);
        wr32(bin, std::uint32_t(taggar[li].size()));
        wr32(bin, poolOff[langs[li].name]);
        wr32(bin, std::uint32_t(langs[li].name.size()));
        wr32(bin, sviter[li].first);
        wr32(bin, sviter[li].second);
    }
    for (std::size_t li = 0; li < langs.size(); ++li)
        for (std::uint32_t k = 0; k < sviter[li].second; ++k) {
            const PackEntry* e = ordnade[sviter[li].first + k];
            wr32(bin, e->id);
            wr32(bin, std::uint32_t(li));
            wr32(bin, poolOff[e->text]);
            wr32(bin, std::uint32_t(e->text.size()));
        }
    bin.insert(bin.end(), pool.begin(), pool.end());

    if (bin.size() != fileSize) {
        err = "internt fel: monterad storlek avviker från fileSize";
        return false;
    }

    // CRC:n räknas med sitt eget fält som NOLL — det är redan noll här,
    // men samma funktion används av läsaren, så de kan inte glida isär.
    const std::uint32_t crc =
        crc32MedHal(bin.data(), bin.size(), kCrcOffset);
    bin[kCrcOffset + 0] = std::uint8_t(crc & 0xFF);
    bin[kCrcOffset + 1] = std::uint8_t((crc >> 8) & 0xFF);
    bin[kCrcOffset + 2] = std::uint8_t((crc >> 16) & 0xFF);
    bin[kCrcOffset + 3] = std::uint8_t(crc >> 24);

    out = std::move(bin);
    return true;
}

bool writeAtomic(const std::string& path,
                 const std::vector<std::uint8_t>& bytes, std::string& err)
{
    namespace fs = std::filesystem;
    const fs::path dst = fs::path(path);
    const fs::path dir =
        dst.has_parent_path() ? dst.parent_path() : fs::path(".");

    // PRAKTISKT unikt, inte formellt kollisionsfritt — se headern.
    std::random_device       rd;
    static unsigned          raknare = 0;
    fs::path                 tmp;
    bool                     hittad = false;
    for (int forsok = 0; forsok < 64 && !hittad; ++forsok) {
        char namn[64];
        std::snprintf(namn, sizeof namn, ".glzlang-%08x-%08x.tmp",
                      unsigned(rd()), ++raknare);
        tmp = dir / namn;
        std::error_code ec;
        if (!fs::exists(tmp, ec))
            hittad = true;
    }
    if (!hittad) {
        err = "hittade inget ledigt tempnamn i " + dir.string();
        return false;
    }

    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            err = "kan inte skriva " + tmp.string();
            return false;
        }
        o.write(reinterpret_cast<const char*>(bytes.data()),
                std::streamsize(bytes.size()));
        o.flush();
        const bool bra = o.good();
        o.close();
        if (!bra || o.fail()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            err = "skrivfel i " + tmp.string();
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        err = "kan inte ersätta " + path + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

bool pack(const std::vector<PackLang>&  langs,
          const std::vector<PackEntry>& entries,
          const std::string& defaultLangTag, bool framework,
          const std::string& outPath, std::string& err)
{
    std::vector<std::uint8_t> bin;
    if (!packBytes(langs, entries, defaultLangTag, framework, bin, err))
        return false;
    return writeAtomic(outPath, bin, err);
}

// -------------------------------------------------------------- manifestet

namespace {

// C++-strängliteralen, STRIKT delmängd: \" \\ \n \t \r och inget annat.
// EGEN implementation — libben får inte bero på builder_core.
bool parseLiteral(const std::string& v, std::string& ut)
{
    if (v.size() < 2 || v.front() != '"' || v.back() != '"')
        return false;
    std::string s;
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
        const char c = v[i];
        if (c == '"')
            return false;              // oescapat citat inuti
        if (static_cast<unsigned char>(c) < 0x20)
            return false;              // rå kontrollbyte
        if (c != '\\') {
            s += c;
            continue;
        }
        if (i + 2 >= v.size())
            return false;              // avslutande backslash
        const char n = v[++i];
        switch (n) {
        case '"':  s += '"';  break;
        case '\\': s += '\\'; break;
        case 'n':  s += '\n'; break;
        case 't':  s += '\t'; break;
        case 'r':  s += '\r'; break;
        default:   return false;       // okänd escape ⇒ AVVISA, aldrig gissa
        }
    }
    ut = std::move(s);
    return true;
}

std::string literalText(const std::string& raw)
{
    std::string ut = "\"";
    for (char c : raw) {
        switch (c) {
        case '"':  ut += "\\\""; break;
        case '\\': ut += "\\\\"; break;
        case '\n': ut += "\\n";  break;
        case '\t': ut += "\\t";  break;
        case '\r': ut += "\\r";  break;
        default:   ut += c;      break;
        }
    }
    ut += '"';
    return ut;
}

bool giltigtIdNamn(const std::string& s)
{
    if (s.empty())
        return false;
    const char f = s[0];
    if (!((f >= 'A' && f <= 'Z') || (f >= 'a' && f <= 'z') || f == '_'))
        return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

std::string trimma(const std::string& s)
{
    const std::size_t a = s.find_first_not_of(" \t\r");
    const std::size_t b = s.find_last_not_of(" \t\r");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

} // namespace

bool parseManifest(const std::string& text, Manifest& out, std::string& err)
{
    err.clear();
    out = Manifest{};
    std::istringstream in(text);
    std::string        rad;
    int                radNr    = 0;
    std::string        aktivTag;
    std::map<std::uint32_t, std::string> namn;

    while (std::getline(in, rad)) {
        ++radNr;
        // '#' inuti en strängliteral är TEXT och inte en kommentar —
        // därför söks kommentarstecknet utanför citat.
        bool iStr = false;
        for (std::size_t i = 0; i < rad.size(); ++i) {
            if (iStr && rad[i] == '\\') { ++i; continue; }
            if (rad[i] == '"')
                iStr = !iStr;
            else if (rad[i] == '#' && !iStr) {
                rad.erase(i);
                break;
            }
        }
        rad = trimma(rad);
        if (rad.empty())
            continue;

        std::istringstream ss(rad);
        std::string        cmd;
        ss >> cmd;

        if (cmd == "default") {
            std::string t;
            if (!(ss >> t)) {
                err = "rad " + std::to_string(radNr) +
                      ": väntade 'default <tagg>'";
                return false;
            }
            out.defaultLangTag = t;
        } else if (cmd == "lang") {
            PackLang l;
            if (!(ss >> l.tag)) {
                err = "rad " + std::to_string(radNr) +
                      ": väntade 'lang <tagg> <självbenämning>'";
                return false;
            }
            std::string rest;
            std::getline(ss, rest);
            l.name = trimma(rest);
            if (l.name.empty()) {
                err = "rad " + std::to_string(radNr) +
                      ": språket saknar självbenämning";
                return false;
            }
            aktivTag = l.tag;
            out.langs.push_back(std::move(l));
        } else if (cmd == "id") {
            if (aktivTag.empty()) {
                err = "rad " + std::to_string(radNr) + ": 'id' före första 'lang'";
                return false;
            }
            unsigned long long tal = 0;
            if (!(ss >> tal) || tal == 0 || tal > 0xFFFFFFFFull) {
                err = "rad " + std::to_string(radNr) +
                      ": väntade 'id <tal> [<NAMN>] \"text\"' med tal > 0";
                return false;
            }
            std::string rest;
            std::getline(ss, rest);
            rest = trimma(rest);
            if (rest.empty()) {
                err = "rad " + std::to_string(radNr) + ": posten saknar text";
                return false;
            }
            if (rest[0] != '"') {
                // Mittentoken är KONSTANTNAMNET — texten börjar alltid
                // med citattecken, så de två kan inte förväxlas.
                const std::size_t mel = rest.find_first_of(" \t");
                if (mel == std::string::npos) {
                    err = "rad " + std::to_string(radNr) +
                          ": posten saknar text efter namnet";
                    return false;
                }
                const std::string n = rest.substr(0, mel);
                if (!giltigtIdNamn(n)) {
                    err = "rad " + std::to_string(radNr) +
                          ": '" + n + "' är ingen C++-identifierare";
                    return false;
                }
                auto it = namn.find(std::uint32_t(tal));
                if (it != namn.end() && it->second != n) {
                    err = "rad " + std::to_string(radNr) + ": id " +
                          std::to_string(tal) + " har redan namnet '" +
                          it->second + "'";
                    return false;
                }
                namn[std::uint32_t(tal)] = n;
                rest = trimma(rest.substr(mel + 1));
            }
            PackEntry e;
            e.lang = aktivTag;
            e.id   = std::uint32_t(tal);
            if (!parseLiteral(rest, e.text)) {
                err = "rad " + std::to_string(radNr) +
                      ": texten är ingen giltig strängliteral (\\\" \\\\ \\n "
                      "\\t \\r är de enda escaperna)";
                return false;
            }
            out.entries.push_back(std::move(e));
        } else {
            err = "rad " + std::to_string(radNr) + ": okänt kommando '" +
                  cmd + "'";
            return false;
        }
    }
    for (const auto& kv : namn)
        out.names.push_back(IdName{kv.first, kv.second});
    return true;
}

std::string exportManifest(const File& f)
{
    std::string ut;
    ut += "# language.bin exporterat av glazelanguage\n";
    if (!f.langs().empty())
        ut += "default " + f.langs()[f.defaultLang()].tag + "\n";
    for (const LangView& l : f.langs()) {
        ut += "\nlang " + l.tag + " " + l.name + "\n";
        for (std::uint32_t i = l.firstEntry; i < l.firstEntry + l.entryCount;
             ++i) {
            const EntryView& e = f.entries()[i];
            ut += "id " + std::to_string(e.id) + " " + literalText(e.text) +
                  "\n";
        }
    }
    return ut;
}

// ------------------------------------------------------------------- dump

std::string dump(const File& f)
{
    const std::vector<std::uint32_t> ids = f.allIds();
    std::string s = fmt("language.bin v%u.%u · %zu språk · %zu poster · "
                        "%zu unika id · %s\n",
                        f.versionMajor(), f.versionMinor(), f.langs().size(),
                        f.entries().size(), ids.size(),
                        f.isFramework() ? "RAMVERK" : "app");
    if (!f.langs().empty())
        s += "  fallback i filen: " + f.langs()[f.defaultLang()].tag + "\n";
    for (std::size_t li = 0; li < f.langs().size(); ++li) {
        const LangView& l = f.langs()[li];
        std::vector<std::uint32_t> saknade;
        for (std::uint32_t id : ids)
            if (!f.find(int(li), id))
                saknade.push_back(id);
        s += fmt("  %-8s %-16s %4zu/%-4zu%s\n", l.tag.c_str(), l.name.c_str(),
                 ids.size() - saknade.size(), ids.size(),
                 saknade.empty() ? "  KOMPLETT" : "");
        if (!saknade.empty()) {
            std::string rad = "      saknar:";
            for (std::size_t k = 0; k < saknade.size() && k < 20; ++k)
                rad += " " + std::to_string(saknade[k]);
            if (saknade.size() > 20)
                rad += " … (" + std::to_string(saknade.size()) + " st)";
            s += rad + "\n";
        }
    }
    return s;
}

std::string headerText(const Manifest& m, const std::string& ns,
                       const std::string& refLangTag)
{
    // Referensspråkets texter som kommentar — headern ska gå att läsa
    // utan att öppna manifestet.
    std::map<std::uint32_t, std::string> ref;
    const std::string refTag = normaliseraTagg(
        refLangTag.empty()
            ? (m.langs.empty() ? std::string() : m.langs.front().tag)
            : refLangTag);
    for (const PackEntry& e : m.entries)
        if (normaliseraTagg(e.lang) == refTag)
            ref[e.id] = e.text;

    std::string s;
    s += "#pragma once\n";
    s += "#include <cstdint>\n\n";
    s += "// GENERERAD av `glazelanguage pack --header` — committas (regel 7)\n";
    s += "// och kompileras som vanlig källkod. Bygget genererar ingenting.\n";
    s += "// Att skriva raderna för hand är lika lagligt; formen är kontraktet.\n\n";
    s += "namespace " + ns + " {\n\n";
    for (const IdName& n : m.names) {
        auto it = ref.find(n.id);
        if (it != ref.end())
            s += "// " + literalText(it->second) + "\n";
        s += "inline constexpr std::uint32_t " + n.name + " = " +
             std::to_string(n.id) + ";\n";
    }
    s += "\n} // namespace " + ns + "\n";
    return s;
}

} // namespace glzlang
