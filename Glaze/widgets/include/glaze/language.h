#pragma once
#include <cstdint>
#include <string>

// SPRÅKPAKETET — översättningarna som appen och ramverket ser
//
//     okBtn.setText(glaze::tr(glaze::str::Ok));   // ramverkets 1–999
//     rubrik.setText(glaze::tr(demo::str::Titel)); // appens 1000+
//
// VARFÖR DET ÄR VÅRT ANSVAR: samma skäl som chromet. Dialogerna är VÅRA
// och aldrig OS:ets, så vi äger varenda sträng — en toolkit som anropar
// systemdialogen får språket gratis, vi får ingenting gratis.
//
// NYCKELN ÄR ETT NUMERISKT ID och språket är dimensionen: index 1 är Ja
// på alla språk, och uppslaget tar det valda språkets index 1.
// Källtext som nyckel hade brutit vid varje textjustering, och `.glazeui`
// kan inte bära något annat än ett tal — `builder_core` är ren std utan
// reflektion och kan aldrig lösa `glaze::str::Ok` till ett nummer.
//
// TVÅ BUTIKER, och gränsen är ROUTINGEN och inte ett headerfält:
//
//   id 1–999    RAMVERKETS, inbäddade i libglaze (lang_builtin.cpp).
//               De kan aldrig saknas — ett bibliotek utan sina egna
//               knapptexter är trasigt, inte osatt.
//   id 1000+    APPENS, ur `loadLanguage("app.langbin")`.
//
// `tr(id)` med id < 1000 slår ALDRIG i appbutiken, så en appfil kan inte
// skugga en ramverkssträng ens om den smiter förbi verifieringen.
//
// ⚠ TRÅDKONTRAKT: `setLanguage`, `setFallbackLanguage`, `loadLanguage`
// och ALL upplösning av widgetägda `TrText` är UI-TRÅDSOPERATIONER. Den
// lata cachen i `TrText` är inte trådsäker. `glaze::app().post(fn)` är
// den enda vägen in från en annan tråd — samma kontrakt som resten av
// ramverket.
//
// ⚠ OCH: `setLanguage` FÅR INTE ANROPAS SYNKRONT ur en handler som fyras
// ur ett POPUP-fönster. `Dropdown::commit()` fyrar `onChange` och
// anropar `close()` FÖRST DÄREFTER, alltså mitt i popupens egen
// klick-dispatch — och `setLanguage` river popups SYNKRONT
// (`closePopupsNow`), så fönstret vars event pågår skulle rivas under
// fötterna på dispatchen. En språkväljare postar därför bytet:
//
//     valj.onChange = [this](glaze::Widget*, glaze::ChangeArgs&) {
//         const std::string tag = taggFor(valj.selectedIndex());
//         glaze::app().post([tag] { glaze::setLanguage(tag); });
//     };

namespace glaze {

// Hur det aktiva språket valdes. `Automatic` betyder att kedjan
// OS-lokal → engelska får bestämma; `Explicit` att appen sagt sitt, och
// då rör automatiken det ALDRIG igen — en senare `loadLanguage` kan
// alltså inte rulla tillbaka appens eget val.
enum class LanguageSource { Automatic, Explicit };

// Ett språk som det ser ut UTIFRÅN — underlaget för en språkväljare.
// `name` är SJÄLVBENÄMNINGEN (Svenska · English · Norsk bokmål · Suomi)
// och ska ALDRIG översättas: en väljare som visar "Finska" för den som
// inte läser svenska är oanvändbar.
struct LanguageInfo {
    std::string tag;                // BCP-47, gemener
    std::string name;               // självbenämning
    bool        complete = false;   // bär varje id BÅDA butikerna känner
    float       coverage = 0.0f;    // 0–1 över båda butikerna
};

// ---------------------------------------------------------------- TrText
//
// Bär ett ID eller en LITERAL, och cachar sin upplösning i sig själv.
//
// De två ctorerna är IMPLICITA med flit: `btn.setText("Hej")` ska
// fortsätta kompilera oförändrat, och en handskriven app som aldrig rör
// språk ska aldrig behöva veta att typen finns.
class TrText {
public:
    TrText() = default;
    TrText(const char* s);          // literal — implicit, med flit
    TrText(std::string s);          // literal — implicit, med flit

    // Den UPPLÖSTA texten. Referensen avser STRÄNGOBJEKTET, som lever
    // lika länge som TrText:en; innehållet skrivs om IN PLACE vid
    // språkbyte. Pekare från `c_str()`/`data()` och iteratorer
    // invalideras då — precis som för varje annan `std::string`.
    const std::string& str() const&;
    // FÄLLAN `tr(id).str()` fångas i KOMPILERINGEN: en temporär TrText dör
    // vid semikolonet och referensen hade dinglat.
    const std::string& str() const&& = delete;

    std::uint32_t id() const { return id_; }   // 0 = literal
    bool          empty() const;               // den UPPLÖSTA texten tom?

    // TOKENSEMANTIK, inget annat: id mot id, literal mot literal.
    // BLANDNING id/literal är ALDRIG lika — och det är avsiktligt, för
    // "upplöst text vid blandning" hade varit språkberoende, kunnat byta
    // resultat i drift och gjort identiteten icke-transitiv.
    //
    // Det finns MEDVETET ingen `operator==(std::string_view)`: den blir
    // tvetydig mot den implicita ctorn ovan, så `t == nagonString` hade
    // haft två likvärdiga användardefinierade konverteringsvägar och
    // slutat kompilera. Textjämförelse görs via `text()`/`.str()`.
    bool operator==(const TrText& o) const;
    bool operator!=(const TrText& o) const { return !(*this == o); }

private:
    friend TrText tr(std::uint32_t id);

    std::uint32_t         id_ = 0;
    std::string           literal_;
    mutable std::string   resolved_;
    mutable std::uint32_t gen_ = 0;
};

// Texten för ett id. Uppslagskedjan är AKTIVT SPRÅK → FALLBACKSPRÅKET →
// platshållaren `⟦id⟧` plus EN loggrad per id (aldrig en per frame).
TrText tr(std::uint32_t id);

// --------------------------------------------------------- appens fil
//
// Laddar appens language.bin (id 1000+). Relativa sökvägar provas mot
// `gfx::basePath()` och macOS-bundlens Resources, precis som
// `loadAssets`.
//
// ATOMISKT: filen valideras HELT — struktur, CRC, app-scope OCH att den
// gällande fallbacken är KOMPLETT i den — innan den byts in. Faller
// något avslås FILEN, den gamla behålls, och orsaken loggas. Ordningen
// är alltid: validera fullt, byt sedan.
// `fallbackTag` SATT ⇒ filen och fallbacken byts som EN TRANSAKTION, båda
// validerade före något ändras. Den formen måste finnas, för annars kan
// fil och fallback låsa varandra: har den laddade filen komplett svenska
// men inte engelska, och den NYA filen tvärtom, avslås både
// `setFallbackLanguage("en")` (gamla filen saknar en) och `loadLanguage`
// av den nya (den saknar sv) — två giltiga lägen utan väg emellan.
bool loadLanguage(const std::string& path = "language.bin",
                  const std::string& fallbackTag = {});
// Samma sak från bytes i minnet — testingången.
bool loadLanguageBytes(const void* data, std::size_t n,
                       const std::string& fallbackTag = {});

// Släpper appbutiken (id 1000+ blir platshållare). Ramverkets 1–999 är
// oberörda. Den ANDRA vägen ur låsningen ovan: utan appfil finns inget
// som kan avslå ett fallbackbyte. No-op när ingen appfil är laddad.
void unloadLanguage();

// Byter aktivt språk och märker källan `Explicit`. Okänd tagg ⇒ loggrad +
// false + oförändrat språk. Se ⚠-noten om popup-handlers överst.
bool setLanguage(const std::string& tag);
const std::string& language();
LanguageSource     languageSource();

// FALLBACKEN — utvecklarens val, default "sv".
// Kravet är hårt: taggen måste finnas och vara KOMPLETT i ramverkets
// butik OCH i den laddade appbutiken. "Komplett" = bär varje id som
// något språk i SAMMA butik bär. Annars loggas de saknade id:na, false
// returneras och fallbacken är oförändrad — en fallback som själv har
// hål är ingen fallback.
bool setFallbackLanguage(const std::string& tag);
const std::string& fallbackLanguage();

// Språken som RAMVERKSFILEN bär — underlaget för en språkväljare.
// Appfilen får bära en DELMÄNGD (att tvinga varje Glaze-app att
// översätta sitt UI till finska vore ett orimligt krav), och dess språk
// räknas därför inte upp här; täckningen syns i `LanguageInfo`.
int          languageCount();
LanguageInfo languageAt(int i);

} // namespace glaze
