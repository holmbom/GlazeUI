// assets.bin-formatet — läsare, packare, export, dump. REN STD.
//
// Layout (allt little-endian; läses/skrivs BYTE FÖR BYTE via rd*/wr* —
// aldrig memcpy av packad struct: noll alignment-UB, noll #pragma pack,
// en framtida big-endian-port är en enda funktion):
//
//   Header — 64 byte
//     0  magic          "GLZASSET" (8, ASCII, ingen NUL)
//     8  versionMajor   u16   != vår ⇒ VÄGRA (inkompatibel layout)
//    10  versionMinor   u16   > vår ⇒ acceptera, hoppa okänt PÅ LÄNGD
//    12  headerSize     u32   64 — okända efterföljande bytes hoppas över
//    16  listCount      u32
//    20  listOffset     u32   absolut
//    24  listRecSize    u32   32 — poster hoppas över på längd
//    28  entryCount     u32   totalt över alla listor
//    32  entryOffset    u32   absolut
//    36  entryRecSize   u32   40
//    40  stringOffset   u32   absolut; UTF-8, NUL-terminerade
//    44  stringBytes    u32
//    48  blobOffset     u32   absolut, 4-byte-alignad
//    52  blobBytes      u32
//    56  blobCrc32      u32   CRC32 över hela blobregionen
//    60  fileSize       u32   hela filens längd
//
//   Listpost — 32 byte: nameOffset/nameLen u32 (RELATIVT strängtabellen),
//     firstEntry/entryCount u32, sizeClass u16 (LOGISKA px, 0 = ospec),
//     flags u16 (reserverad), 12 byte reserverat (0).
//
//   Entrypost — 40 byte: keyOffset/keyLen u32, listIndex u32 (redundant ⇒
//     VERIFIERBAR bakpekare), typeTag u16, density u16, dataOffset/
//     dataSize u32 (dataOffset ABSOLUT), origNameOffset/origNameLen u32,
//     8 byte reserverat (0).
//
//   Blob: nyttolasterna back-to-back, 4-byte-alignade med nollpadding
//     (paddingen ingår inte i dataSize). Bytesen är den OFÖRÄNDRADE
//     källfilen — ingen transkodning, ingen komprimering (PNG är redan
//     komprimerad). Identiskt innehåll får dela (dataOffset, dataSize).

#include "glaze_assets/assets_file.h"

#include "crc32.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace glzassets {
namespace {

constexpr std::uint32_t kHeaderSize   = 64;
constexpr std::uint32_t kListRecSize  = 32;
constexpr std::uint32_t kEntryRecSize = 40;
constexpr char          kMagic[8]     = {'G','L','Z','A','S','S','E','T'};

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

// CRC32 bor i den delade interna headern (crc32.h) — PNG-codecen räknar
// samma summa över varje chunk.
using detail::crc32;

std::uint32_t align4(std::uint32_t x) { return (x + 3u) & ~3u; }

// format-attributet ger dels kompileringstidskontroll av anropen, dels
// vet clang att f ÄR en formatsträngsparameter (tystar -Wformat-nonliteral
// i vidarebefordran till vsnprintf).
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
std::string fmt(const char* f, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// Strängkontroll: (off, len) relativt strängtabellen — inom regionen och
// NUL-terminerad. Fyller ut strängen vid ok.
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

} // namespace

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
    // Vid minsta fel: lämna objektet tomt — ladda ALLT eller INGET.
    bytes_.clear();
    lists_.clear();
    entries_.clear();
    verMajor_ = verMinor_ = 0;

    const std::uint64_t size = bytes.size();
    const std::uint8_t* p    = bytes.data();

    if (size < kHeaderSize) {
        err = fmt("trunkerad: %llu byte, headern kräver %u",
                  (unsigned long long)size, kHeaderSize);
        return false;
    }
    if (std::memcmp(p, kMagic, 8) != 0) {
        err = "fel magic — inte en assets.bin";
        return false;
    }

    const std::uint16_t major = rd16(p + 8);
    const std::uint16_t minor = rd16(p + 10);
    if (major != kVersionMajor) {
        err = fmt("versionMajor %u stöds inte (denna läsare: %u)",
                  major, kVersionMajor);
        return false;
    }
    // Nyare minor accepteras — allt okänt hoppas över PÅ DEKLARERAD LÄNGD
    // nedan (headerSize/listRecSize/entryRecSize), aldrig gissning.

    const std::uint32_t headerSize   = rd32(p + 12);
    const std::uint32_t listCount    = rd32(p + 16);
    const std::uint32_t listOffset   = rd32(p + 20);
    const std::uint32_t listRecSize  = rd32(p + 24);
    const std::uint32_t entryCount   = rd32(p + 28);
    const std::uint32_t entryOffset  = rd32(p + 32);
    const std::uint32_t entryRecSize = rd32(p + 36);
    const std::uint32_t stringOffset = rd32(p + 40);
    const std::uint32_t stringBytes  = rd32(p + 44);
    const std::uint32_t blobOffset   = rd32(p + 48);
    const std::uint32_t blobBytes    = rd32(p + 52);
    const std::uint32_t blobCrc      = rd32(p + 56);
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
    if (listRecSize < kListRecSize) {
        err = fmt("ogiltig listRecSize %u (min %u)", listRecSize, kListRecSize);
        return false;
    }
    if (entryRecSize < kEntryRecSize) {
        err = fmt("ogiltig entryRecSize %u (min %u)",
                  entryRecSize, kEntryRecSize);
        return false;
    }

    // Sektionsgränserna — all offsetmatte i 64 bitar (inga överslag).
    if (std::uint64_t(headerSize) > size ||
        std::uint64_t(listOffset) + std::uint64_t(listCount) * listRecSize > size) {
        err = "listtabellen ligger utanför filen";
        return false;
    }
    if (std::uint64_t(entryOffset) + std::uint64_t(entryCount) * entryRecSize > size) {
        err = "entrytabellen ligger utanför filen";
        return false;
    }
    if (std::uint64_t(stringOffset) + stringBytes > size) {
        err = "strängtabellen ligger utanför filen";
        return false;
    }
    if (std::uint64_t(blobOffset) + blobBytes > size) {
        err = "blobben ligger utanför filen";
        return false;
    }

    const std::uint8_t* strBase = p + stringOffset;

    // Listtabellen. Namnen måste vara unika — glaze::img("lista","key")
    // slår upp på namn, en dubblett vore tyst tvetydig.
    std::vector<ListView> lists;
    std::set<std::string> listNames;
    lists.reserve(listCount);
    for (std::uint32_t i = 0; i < listCount; ++i) {
        const std::uint8_t* rec = p + listOffset + std::uint64_t(i) * listRecSize;
        ListView l;
        const std::uint32_t nameOff = rd32(rec + 0);
        const std::uint32_t nameLen = rd32(rec + 4);
        l.firstEntry = rd32(rec + 8);
        l.entryCount = rd32(rec + 12);
        l.sizeClass  = rd16(rec + 16);
        if (!readStr(strBase, stringBytes, nameOff, nameLen, l.name)) {
            err = fmt("lista %u: namnet ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        if (std::uint64_t(l.firstEntry) + l.entryCount > entryCount) {
            err = fmt("lista %u ('%s'): firstEntry+entryCount utanför "
                      "entrytabellen", i, l.name.c_str());
            return false;
        }
        if (!listNames.insert(l.name).second) {
            err = "dubblerat listnamn '" + l.name + "'";
            return false;
        }
        lists.push_back(std::move(l));
    }

    // Entrytabellen. listIndex är redundant mot listornas intervall —
    // det är AVSIKTLIGT: bakpekaren gör tabellen verifierbar.
    std::vector<EntryView> entries;
    std::set<std::pair<std::uint32_t, std::string>> keys;
    entries.reserve(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint8_t* rec = p + entryOffset + std::uint64_t(i) * entryRecSize;
        EntryView e;
        const std::uint32_t keyOff  = rd32(rec + 0);
        const std::uint32_t keyLen  = rd32(rec + 4);
        e.listIndex  = rd32(rec + 8);
        e.typeTag    = rd16(rec + 12);
        e.density    = rd16(rec + 14);
        e.dataOffset = rd32(rec + 16);
        e.dataSize   = rd32(rec + 20);
        const std::uint32_t nameOff = rd32(rec + 24);
        const std::uint32_t nameLen = rd32(rec + 28);
        if (!readStr(strBase, stringBytes, keyOff, keyLen, e.key)) {
            err = fmt("post %u: nyckeln ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        if (!readStr(strBase, stringBytes, nameOff, nameLen, e.origName)) {
            err = fmt("post %u: origName ligger utanför strängtabellen "
                      "eller saknar NUL", i);
            return false;
        }
        if (e.listIndex >= listCount) {
            err = fmt("post %u ('%s'): listIndex %u utanför listtabellen",
                      i, e.key.c_str(), e.listIndex);
            return false;
        }
        const ListView& l = lists[e.listIndex];
        if (i < l.firstEntry || i >= l.firstEntry + l.entryCount) {
            err = fmt("post %u ('%s'): listIndex pekar på '%s' men posten "
                      "ligger utanför listans intervall",
                      i, e.key.c_str(), l.name.c_str());
            return false;
        }
        if (std::uint64_t(e.dataOffset) < blobOffset ||
            std::uint64_t(e.dataOffset) + e.dataSize >
                std::uint64_t(blobOffset) + blobBytes) {
            err = fmt("post %u ('%s'): data utanför blobben", i, e.key.c_str());
            return false;
        }
        if (!keys.insert({e.listIndex, e.key}).second) {
            err = "dubblettnyckel '" + l.name + "/" + e.key + "'";
            return false;
        }
        entries.push_back(std::move(e));
    }

    // CRC sist — dyrast, och strukturfel ska rapporteras framför CRC-fel.
    const std::uint32_t calc = crc32(p + blobOffset, blobBytes);
    if (calc != blobCrc) {
        err = fmt("CRC-fel i blobben (fil: %08x, beräknad: %08x)",
                  blobCrc, calc);
        return false;
    }

    bytes_    = std::move(bytes);
    lists_    = std::move(lists);
    entries_  = std::move(entries);
    verMajor_ = major;
    verMinor_ = minor;
    return true;
}

const EntryView* File::find(const std::string& list,
                            const std::string& key) const
{
    for (std::uint32_t li = 0; li < lists_.size(); ++li) {
        if (lists_[li].name != list)
            continue;
        const ListView& l = lists_[li];
        for (std::uint32_t i = l.firstEntry; i < l.firstEntry + l.entryCount; ++i)
            if (entries_[i].key == key)
                return &entries_[i];
        return nullptr;   // listan fanns, nyckeln inte
    }
    return nullptr;
}

// ---------------------------------------------------------------- packaren

bool pack(const std::vector<PackList>&  lists,
          const std::vector<PackEntry>& entries,
          const std::string& outPath, std::string& err)
{
    // Indata-validering — samma krav som läsaren ställer, fast framåt.
    std::set<std::string> listNames;
    for (const PackList& l : lists) {
        if (l.name.empty()) {
            err = "lista utan namn";
            return false;
        }
        if (!listNames.insert(l.name).second) {
            err = "dubblerat listnamn '" + l.name + "'";
            return false;
        }
    }
    std::set<std::pair<std::string, std::string>> keys;
    for (const PackEntry& e : entries) {
        if (e.key.empty()) {
            err = "post utan nyckel (lista '" + e.list + "')";
            return false;
        }
        if (!listNames.count(e.list)) {
            err = "posten '" + e.key + "' pekar på okänd lista '" + e.list + "'";
            return false;
        }
        // Exporten skriver till <dir>/<origName> — namnet måste finnas
        // och får inte bära sökvägsled.
        if (e.origName.empty()) {
            err = "posten '" + e.list + "/" + e.key + "' saknar origName";
            return false;
        }
        if (e.origName.find('/') != std::string::npos ||
            e.origName.find('\\') != std::string::npos ||
            e.origName.find("..") != std::string::npos) {
            err = "posten '" + e.list + "/" + e.key +
                  "' har sökvägsled i origName ('" + e.origName + "')";
            return false;
        }
        if (!keys.insert({e.list, e.key}).second) {
            err = "dubblettnyckel '" + e.list + "/" + e.key + "'";
            return false;
        }
    }

    // Gruppera posterna per lista (firstEntry/entryCount är intervall) —
    // relativ ordning inom listan bevaras.
    std::vector<const PackEntry*> ordered;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;   // first,count
    ordered.reserve(entries.size());
    for (const PackList& l : lists) {
        const std::uint32_t first = std::uint32_t(ordered.size());
        for (const PackEntry& e : entries)
            if (e.list == l.name)
                ordered.push_back(&e);
        ranges.push_back({first, std::uint32_t(ordered.size()) - first});
    }

    // Strängtabellen — dedupliceras (samma origName/nyckel lagras en gång).
    std::vector<std::uint8_t> pool;
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
    for (const PackList& l : lists)
        intern(l.name);
    for (const PackEntry* e : ordered) {
        intern(e->key);
        intern(e->origName);
    }

    // Sektionsoffsets — allt känt innan en enda byte skrivs.
    const std::uint32_t nLists  = std::uint32_t(lists.size());
    const std::uint32_t nEntries = std::uint32_t(ordered.size());
    const std::uint32_t listOffset   = kHeaderSize;
    const std::uint32_t entryOffset  = listOffset + nLists * kListRecSize;
    const std::uint32_t stringOffset = entryOffset + nEntries * kEntryRecSize;
    const std::uint32_t blobOffset   = align4(stringOffset +
                                              std::uint32_t(pool.size()));

    // Blobben — identiskt innehåll delar (dataOffset, dataSize). Linjär
    // jämförelse: posterna är tiotals, inte tusentals.
    std::vector<std::uint8_t> blob;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> written;  // rel,size
    std::vector<std::uint32_t> dataOff(nEntries);
    for (std::uint32_t i = 0; i < nEntries; ++i) {
        const std::vector<std::uint8_t>& b = ordered[i]->bytes;
        std::uint32_t rel = UINT32_MAX;
        for (const auto& w : written)
            if (w.second == b.size() &&
                std::memcmp(blob.data() + w.first, b.data(), b.size()) == 0) {
                rel = w.first;
                break;
            }
        if (rel == UINT32_MAX) {
            while (blob.size() % 4)   // 4-byte-align nästa nyttolast
                blob.push_back(0);
            rel = std::uint32_t(blob.size());
            blob.insert(blob.end(), b.begin(), b.end());
            written.push_back({rel, std::uint32_t(b.size())});
        }
        dataOff[i] = blobOffset + rel;
    }
    const std::uint32_t blobBytes = std::uint32_t(blob.size());
    const std::uint32_t blobCrc   = crc32(blob.data(), blob.size());
    const std::uint32_t fileSize  = blobOffset + blobBytes;

    // Montera filen i minnet — assets är små (ikoner).
    std::vector<std::uint8_t> out;
    out.reserve(fileSize);
    out.insert(out.end(), kMagic, kMagic + 8);
    wr16(out, kVersionMajor);
    wr16(out, kVersionMinor);
    wr32(out, kHeaderSize);
    wr32(out, nLists);
    wr32(out, listOffset);
    wr32(out, kListRecSize);
    wr32(out, nEntries);
    wr32(out, entryOffset);
    wr32(out, kEntryRecSize);
    wr32(out, stringOffset);
    wr32(out, std::uint32_t(pool.size()));
    wr32(out, blobOffset);
    wr32(out, blobBytes);
    wr32(out, blobCrc);
    wr32(out, fileSize);

    for (std::uint32_t i = 0; i < nLists; ++i) {
        const PackList& l = lists[i];
        wr32(out, poolOff[l.name]);
        wr32(out, std::uint32_t(l.name.size()));
        wr32(out, ranges[i].first);
        wr32(out, ranges[i].second);
        wr16(out, l.sizeClass);
        wr16(out, 0);                       // flags — reserverad
        for (int r = 0; r < 3; ++r)
            wr32(out, 0);                   // reserverat
    }
    // Listindex per post — grupperingen gör uppslaget trivialt.
    for (std::uint32_t i = 0; i < nEntries; ++i) {
        const PackEntry& e = *ordered[i];
        std::uint32_t li = 0;
        while (lists[li].name != e.list)
            ++li;
        wr32(out, poolOff[e.key]);
        wr32(out, std::uint32_t(e.key.size()));
        wr32(out, li);
        wr16(out, e.typeTag);
        wr16(out, e.density);
        wr32(out, dataOff[i]);
        wr32(out, std::uint32_t(e.bytes.size()));
        wr32(out, poolOff[e.origName]);
        wr32(out, std::uint32_t(e.origName.size()));
        wr32(out, 0);                       // reserverat
        wr32(out, 0);
    }
    out.insert(out.end(), pool.begin(), pool.end());
    while (out.size() < blobOffset)         // padda fram till blobben
        out.push_back(0);
    out.insert(out.end(), blob.begin(), blob.end());

    // Atomär skrivning: temp + rename. Filen är committad källkod — en
    // avbruten skrivning får aldrig lämna en halv sanning.
    const std::string tmp = outPath + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            err = "kan inte skriva " + tmp;
            return false;
        }
        o.write(reinterpret_cast<const char*>(out.data()),
                std::streamsize(out.size()));
        if (!o.good()) {
            err = "skrivfel i " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, outPath, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        err = "kan inte ersätta " + outPath;
        return false;
    }
    return true;
}

// ----------------------------------------------------------------- export

bool extract(const File& f, const EntryView& e,
             const std::string& dir, std::string& err)
{
    // Fientlig fil får inte skriva utanför dir — packaren vägrar redan
    // sökvägsled, men läsaren kan ha öppnat en främmande fil.
    if (e.origName.empty() ||
        e.origName.find('/') != std::string::npos ||
        e.origName.find('\\') != std::string::npos ||
        e.origName.find("..") != std::string::npos) {
        err = "ogiltigt exportnamn '" + e.origName + "'";
        return false;
    }
    const std::string path = dir + "/" + e.origName;
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) {
        err = "kan inte skriva " + path;
        return false;
    }
    o.write(reinterpret_cast<const char*>(f.data(e)),
            std::streamsize(e.dataSize));
    if (!o.good()) {
        err = "skrivfel i " + path;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------- dump

std::string dump(const File& f)
{
    const char* typeName[] = {"Unknown", "Image", "Audio"};
    std::string s = fmt("assets.bin v%u.%u · %zu listor · %zu poster\n",
                        f.versionMajor(), f.versionMinor(),
                        f.lists().size(), f.entries().size());
    for (const ListView& l : f.lists()) {
        s += fmt("  lista \"%s\" (%u px, %u poster)\n",
                 l.name.c_str(), l.sizeClass, l.entryCount);
        for (std::uint32_t i = l.firstEntry;
             i < l.firstEntry + l.entryCount; ++i) {
            const EntryView& e = f.entries()[i];
            const char* tn = e.typeTag < 3 ? typeName[e.typeTag] : "?";
            s += fmt("    %-16s %-8s d%-4u %8u byte  @%-8u → %s\n",
                     e.key.c_str(), tn, e.density, e.dataSize,
                     e.dataOffset, e.origName.c_str());
        }
    }
    return s;
}

} // namespace glzassets
