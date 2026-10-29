#pragma once
#include <cstdint>
#include <string>
#include <vector>

// glaze_assets — assets.bin-formatet. Containern ÄR källan
// för appens assets (som .resx i .NET): den committas, käll-PNG:erna gör
// det inte — därför är exporten ett HÅRT krav och garanterad per
// konstruktion (posterna bär den oförändrade källfilen + origName).
//
// REN STD — inga beroenden, ingen koppling till glaze/gfx. Libben ska
// kunna kompileras helt fristående (dump/verify-CLI:t, GlazeBuilder,
// headless-tester), precis som cpp_reader/cpp_writer.
//
// Layout (little-endian, absoluta offsets ur headern; detaljerna i
// assets_file.cpp):
//   Header(64) → Listtabell → Entrytabell → Strängtabell → Blob
// Keys är unika PER LISTA — referensen är paret (lista, key). Listan bär
// storleksklassen (logiska px). Versionsregeln är hård regel 4:s anda:
// fel magic/major ⇒ VÄGRA; nyare minor ⇒ acceptera och hoppa okänt på
// DEKLARERAD längd (headerSize/listRecSize/entryRecSize) — aldrig gissa.
// Varje laddning kör HELA valideringen (offsets, bakpekare, NUL-term,
// dubbletter, CRC32 över blobben) — minsta fel ⇒ ladda ingenting.

namespace glzassets {

constexpr std::uint16_t kVersionMajor = 1;
constexpr std::uint16_t kVersionMinor = 0;

// typeTag-värden. Audio är RESERVERAD — ingen ljudkod finns (bara plats i
// formatet).
constexpr std::uint16_t kTypeUnknown = 0;
constexpr std::uint16_t kTypeImage   = 1;
constexpr std::uint16_t kTypeAudio   = 2;

// density-värden: 100 = @1x, 200 = @2x, 0 = ospecificerad. PLATS ENDAST —
// ingen uppslagslogik väljer på density ännu.
constexpr std::uint16_t kDensity1x = 100;
constexpr std::uint16_t kDensity2x = 200;

struct ListView {
    std::string   name;         // unik i filen
    std::uint32_t firstEntry = 0;
    std::uint32_t entryCount = 0;
    std::uint16_t sizeClass  = 0;   // LOGISKA px; 0 = ospecificerad
};

struct EntryView {
    std::string   key;          // unik inom sin lista
    std::string   origName;     // källfilens namn — bär exporten
    std::uint32_t listIndex  = 0;
    std::uint32_t dataOffset = 0;   // absolut filoffset
    std::uint32_t dataSize   = 0;
    std::uint16_t typeTag    = kTypeUnknown;
    std::uint16_t density    = 0;
};

// Läsaren. open* validerar ALLT — vid minsta fel lämnas objektet tomt,
// err får en distinkt orsak (svenska) och false returneras.
class File {
public:
    bool openFile(const std::string& path, std::string& err);
    bool openBytes(std::vector<std::uint8_t> bytes, std::string& err);

    const std::vector<ListView>&  lists()   const { return lists_; }
    const std::vector<EntryView>& entries() const { return entries_; }

    // Pekare in i filens bytes — giltig så länge File lever.
    const std::uint8_t* data(const EntryView& e) const
    {
        return bytes_.data() + e.dataOffset;
    }

    // Uppslag på (lista, key) — nullptr om något av leden saknas.
    const EntryView* find(const std::string& list,
                          const std::string& key) const;

    std::uint16_t versionMajor() const { return verMajor_; }
    std::uint16_t versionMinor() const { return verMinor_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<ListView>     lists_;
    std::vector<EntryView>    entries_;
    std::uint16_t             verMajor_ = 0;
    std::uint16_t             verMinor_ = 0;
};

// Packaren. Posternas bytes ges i minnet (CLI:t läser filerna, Glaze-
// Builder har dem redan) — pack rör aldrig källfiler själv. Skriver
// ATOMÄRT (temp + rename): assets.bin är committad källkod, en avbruten
// skrivning får aldrig lämna en halv fil. Identiskt innehåll dedupliceras
// i blobben. Fel (okänd lista, dubblett-key, tomma namn) ⇒ false + err.
struct PackList {
    std::string   name;
    std::uint16_t sizeClass = 0;
};

struct PackEntry {
    std::string               list;
    std::string               key;
    std::string               origName;   // bara filnamn, inga sökvägsled
    std::vector<std::uint8_t> bytes;      // källfilen oförändrad
    std::uint16_t             typeTag = kTypeImage;
    std::uint16_t             density = 0;
};

bool pack(const std::vector<PackList>&  lists,
          const std::vector<PackEntry>& entries,
          const std::string& outPath, std::string& err);

// Exporterar originalet: skriver dataSize byte till <dir>/<origName>.
// Byte-identisk round-trip per konstruktion. Vägrar origName med
// sökvägsled (en fientlig fil får inte skriva utanför dir).
bool extract(const File& f, const EntryView& e,
             const std::string& dir, std::string& err);

// Människoläsbar rapport (listor, poster, typ, density, storlek) — det
// egna formatets motsvarighet till sqlite3-CLI:t.
std::string dump(const File& f);

} // namespace glzassets
