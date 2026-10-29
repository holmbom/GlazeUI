#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// FILVÄLJARENS MODELL — INTERN och REN STD: ingen
// glaze-typ, inget fönster, ingen font. Det dialogen VET — vad som finns i
// en mapp, vad ett filter släpper igenom, hur ett inskrivet namn eller en
// sökväg tolkas och i vilken ordning raderna står — bor här, så att det går
// att grinda HEADLESS: tests/filedialog_gate.cpp når filen precis som
// msgbox_gate når `message_box_table.h`. Det dialogen GÖR (fönstret,
// listorna, flödet) bor i file_dialog.cpp.
//
// KATALOGLÄSNINGEN ÄR REN std::filesystem — inget fasadtillägg. Det enda
// OS-specifika i filväljaren är de kända platserna (`gfx::knownPlaces`).
// Ingenting här KASTAR: varje filsystemsanrop går genom sin
// `std::error_code`-överlagring, och ett fel blir ett svar.

namespace glaze::filedlg {

namespace fs = std::filesystem;

// ----------------------------------------------------------------- UTF-8
// Sökvägar är UTF-8 överallt utanför den här filen. `fs::path::string()`
// konverterar på Windows genom ANSI-teckentabellen och förstör åäö — därför
// går varje övergång mellan sträng och sökväg genom de här två
// (`u8string`, som är UTF-8 på varje plattform).
fs::path    pathFromUtf8(const std::string& utf8);
std::string utf8FromPath(const fs::path& p);

// ---------------------------------------------------------------- FILTER
// "*.png; *.JPG" → {"*.png", "*.JPG"}: semikolon skiljer, blanktecken runt
// varje mönster tas bort och tomma mönster försvinner.
std::vector<std::string> splitPatterns(const std::string& patterns);

// Ett mönster mot ett NAMN: `*` är vilken följd som helst (även tom), `?`
// exakt ett TECKEN — ett å är ett tecken och inte två byte. ASCII-bokstäver
// jämförs utan skiftläge på alla plattformar ("*.png" träffar "BILD.PNG"):
// en filterlista är skriven för användaren, inte för filsystemet. "*" och
// "*.*" träffar allt, också ett namn utan punkt — Windows-konventionen som
// varje "Alla filer (*.*)" bygger på.
bool globMatch(const std::string& name, const std::string& pattern);

// Släpper mönstren igenom namnet? En TOM lista släpper igenom allt.
bool matchesAny(const std::string& name,
                const std::vector<std::string>& patterns);

// Ändelsen ett SPARAT namn utan ändelse får: ".png" när filtrets FÖRSTA
// mönster är exakt "*.<ändelse>" utan fler jokertecken, annars tom — ett
// "*.*" eller "rapport*.txt" säger ingenting om vilken ändelse som menas.
std::string defaultExtension(const std::vector<std::string>& patterns);

// Namnet med ändelsen när det saknar en egen ("rapport" + ".pdf"); ett namn
// med ändelse eller en tom `ext` lämnas orört. En avslutande punkt räknas
// som ingen ändelse ("rapport." ⇒ "rapport.pdf").
std::string withExtension(const std::string& name, const std::string& ext);

// Filändelsen utan punkt ("bild.png" → "png"). Ingen för ".bashrc" (en
// punkt FÖRST är ett dolt namn, inte en ändelse) och inte för "x.".
std::string extensionOf(const std::string& name);

// ------------------------------------------------------------ NAMNFÄLTET
// Fältet bär ETT namn eller FLERA i citattecken — Utforskarens form för
// flerval: `"a.txt" "b.txt"`. Ett fält som inte BÖRJAR med `"` är ett enda
// namn (ett citattecken inuti är då en del av det). Blanktecken runt tas
// bort; ett oavslutat citat tar resten av fältet.
std::vector<std::string> splitNames(const std::string& field);
// Motsatsen: ett namn står som det är, flera citeras.
std::string joinNames(const std::vector<std::string>& names);

// ------------------------------------------------------------- ORDNINGEN
// Namnordningen: NATURLIG ("fil2" före "fil10", inledande nollor
// ignorerade) och utan skiftläge för ASCII och Latin-1 (Å = å, Ö = ö).
// Lika i allt det ⇒ bytevis, så att ordningen alltid är total. Svarar <0,
// 0 eller >0. Språkmedveten kollation (å före ä på svenska) ligger utanför
// — samma gräns som tabellens egen sortering.
int compareNames(const std::string& a, const std::string& b);

// ---------------------------------------------------------------- VISNINGEN
// Storleken med SI-prefix (1000-steg): "0 B", "999 B", "1,2 kB", "12 kB",
// "3,4 MB". En decimal under tio, annars heltal. Två saker är SPRÅKETS och
// kommer in som argument, för modellen är ren std: decimaltecknet
// (`DecimalSeparator`) och BYTE-ENHETEN (`FileSizeUnit`) — prefixen
// k/M/G/T/P är internationella, men finskan skriver tavu ("12 kt", "3,4 Mt").
std::string sizeText(std::uintmax_t bytes, const std::string& decimalSep,
                     const std::string& byteUnit);

// Ändringstiden i LOKAL tid, "ÅÅÅÅ-MM-DD hh:mm" — samma form på alla språk
// (ISO 8601 läses överallt) och dessutom i rätt ordning som text.
std::string dateText(std::int64_t unixSeconds);

// ---------------------------------------------------------- KATALOGLÄSNINGEN
struct Entry {
    std::string    name;              // UTF-8, bara namnet
    bool           folder   = false;
    std::uintmax_t size     = 0;      // byte — bara filer
    bool           hasSize  = false;
    std::int64_t   modified = 0;      // unix-sekunder
    bool           hasTime  = false;
};

// Varför en mapp inte gick att läsa — dialogen säger det i klartext.
enum class ListError {
    None,
    NoAccess,     // ingen behörighet
    Missing,      // finns inte (eller är ingen mapp)
    Unreadable,   // allt annat — också ett fel MITT I läsningen
};

struct Listing {
    std::vector<Entry> entries;   // mappar först, sedan filer — båda i
                                  // namnordning (`compareNames`)
    ListError          error = ListError::None;
};

// Frågan "döljer OS:et den här posten?" (UTF-8-sökväg in). Dialogen skickar
// `gfx::isHiddenFile` — Windows ATTRIBUT och macOS FLAGGA, som inte syns i
// namnet — och modellen förblir ren std: den känner bara en funktionspekare.
using HiddenFn = bool (*)(const std::string& utf8Path);

// Mappens innehåll. `foldersOnly` är mappläget. `patterns` gäller FILER och
// aldrig mappar — man måste kunna gå in i en mapp vilket filter som än
// gäller. DOLDA POSTER: namn som börjar med PUNKT döljs (Unix-konventionen,
// på alla plattformar), och det `hidden` svarar ja på likaså (nullptr = bara
// punkten) — utom med `showHidden` (dialogens kryssruta): då visas
// allt, och `hidden` frågas aldrig, för på Windows är varje fråga ett
// filsystemsanrop. Filtret gäller en dold fil precis som en annan. Ett fel
// MITT I läsningen behåller det som hann läsas och svarar `Unreadable`.
//
// `showHidden` står SIST med flit: en bool före funktionspekaren hade tagit
// emot en lambda eller `&gfx::isHiddenFile` — pekaren omvandlas till `true`
// — så ett anrop som missats vid en omskrivning hade KOMPILERAT och visat
// allt, med bara en varning (GCC:s -Waddress) i stället för ett fel.
Listing listFolder(const fs::path& folder, bool foldersOnly,
                   const std::vector<std::string>& patterns,
                   HiddenFn hidden = nullptr, bool showHidden = false);

// Felkoden → orsaken. `permission_denied`/`operation_not_permitted` är
// ingen behörighet, `no_such_file_or_directory`/`not_a_directory` att mappen
// saknas, allt annat att den inte gick att läsa. libstdc++ på
// Windows svarar EINVAL (och inte EACCES) för en mapp man inte får lista —
// profilens gamla korsningar ("Mina dokument") och "System Volume
// Information" — så där blir det "kunde inte läsas" och inte "ingen
// behörighet". Fel mening hade varit värre än en allmän; Linux och macOS
// svarar EACCES och får den specifika.
ListError classify(const std::error_code& ec);

// SORTERINGSNYCKLARNA — en per KOLUMN (namn, storlek, ändrad, typ) och
// rad. Tabellen sorterar själv på nycklarna (`Table::setSortKey`), och de är
// RANGER och inte råvärden: mapparna FÖRST i varje kolumn (Utforskarens
// gruppering — fallande sortering lägger dem därmed sist), sedan kolumnens
// eget värde och sist namnet, så att lika storlekar eller typer står i
// namnordning och ordningen aldrig hoppar. En fil utan känd storlek eller
// tid kommer före de kända. `typeTexts` är typkolumnens VISADE texter,
// parallella med `entries` (de är språkets och bor därför hos anroparen).
enum Column { ColName = 0, ColSize = 1, ColModified = 2, ColType = 3 };
std::vector<std::vector<double>> sortKeys(
    const std::vector<Entry>&       entries,
    const std::vector<std::string>& typeTexts);

// ------------------------------------------------------------- SÖKVÄGARNA
// En sökväg utan avslutande avskiljare ("C:\Users\" → "C:\Users") — utom
// roten, som ÄR en avskiljare ("C:\", "/").
fs::path withoutTrailingSeparator(const fs::path& p);

// En sökväg eller ett namn mot `current`: en absolut står kvar, en relativ
// läggs till `current`, och "." och ".." löses LEXIKALT, utan att
// filsystemet tillfrågas. En UNC-sökväg ("\\server\resurs\…") är absolut och
// dess ".." stannar vid resursen — libstdc++ håller inte med om någotdera, så
// den formen läses ur texten (skälet står i cpp:n).
fs::path resolvePath(const fs::path& current, const fs::path& p);
// Samma sak för en INSKRIVEN text (UTF-8): blanktecken runt tas bort först.
fs::path resolveTyped(const fs::path& current, const std::string& typedUtf8);

// Närmaste förfader-eller-jag som FINNS som katalog — där en sparad mapp som
// tagits bort ska öppna. Tom sökväg när ingen gör det; sökningen går aldrig
// förbi en rot (`isRootFolder`).
fs::path nearestExisting(const fs::path& p);

// Roten ("C:\", "/", UNC-resursen "\\server\resurs\") har ingen mapp ovanför
// sig.
bool isRootFolder(const fs::path& p);

// Mappen ovanför — eller `p` själv i roten.
fs::path parentFolder(const fs::path& p);

} // namespace glaze::filedlg
