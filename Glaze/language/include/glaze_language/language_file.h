#pragma once
#include <cstdint>
#include <string>
#include <vector>

// glaze_language — language.bin-formatet. Containern ÄR
// källan för ramverkets och appens översättningar, precis som assets.bin
// är källan för bilderna: den committas, och en id→text-tabell är lika
// mycket källkod som en ikon.
//
// REN STD — inga beroenden, ingen koppling till glaze/gfx/builder_core.
// Libben ska kunna kompileras helt fristående (CLI:t, lang_gate del 1),
// precis som glaze_assets och cpp_reader/cpp_writer.
//
// Layout (little-endian, absoluta offsets ur headern; fältlistan står i
// language_file.cpp):
//   Header(64) → Språktabell(24/post) → Entrytabell(16/post) → Strängtabell
//
// NYCKELN ÄR ETT NUMERISKT ID och språket är formatets YTTRE DIMENSION:
// index 1 är "Ja" på alla språk, och uppslaget tar det valda språkets
// index 1. Ett femte språk är därför ett rent TILLÄGG — en
// språkpost, dess entrysvit, dess texter — medan en platt
// (id, språk)-tabell hade krävt omsortering av hela entrytabellen.
//
// Versionsregeln är hård regel 4:s anda: fel magic/major ⇒ VÄGRA; nyare
// minor ⇒ acceptera och hoppa okänt på DEKLARERAD längd (headerSize/
// langRecSize/entryRecSize) — aldrig gissa. Hela valideringen körs vid
// VARJE laddning; minsta fel ⇒ ladda ingenting.

namespace glzlang {

constexpr std::uint16_t kVersionMajor = 1;
constexpr std::uint16_t kVersionMinor = 0;

// ID-GOLVET: 1–999 är RAMVERKETS, 1000+ appens. Version 1 kräver att
// filen deklarerar exakt det här talet — en fil som påstår något annat är
// inte den här versionens fil.
constexpr std::uint32_t kAppIdFloor = 1000;

// flags bit 0 = ramverksfil. Okända bitar ⇒ VÄGRA (en framtida bit kan
// bära semantik den här läsaren inte kan uppfylla).
constexpr std::uint16_t kFlagFramework = 0x0001;
constexpr std::uint16_t kKnownFlags    = kFlagFramework;

// TAGGNORMALISERINGEN — gemener och '_' → '-'. EN sanning, delad av
// läsaren, packaren, `findLang` och runtime-lagrets `setLanguage`: två
// stavningar av samma tagg får aldrig bli två olika språk.
std::string normaliseraTagg(const std::string& tag);

struct LangView {
    std::string   tag;          // BCP-47, NORMALISERAD till gemener och '-'
    std::string   name;         // självbenämning: Svenska/English/Suomi …
    std::uint32_t firstEntry = 0;
    std::uint32_t entryCount = 0;
};

struct EntryView {
    std::uint32_t id        = 0;   // ALDRIG 0 — 0 betyder "ingen översättning"
    std::uint32_t langIndex = 0;   // redundant ⇒ VERIFIERBAR bakpekare
    std::string   text;            // TOM sträng är en GILTIG text
};

// Läsaren. open* validerar ALLT — vid minsta fel lämnas objektet tomt,
// err får en distinkt orsak (svenska) och false returneras.
class File {
public:
    bool openFile(const std::string& path, std::string& err);
    bool openBytes(std::vector<std::uint8_t> bytes, std::string& err);

    const std::vector<LangView>&  langs()   const { return langs_; }
    const std::vector<EntryView>& entries() const { return entries_; }

    // Filens EGNA bytes — `embed` skriver dem till en committad TU.
    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

    // Språkindex för en tagg (ASCII-skiftlägesokänsligt, '_' → '-'),
    // annars -1.
    int findLang(const std::string& tag) const;

    // Uppslag i ETT språks svit. nullptr = id:t saknas i det språket.
    // Binärsökning: entries är stigande sorterade på id inom sitt språk.
    const EntryView* find(int langIndex, std::uint32_t id) const;

    // Unionen av alla id som något språk i filen bär, stigande. Det är
    // "komplett"-begreppets nämnare.
    std::vector<std::uint32_t> allIds() const;

    std::uint16_t versionMajor() const { return verMajor_; }
    std::uint16_t versionMinor() const { return verMinor_; }
    std::uint16_t defaultLang()  const { return defaultLang_; }
    std::uint16_t flags()        const { return flags_; }
    bool isFramework() const { return (flags_ & kFlagFramework) != 0; }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<LangView>     langs_;
    std::vector<EntryView>    entries_;
    std::uint16_t             verMajor_    = 0;
    std::uint16_t             verMinor_    = 0;
    std::uint16_t             defaultLang_ = 0;
    std::uint16_t             flags_       = 0;
};

// ------------------------------------------------------- SCOPE-KONTROLLEN
//
// Låset på 1–999 bärs av ROUTINGEN och inte av ett headerfält. De två
// funktionerna är lager 2 och 3 i den kedjan, och de bor HÄR — inte i
// runtime — så att grinden kan mäta dem utan att länka glaze.
//
//   requireAppScope        flags == 0 OCH varje id >= kAppIdFloor.
//   requireFrameworkScope  EXAKT ramverksbiten OCH varje id < kAppIdFloor.
//
// Det bärande lagret ligger ändå ovanför: `tr(id)` med id < 1000 slår
// ALDRIG i appbutiken, så en fil som smiter förbi de här kan inte skugga
// en ramverkssträng.
bool requireAppScope(const File& f, std::string& err);
bool requireFrameworkScope(const File& f, std::string& err);

// -------------------------------------------------------------- packaren

struct PackLang {
    std::string tag;    // normaliseras vid packning
    std::string name;   // självbenämning, får inte vara tom
};

struct PackEntry {
    std::string   lang;   // taggen posten hör till
    std::uint32_t id = 0;
    std::string   text;   // får vara tom
};

// Bygger filen i minnet. Sorterar posterna per språk (deklarationsordning)
// och stigande på id inom språket — kontraktet läsaren kräver.
// `defaultLangTag` tom ⇒ första språket. `framework` sätter flaggbiten.
// Ids kontrolleras INTE mot golvet här: `pack --ramverk` är en
// bekvämlighet, och gränsen mäts av scope-funktionerna ovan.
bool packBytes(const std::vector<PackLang>&  langs,
               const std::vector<PackEntry>& entries,
               const std::string& defaultLangTag, bool framework,
               std::vector<std::uint8_t>& out, std::string& err);

// Samma sak, skriven ATOMÄRT till outPath.
bool pack(const std::vector<PackLang>&  langs,
          const std::vector<PackEntry>& entries,
          const std::string& defaultLangTag, bool framework,
          const std::string& outPath, std::string& err);

// ATOMÄR ERSÄTTNING — `std::filesystem::rename` är specificerad "as if by
// POSIX rename", alltså atomisk ersättning av en befintlig icke-katalog.
// Vi gör ALDRIG "ta bort destinationen, sedan rename" — det är inte
// atomiskt. Tempnamnet byggs av std::random_device-entropi plus en
// räknare i MÅLKATALOGEN, med exists()-kontroll före öppning: det är
// PRAKTISKT unikt, inte formellt kollisionsfritt mellan processer —
// standardbiblioteket har varken process-id eller O_EXCL, och att låtsas
// om en garanti std inte ger vore värre än att sakna den.
// KÄND WINDOWS-AVVIKELSE: ersättningen misslyckas om destinationen är
// öppen av en annan process.
bool writeAtomic(const std::string& path,
                 const std::vector<std::uint8_t>& bytes, std::string& err);

// -------------------------------------------------------------- manifestet
//
// Radbaserat, '#' = kommentar, tomrader ignoreras:
//
//   default sv                  # valfri; utelämnad ⇒ första språket
//   lang sv Svenska             # taggen, sedan SJÄLVBENÄMNINGEN (resten
//   id 1 Yes "Ja"               #   av raden — namn får bära blanksteg)
//   id 2 No  "Nej"
//   lang en English
//   id 1 Yes "Yes"
//
// `id` hör till senaste `lang`. Ordningen är `id <tal> [<KONSTANTNAMN>]
// "<text>"`: mittentoken är VALFRI och skiljs från texten på att texten
// alltid börjar med citattecken. Namnet lagras ALDRIG i .bin — det är
// bara underlag för `pack --header`, som skriver konstant-headern vid
// SAVE (regel 7: genererad kod committas, bygget genererar ingenting).
// Ges namnet flera gånger för samma id måste det vara SAMMA namn.
//
// Texten är en C++-STRÄNGLITERAL i en STRIKT escape-delmängd — \" \\ \n
// \t \r och inget annat, precis som `bld::parseCppString` — men EGEN
// implementation: libben får inte bero på builder_core.
struct IdName {
    std::uint32_t id = 0;
    std::string   name;   // C++-identifierare
};

struct Manifest {
    std::vector<PackLang>  langs;
    std::vector<PackEntry> entries;
    std::string            defaultLangTag;
    std::vector<IdName>    names;   // stigande på id
};

bool parseManifest(const std::string& text, Manifest& out, std::string& err);

// Filen tillbaka till manifesttext. Round-trippar innehållet;
// KONSTANTNAMNEN följer inte med, eftersom de aldrig lagras i .bin.
std::string exportManifest(const File& f);

// Människoläsbar rapport med TÄCKNINGSMATRISEN per språk — filens
// motsvarighet till `glazeassets list`.
std::string dump(const File& f);

// Konstant-headern (`pack --header`): `namespace <ns> { inline constexpr
// std::uint32_t <Namn> = <id>; }`, med filens text i valfritt språk som
// kommentar. Tom sträng = inga namn i manifestet.
std::string headerText(const Manifest& m, const std::string& ns,
                       const std::string& refLangTag);

} // namespace glzlang
