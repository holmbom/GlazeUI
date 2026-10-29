// Asset-storen — den fristående Meyers-singletonen bakom glaze::img.
// Äger assets.bin som RÅBYTES (glzassets::File) + den LOGISKA tabellen
// handtag → post. Ingen renderer, inga texturer här — de bor per fönster
// i ImageCache. Samma singleton-precedens som App::themeState: måste
// kunna leva före App (uppslag i fönster-ctor:er) och efter (inget att
// riva — bara minne).
//
// TVÅ BUTIKER: appens (loadAssets, id = index+1) och
// RAMVERKETS — den inbäddade framework_assets.bin (assets_builtin.cpp),
// öppnad latt vid första detail::builtinAsset. Ramverkets id har bit 31
// satt, så de aldrig krockar med appens och överlever en ny loadAssets;
// assetBytes och assetSizeClass routar på biten.

#include "glaze/assets.h"

#include <set>
#include <unordered_map>
#include <vector>

#include "assets_builtin.h"
#include "gfx/platform.h"
#include "glaze_assets/assets_file.h"
#include "image_cache.h"

namespace glaze {
namespace {

// Ramverkets handtag: bit 31 + (index+1) i den inbäddade butiken.
constexpr std::uint32_t kBuiltinBit = 0x80000000u;

struct AssetStore {
    glzassets::File file;
    bool            loaded = false;

    // Logisk tabell: handtag id = index+1, en logisk post per fysisk.
    std::vector<std::uint32_t> entryIdx;     // logisk → entries()-index
    std::vector<float>         sizeClass;    // logisk → listans klass (log. px)

    // (lista, nyckel) → logiskt id. '\0'-skarv är säker: formatets
    // strängar är NUL-terminerade och kan inte innehålla NUL.
    std::unordered_map<std::string, std::uint32_t> byKey;

    // Miss-loggen: EN loggrad per okänd nyckel — inte en per frame.
    std::set<std::string> loggedMisses;

    // Stegas varje gång loadAssets ersätter butiken (detail::assetGeneration).
    std::uint64_t generation = 0;
};

// Fyller den logiska tabellen ur en öppnad fil — samma för båda butikerna.
void indexEntries(AssetStore& s)
{
    s.entryIdx.clear();
    s.sizeClass.clear();
    s.byKey.clear();
    s.loggedMisses.clear();
    for (std::uint32_t li = 0; li < s.file.lists().size(); ++li) {
        const glzassets::ListView& l = s.file.lists()[li];
        for (std::uint32_t i = l.firstEntry; i < l.firstEntry + l.entryCount;
             ++i) {
            s.entryIdx.push_back(i);
            s.sizeClass.push_back(float(l.sizeClass));
            s.byKey[l.name + '\0' + s.file.entries()[i].key] =
                std::uint32_t(s.entryIdx.size());   // id = index+1
        }
    }
}

AssetStore& store()
{
    static AssetStore s;
    return s;
}

// Ramverkets butik — öppnas EN gång, vid första uppslaget. Den inbäddade
// filen är committad och grindad, så ett fel här är ett trasigt bygge: det
// loggas och butiken förblir tom (alla ramverkets uppslag ger ogiltiga
// handtag — ingen ikon, aldrig en krasch).
AssetStore& builtinStore()
{
    static AssetStore s = [] {
        AssetStore b;
        std::vector<std::uint8_t> bytes(
            kFrameworkAssetsBin, kFrameworkAssetsBin + kFrameworkAssetsBinSize);
        std::string err;
        if (b.file.openBytes(std::move(bytes), err)) {
            b.loaded = true;
            indexEntries(b);
        } else {
            gfx::log("assets: ramverkets inbäddade bilder är trasiga (%s)",
                     err.c_str());
        }
        return b;
    }();
    return s;
}

// Handtaget → (butik, logiskt index) — nullptr för ett ogiltigt handtag.
const AssetStore* storeFor(AssetHandle h, std::uint32_t& logical)
{
    if (!h.valid())
        return nullptr;
    const bool builtin = (h.id & kBuiltinBit) != 0;
    const AssetStore& s = builtin ? builtinStore() : store();
    const std::uint32_t id = h.id & ~kBuiltinBit;
    if (id == 0 || id > s.entryIdx.size())
        return nullptr;
    logical = id - 1;
    return &s;
}

} // namespace

bool loadAssets(const std::string& path)
{
    AssetStore& s = store();

    // Kandidatordningen för relativa namn: exe-katalogen → bundlens
    // Resources (basePath() är …/Contents/MacOS/ på Mac) → som det är.
    std::vector<std::string> candidates;
    if (!path.empty() && path[0] != '/' &&
        (path.size() < 2 || path[1] != ':')) {          // ej absolut (POSIX/Win)
        const std::string base = gfx::basePath();
        if (!base.empty()) {
            candidates.push_back(base + path);
            candidates.push_back(base + "../Resources/" + path);
        }
    }
    candidates.push_back(path);

    std::string err, lastErr;
    for (const std::string& cand : candidates) {
        glzassets::File f;
        if (f.openFile(cand, err)) {
            s.file   = std::move(f);
            s.loaded = true;
            indexEntries(s);
            ++s.generation;   // samma id kan nu peka på en annan bild
            gfx::log("assets: laddade %s (%zu listor, %zu poster)",
                     cand.c_str(), s.file.lists().size(),
                     s.file.entries().size());
            return true;
        }
        lastErr = err;   // sista kandidatens orsak är den intressanta
    }
    // Tyst no-op i drift men aldrig osynlig: EN loggrad med orsaken.
    gfx::log("assets: %s laddades inte (%s)", path.c_str(), lastErr.c_str());
    return false;
}

AssetHandle img(const std::string& list, const std::string& key)
{
    AssetStore& s = store();
    const std::string k = list + '\0' + key;
    auto it = s.byKey.find(k);
    if (it != s.byKey.end())
        return AssetHandle{it->second};
    // Första missen per nyckel loggas — felstavning/raderad asset ska
    // synas i loggen, inte bara som en knapp utan bild.
    if (s.loggedMisses.insert(k).second)
        gfx::log("assets: '%s/%s' finns inte%s", list.c_str(), key.c_str(),
                 s.loaded ? "" : " (inga assets laddade)");
    return {};
}

float assetSizeClass(AssetHandle h)
{
    std::uint32_t i = 0;
    const AssetStore* s = storeFor(h, i);
    return s ? s->sizeClass[i] : 0.0f;
}

namespace detail {

bool assetBytes(AssetHandle h, const std::uint8_t** data, std::size_t* size)
{
    std::uint32_t i = 0;
    const AssetStore* s = storeFor(h, i);
    if (!s)
        return false;
    const glzassets::EntryView& e = s->file.entries()[s->entryIdx[i]];
    *data = s->file.data(e);
    *size = e.dataSize;
    return true;
}

AssetHandle builtinAsset(const std::string& list, const std::string& key)
{
    AssetStore& s = builtinStore();
    const std::string k = list + '\0' + key;
    auto it = s.byKey.find(k);
    if (it != s.byKey.end())
        return AssetHandle{kBuiltinBit | it->second};
    if (s.loggedMisses.insert(k).second)
        gfx::log("assets: ramverkets '%s/%s' finns inte", list.c_str(),
                 key.c_str());
    return {};
}

std::uint64_t assetGeneration()
{
    return store().generation;
}

} // namespace detail
} // namespace glaze
