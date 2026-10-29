// Språkbutiken — den fristående Meyers-singletonen bakom glaze::tr.
//
// Samma precedens som `AssetStore` och `App::themeState`: den måste kunna
// leva FÖRE App (ett `tr()`-uppslag i en fönster-ctor får inte konstruera
// App i förtid) och efter (inget att riva — bara minne).
//
// TVÅ BUTIKER, och routingen ÄR låset på 1–999:
//
//   builtin   ramverkets 1–999, inbäddade som bytes i lang_builtin.cpp.
//             Laddas lat vid första uppslaget och verifieras med
//             `requireFrameworkScope` — en fil som smugit sig in med
//             app-id vägras.
//   app       appens 1000+, ur `loadLanguage`. Verifieras med
//             `requireAppScope`.
//
// `tr(id)` väljer butik på TALET och aldrig på filens egna påståenden, så
// en appfil kan inte skugga en ramverkssträng.
//
// De PLATTA TABELLERNA för aktivt språk och fallback ÄR filens
// egna entrytabeller: posterna ligger i en SAMMANHÄNGANDE, id-sorterad
// svit per språk, och `glzlang::File::find` binärsöker i den. Butiken
// håller därför bara fyra INDEX (aktivt/fallback × builtin/app) i stället
// för fyra kopior av samma data — en kopia som ingen jämför är precis den
// sorts drift som ska undvikas.

#include "glaze/language.h"

#include <cstdio>
#include <set>
#include <vector>

#include "gfx/platform.h"
#include "glaze/app.h"
#include "glaze_language/language_file.h"

#include "lang_builtin.h"

namespace glaze {
namespace {

struct LangStore {
    glzlang::File builtin;
    bool          builtinOk = false;

    glzlang::File app;
    bool          appOk = false;

    std::string    active;                  // normaliserad tagg
    // FALLBACKEN initieras ur RAMVERKSFILENS `defaultLang` i
    // `ensureBuiltin` — inte här. Fältet står tomt tills dess, så en
    // hårdkodad tagg aldrig kan tysta filens egen deklaration — en
    // spikad tagg kan råka stämma och då dölja att
    // `File::defaultLang()` aldrig läses.
    std::string    fallback;
    LanguageSource source   = LanguageSource::Automatic;
    bool           resolved = false;        // har automatiken körts?

    // GENERATIONEN börjar på 1: en nyfödd `TrText` har `gen_ == 0` och
    // måste alltså alltid räkna om första gången.
    std::uint32_t gen = 1;

    int builtinActive = -1, builtinFallback = -1;
    int appActive = -1, appFallback = -1;

    // Miss-loggen: EN loggrad per okänt id — inte en per frame.
    std::set<std::uint32_t> loggedMisses;
};

LangStore& store()
{
    static LangStore s;
    return s;
}

// Ramverksfilen laddas LAT och exakt en gång. Misslyckas den är det ett
// BYGGFEL och inte ett tillståndsfel — de här bytesen är committad
// källkod som `lang_gate` del 2 mäter mot `strings.h`.
void ensureBuiltin(LangStore& s)
{
    if (s.builtinOk)
        return;
    std::string err;
    std::vector<std::uint8_t> bytes(
        kLanguageBin, kLanguageBin + kLanguageBinSize);
    if (!s.builtin.openBytes(std::move(bytes), err)) {
        gfx::log("glaze::language: inbäddad language.bin är trasig (%s) — "
                 "ramverkets texter blir platshållare",
                 err.c_str());
        return;
    }
    if (!glzlang::requireFrameworkScope(s.builtin, err)) {
        gfx::log("glaze::language: inbäddad language.bin är ingen "
                 "ramverksfil (%s)", err.c_str());
        s.builtin = glzlang::File{};
        return;
    }
    s.builtinOk = true;

    // FILENS `defaultLang` ÄR den initiala fallbacken. Att
    // spika "sv" i C++ hade gjort headerfältet till dekoration: packar
    // man om ramverksfilen med ett annat default ska runtime följa den,
    // annars finns två konkurrerande sanningar om samma sak.
    // `setFallbackLanguage` skriver över detta när appen väljer själv.
    if (s.fallback.empty()) {
        const auto& L = s.builtin.langs();
        if (s.builtin.defaultLang() < L.size())
            s.fallback = L[s.builtin.defaultLang()].tag;
        if (s.fallback.empty())
            s.fallback = "en";   // filen saknade språk — sista utvägen
    }
}

// TVÅSTEGSMATCHNING, ASCII-skiftlägesokänslig: exakt tagg först, annars
// PRIMÄRT UNDERFÄLT (`nb-NO` → `nb`). Steg två är det som gör att en
// användare med `sv-FI` får svenska i stället för engelska.
int matchLang(const glzlang::File& f, const std::string& tag)
{
    if (tag.empty())
        return -1;
    const std::string n = glzlang::normaliseraTagg(tag);
    const int exakt = f.findLang(n);
    if (exakt >= 0)
        return exakt;
    const std::size_t streck = n.find('-');
    const std::string primar = (streck == std::string::npos)
                                   ? n
                                   : n.substr(0, streck);
    for (std::size_t i = 0; i < f.langs().size(); ++i) {
        const std::string& t = f.langs()[i].tag;
        const std::size_t  s = t.find('-');
        if ((s == std::string::npos ? t : t.substr(0, s)) == primar)
            return int(i);
    }
    return -1;
}

void rebuildIndices(LangStore& s)
{
    s.builtinActive   = s.builtinOk ? matchLang(s.builtin, s.active) : -1;
    s.builtinFallback = s.builtinOk ? matchLang(s.builtin, s.fallback) : -1;
    s.appActive       = s.appOk ? matchLang(s.app, s.active) : -1;
    s.appFallback     = s.appOk ? matchLang(s.app, s.fallback) : -1;
}

// STARTSPRÅKET — explicit → OS-lokalen → engelska. Körs vid FÖRSTA
// uppslaget och skriver ALDRIG över ett `Explicit`-val.
void ensureResolved(LangStore& s)
{
    ensureBuiltin(s);
    if (s.resolved)
        return;
    s.resolved = true;
    if (s.source == LanguageSource::Explicit && !s.active.empty()) {
        rebuildIndices(s);
        return;
    }
    const std::string os = gfx::systemLocale();
    int idx = s.builtinOk ? matchLang(s.builtin, os) : -1;
    if (idx < 0 && s.builtinOk)
        idx = s.builtin.findLang("en");   // sista ledet i kedjan
    s.active = (idx >= 0) ? s.builtin.langs()[std::size_t(idx)].tag
                          : std::string();
    rebuildIndices(s);
}

// Är taggen KOMPLETT i en butik? "Komplett" = bär varje id som NÅGOT
// språk i SAMMA butik bär. `saknade` fylls med det som fattas.
bool komplett(const glzlang::File& f, const std::string& tag,
              std::vector<std::uint32_t>& saknade)
{
    saknade.clear();
    const int li = matchLang(f, tag);
    if (li < 0) {
        // Ett språk som inte finns i butiken saknar per definition ALLT.
        saknade = f.allIds();
        return saknade.empty();
    }
    for (std::uint32_t id : f.allIds())
        if (!f.find(li, id))
            saknade.push_back(id);
    return saknade.empty();
}

std::string idLista(const std::vector<std::uint32_t>& ids)
{
    std::string s;
    for (std::size_t i = 0; i < ids.size() && i < 12; ++i)
        s += (i ? ", " : "") + std::to_string(ids[i]);
    if (ids.size() > 12)
        s += ", … (" + std::to_string(ids.size()) + " st)";
    return s;
}

// Byter in en redan VALIDERAD appfil och stegar generationen.
void bytAppfil(LangStore& s, glzlang::File ny)
{
    s.app   = std::move(ny);
    s.appOk = true;
    s.loggedMisses.clear();
    // En Automatic-källa får pröva OS-lokalen på nytt när butiken ändras;
    // ett Explicit-val står kvar.
    if (s.source == LanguageSource::Automatic)
        s.resolved = false;
    rebuildIndices(s);
    ++s.gen;
}


} // namespace

// ------------------------------------------------------------- upplösning

// Texten för ett id, eller platshållaren. Intern — `TrText::str()` är den
// enda anroparen, och den cachar resultatet mot generationen.
static std::string resolveId(std::uint32_t id)
{
    LangStore& s = store();
    ensureResolved(s);
    if (id == 0)
        return std::string();

    // ROUTINGEN: talet väljer butik, aldrig filens egna påståenden.
    const bool ramverk = id < glzlang::kAppIdFloor;
    const glzlang::File* f = ramverk ? &s.builtin : &s.app;
    const bool           ok = ramverk ? s.builtinOk : s.appOk;
    const int aktiv    = ramverk ? s.builtinActive : s.appActive;
    const int fallback = ramverk ? s.builtinFallback : s.appFallback;

    if (ok) {
        if (const glzlang::EntryView* e = f->find(aktiv, id))
            return e->text;
        if (const glzlang::EntryView* e = f->find(fallback, id))
            return e->text;
    }
    if (s.loggedMisses.insert(id).second)
        gfx::log("glaze::tr: id %u saknas i både '%s' och fallbacken '%s'",
                 unsigned(id), s.active.c_str(), s.fallback.c_str());
    // PLATSHÅLLAREN — synlig i UI:t, aldrig tomhet: en saknad översättning
    // ska SYNAS och inte se ut som en tom etikett.
    char buf[32];
    std::snprintf(buf, sizeof buf, "\xE2\x9F\xA6%u\xE2\x9F\xA7", unsigned(id));
    return buf;
}

// ------------------------------------------------------------------ TrText

TrText::TrText(const char* s) : literal_(s ? s : "") {}
TrText::TrText(std::string s) : literal_(std::move(s)) {}

const std::string& TrText::str() const&
{
    if (id_ == 0)
        return literal_;
    LangStore& s = store();
    ensureResolved(s);
    if (gen_ != s.gen) {
        // IN PLACE — samma strängobjekt, nytt innehåll. Referensen från
        // ett tidigare `str()`-anrop pekar därför fortfarande på ett
        // levande objekt; det är innehållet som bytts.
        resolved_ = resolveId(id_);
        gen_      = s.gen;
    }
    return resolved_;
}

bool TrText::empty() const
{
    return str().empty();
}

bool TrText::operator==(const TrText& o) const
{
    if (id_ != o.id_)
        return false;
    return id_ ? true : literal_ == o.literal_;
}

TrText tr(std::uint32_t id)
{
    TrText t;
    t.id_ = id;
    return t;
}

// ---------------------------------------------------------------- API:t

bool setLanguage(const std::string& tag)
{
    LangStore& s = store();
    ensureBuiltin(s);
    const int li = s.builtinOk ? matchLang(s.builtin, tag) : -1;
    if (li < 0) {
        gfx::log("glaze::setLanguage: okänd språktagg '%s' — oförändrat",
                 tag.c_str());
        return false;
    }
    const std::string ny = s.builtin.langs()[std::size_t(li)].tag;
    s.source   = LanguageSource::Explicit;
    s.resolved = true;
    if (ny == s.active)
        return true;   // redan aktivt: ingen generation, ingen omritning

    s.active = ny;
    rebuildIndices(s);
    // Se noten i `setFallbackLanguage`: miss-loggen nollställs INTE av ett
    // språkbyte, bara när en ny appfil byts in.
    ++s.gen;

    // Invalideringen — ordningen är BÄRANDE.
    App::languageChangedAll();
    return true;
}

const std::string& language()
{
    LangStore& s = store();
    ensureResolved(s);
    return s.active;
}

LanguageSource languageSource()
{
    return store().source;
}

bool setFallbackLanguage(const std::string& tag)
{
    LangStore& s = store();
    ensureBuiltin(s);
    const int li = s.builtinOk ? matchLang(s.builtin, tag) : -1;
    if (li < 0) {
        gfx::log("glaze::setFallbackLanguage: okänd språktagg '%s' — "
                 "oförändrat", tag.c_str());
        return false;
    }
    const std::string ny = s.builtin.langs()[std::size_t(li)].tag;

    // KRAVET ÄR BÅDA BUTIKERNA: en fallback som själv har hål är
    // ingen fallback, och hålet kan sitta i vilken av de två som helst.
    std::vector<std::uint32_t> saknade;
    if (!komplett(s.builtin, ny, saknade)) {
        gfx::log("glaze::setFallbackLanguage: '%s' är ofullständigt i "
                 "RAMVERKSBUTIKEN (saknar %s) — oförändrat",
                 ny.c_str(), idLista(saknade).c_str());
        return false;
    }
    if (s.appOk && !komplett(s.app, ny, saknade)) {
        gfx::log("glaze::setFallbackLanguage: '%s' är ofullständigt i "
                 "APPBUTIKEN (saknar %s) — oförändrat",
                 ny.c_str(), idLista(saknade).c_str());
        return false;
    }

    if (ny == s.fallback)
        return true;
    s.fallback = ny;
    rebuildIndices(s);
    // MISS-LOGGEN NOLLSTÄLLS INTE här: ett id som saknas gör det i
    // varje språk (ramverksbutiken är komplett), så en
    // andra loggrad för samma id säger ingenting nytt. Den nollställs
    // bara när DATAN faktiskt byts — i `bytAppfil`.
    ++s.gen;
    App::languageChangedAll();
    return true;
}

const std::string& fallbackLanguage()
{
    // ensureBuiltin FÖRST: fallbacken initieras ur ramverksfilens
    // `defaultLang` där, så utan det anropet hade den första läsaren
    // fått en TOM sträng tillbaka.
    LangStore& s = store();
    ensureBuiltin(s);
    return s.fallback;
}

int languageCount()
{
    LangStore& s = store();
    ensureBuiltin(s);
    return s.builtinOk ? int(s.builtin.langs().size()) : 0;
}

LanguageInfo languageAt(int i)
{
    LangStore& s = store();
    ensureBuiltin(s);
    LanguageInfo ut;
    if (!s.builtinOk || i < 0 || i >= int(s.builtin.langs().size()))
        return ut;
    const glzlang::LangView& l = s.builtin.langs()[std::size_t(i)];
    ut.tag  = l.tag;
    ut.name = l.name;

    // TÄCKNINGEN mäts över BÅDA butikerna — det är vad en språkväljare
    // vill visa: "är den här raden helt översatt i den här appen?"
    std::size_t har = 0, totalt = 0;
    for (std::uint32_t id : s.builtin.allIds()) {
        ++totalt;
        if (s.builtin.find(i, id))
            ++har;
    }
    if (s.appOk) {
        const int ai = matchLang(s.app, l.tag);
        for (std::uint32_t id : s.app.allIds()) {
            ++totalt;
            if (ai >= 0 && s.app.find(ai, id))
                ++har;
        }
    }
    ut.coverage = totalt ? float(har) / float(totalt) : 1.0f;
    ut.complete = har == totalt;
    return ut;
}

// ------------------------------------------------------------ appens fil

// `onskadFallback` tom ⇒ behåll gällande fallback. Är den SATT byts filen
// och fallbacken som EN transaktion — se kommentaren vid `loadLanguage`
// nedan om varför den formen måste finnas.
static bool laddaAppBytes(std::vector<std::uint8_t> bytes,
                          const char* varifran,
                          const std::string& onskadFallback)
{
    LangStore& s = store();
    ensureBuiltin(s);

    glzlang::File ny;
    std::string   err;
    if (!ny.openBytes(std::move(bytes), err)) {
        gfx::log("glaze::loadLanguage(%s): %s — den gamla filen behålls",
                 varifran, err.c_str());
        return false;
    }
    if (!glzlang::requireAppScope(ny, err)) {
        gfx::log("glaze::loadLanguage(%s): %s — den gamla filen behålls",
                 varifran, err.c_str());
        return false;
    }

    // Vilken fallback ska GÄLLA efter bytet?
    std::string malFallback = s.fallback;
    if (!onskadFallback.empty()) {
        const int li = s.builtinOk ? matchLang(s.builtin, onskadFallback) : -1;
        if (li < 0) {
            gfx::log("glaze::loadLanguage(%s): okänt fallbackspråk '%s' — "
                     "ingenting ändras",
                     varifran, onskadFallback.c_str());
            return false;
        }
        malFallback = s.builtin.langs()[std::size_t(li)].tag;
    }

    // ATOMICITETEN: validera BÅDA butikerna mot målfallbacken FÖRE
    // något byts. Ordningen är alltid "validera fullt, byt sedan".
    std::vector<std::uint32_t> saknade;
    if (!onskadFallback.empty() && !komplett(s.builtin, malFallback, saknade)) {
        gfx::log("glaze::loadLanguage(%s): fallbackspråket '%s' är "
                 "ofullständigt i RAMVERKSBUTIKEN (saknar %s) — ingenting "
                 "ändras",
                 varifran, malFallback.c_str(), idLista(saknade).c_str());
        return false;
    }
    if (!komplett(ny, malFallback, saknade)) {
        gfx::log("glaze::loadLanguage(%s): fallbackspråket '%s' är "
                 "ofullständigt i filen (saknar %s) — FILEN AVSLÅS, den "
                 "gamla behålls. Ladda med ett fallbackspråk filen HAR "
                 "(loadLanguage(path, tag)), eller släpp den gamla filen "
                 "med unloadLanguage() först.",
                 varifran, malFallback.c_str(), idLista(saknade).c_str());
        return false;
    }

    s.fallback = malFallback;
    bytAppfil(s, std::move(ny));
    App::languageChangedAll();
    return true;
}

// TRANSAKTIONEN finns för att fil och fallback annars kan LÅSA VARANDRA
//: har appfil A komplett svenska men inte engelska, och
// B tvärtom, så avslås BÅDE `setFallbackLanguage("en")` (A saknar en) och
// `loadLanguage(B)` (B saknar sv) — två giltiga lägen utan tillåten
// övergång mellan sig. Att byta båda i ETT anrop är vägen dit, och
// `unloadLanguage()` är den andra: utan appbutik finns inget att krocka
// med.
bool loadLanguage(const std::string& path, const std::string& fallbackTag)
{
    // Kandidatordningen är `loadAssets`: exe-katalogen → bundlens
    // Resources (basePath() är …/Contents/MacOS/ på Mac) → som det är.
    std::vector<std::string> kandidater;
    if (!path.empty() && path[0] != '/' &&
        (path.size() < 2 || path[1] != ':')) {
        const std::string base = gfx::basePath();
        if (!base.empty()) {
            kandidater.push_back(base + path);
            kandidater.push_back(base + "../Resources/" + path);
        }
    }
    kandidater.push_back(path);

    for (const std::string& k : kandidater) {
        glzlang::File f;
        std::string   err;
        if (!f.openFile(k, err))
            continue;
        return laddaAppBytes(f.bytes(), k.c_str(), fallbackTag);
    }
    gfx::log("glaze::loadLanguage: hittade ingen läsbar '%s' — appens "
             "texter blir platshållare", path.c_str());
    return false;
}

bool loadLanguageBytes(const void* data, std::size_t n,
                       const std::string& fallbackTag)
{
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    return laddaAppBytes(std::vector<std::uint8_t>(p, p + n), "bytes",
                         fallbackTag);
}

void unloadLanguage()
{
    LangStore& s = store();
    if (!s.appOk)
        return;
    // Appbutiken släpps HELT: id 1000+ blir platshållare igen, och
    // fallbacken kan därefter bytas utan att en gammal fil kan avslå
    // valet. Ramverkets 1–999 är oberörda — de bor i den inbäddade filen.
    s.app   = glzlang::File{};
    s.appOk = false;
    rebuildIndices(s);
    s.loggedMisses.clear();
    ++s.gen;
    App::languageChangedAll();
}

} // namespace glaze
