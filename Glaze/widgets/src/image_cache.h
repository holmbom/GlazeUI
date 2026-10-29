#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gfx/image.h"
#include "gfx/renderer.h"
#include "glaze/assets.h"
#include "glaze/canvas/image_source.h"
#include "glaze/theme.h"   // Theme::Colors — standardikonens färger

// INTERN header — ingår inte i publika API:t (till skillnad från
// glaze/draw.h, som är publik med flit).
//
// ImageCache — per-FÖNSTER texturcache: handtag → gfx::Image. Lever bredvid
// font_ i AppWindow och följer exakt samma livstidsregel: texturerna hör
// till fönstrets renderer, cachen MÅSTE nollas före win_->shutdown()
// (App::executeClose gör det, images_ före font_). Uppladdningen är LAT
// (första get).
//
// TVÅ SORTERS POSTER:
//   ASSETS (AssetHandle) — App äger råbytes, cachen avkodar en gång och
//     behåller texturen fönstrets livstid. Små, få, aldrig vräkta.
//   BILDER UR PIXLAR (glaze::Bitmap) — appens egna,
//     potentiellt tusentals plattor. De ligger under en BYTE-BUDGET med LRU
//     (`App::setCanvasMemoryBudget`, förval 256 MB) och en UPPLADDNINGS-
//     BUDGET per frame (`App::setCanvasUploadBudget`, förval 16 MiB — båda
//     lämnas till `trim`), och följer bildens ändringslogg: en ändrad bild
//     laddar upp BARA sina ändrade rektanglar.
//   RENDER-TEXTURER (glaze::RenderTexture) — ett gfx::RenderTarget
//     per textur och fönster, MÅLAT LATAT: första gången texturen ritas och
//     igen när dess version ändrats. Samma budget och LRU som bilderna — en
//     vräkt textur målas om nästa gång den ritas.
//   OMRÄKNADE BILDER UR PIXLAR — en Bitmap skarpt
//     nedskalad på CPU till en viss storlek (`getResampled`, programikonens
//     väg). En textur per (bild-id, w, h), räknad om när bildens version
//     ändrats. Samma budget, uppladdningsbudget och LRU som bilderna — en
//     programikon i flera storlekar eller skalor lämnar inte gamla storlekar
//     kvar för evigt.
//
// WIDGETARNA tar ett `ImageSource` (asset, Bitmap eller RenderTexture) och
// slår upp det med `get(källa, ctx)` — EN switch till de tre `get` nedan. En
// STANDARDIKON blir där en Bitmap ur `detail::stockBitmap`, i
// klassens storlek gånger fönstrets skala och temats textfärg.
//
// VRÄKNINGEN sker BARA i `trim()`, som AppWindow kör ÖVERST i renderContent
// — aldrig mitt i en frame, där en batchad ritning kan peka på texturen.
// Det som ritades i framen som just tog slut vräks aldrig, inte ens över
// budgeten (det loggas en gång): att släppa en synlig bild hade bara gett en
// ny uppladdning i nästa frame.
//
// KONTONA (`Canvas::setMemoryBudget`): budgeten är PER RITYTA. En
// rityta som ritar öppnar sitt konto (`beginAccount` — scenens id, scenen
// som livstecken och dess budget; `detail::ImageAccount` i
// canvas/scene.h), och varje bild, render-textur och omräknad bild som
// hämtas medan det är öppet räknas på det — på den rityta som RITADE den
// senast. Det som ritas utanför ritytorna (widgetarna) räknas på FÖNSTRET,
// konto 0, med appens förval. `trim` vräker per konto med LRU: en post vräks
// bara medan dess konto ligger över sin budget. Ett dött kontos poster
// flyttas till fönstret. Draperingarna (canvas/drape.h) räknas mot samma
// budget genom `setAccountExtra`.
//
// BILDER SOM SLÄPPT SINA PIXLAR (`Bitmap::setKeepPixels(false)`):
// `get` köar bilden när texturen är hel och aktuell; utan textur ger den
// nullptr och loggar en gång (en NEGATIV post — en tom buffert hade blivit en
// svart textur). En släppt eller köad bild — och dess omräkningar — vräks
// ALDRIG av LRU:n: den går inte att ladda upp igen. Den räknas i budgeten.

namespace glaze {

class Bitmap;
class RenderTexture;
struct Ctx;

namespace detail {
// Råbytes för ett handtag ur asset-storen (assets.cpp) — intern söm
// mellan storen (bytes) och cachen (texturer). false vid ogiltigt id.
// Routar RAMVERKETS handtag (bit 31) till den inbäddade butiken.
bool assetBytes(AssetHandle h, const std::uint8_t** data, std::size_t* size);

// RAMVERKETS egna bilder — ur den inbäddade
// framework_assets.bin (assets_builtin.cpp, `kFrameworkAssetsBin`), aldrig
// ur appens assets.bin. Handtaget har bit 31 satt (`0x80000000 | (index+1)`),
// så det aldrig krockar med appens id:n och överlever en ny loadAssets.
// Butiken öppnas LATT vid första uppslaget. Ogiltigt handtag vid miss (en
// loggrad första gången) — listan "glaze" med "icon" (256×256) och
// "icon16" (den förenklade lilla).
AssetHandle builtinAsset(const std::string& list, const std::string& key);

// Asset-storens GENERATION — stegas varje gång loadAssets ersätter appens
// butik, så samma handtags-id kan peka på en annan bild efteråt. En cache
// nycklad på id (programikonens ramar, window_icon.cpp) tar med
// generationen i nyckeln. Ramverkets handtag påverkas aldrig av den.
std::uint64_t assetGeneration();

// STANDARDIKONEN (stock_icon.cpp) rastrerad i `px` × `px` FYSISKA
// px i stilen `style`, färgad ur paletten `colors` (konturen `text`,
// färgvariantens accenter `stock*`) — EN bild per (ikon, px, stil, färgerna
// den använder) i processen, så fönstrens texturer (`get(Bitmap)`) delar den.
// Rastreringen är stock_icon.cpp:s; `px` ≤ 0 och en ikon utanför uppräkningen
// ger ingen bild. UI-tråden.
Bitmap stockBitmap(StockIcon icon, int px, StockIconStyle style,
                   const Theme::Colors& colors);

struct BitmapData;
struct RenderTextureData;
} // namespace detail

// EN RUTA ur en bild större än GPU:ns största textur
// (`ImageCache::tiles`). `u0`–`v1` är rutans del av HELA bilden (uv 0–1, rad 0
// överst) och `tu0`–`tv1` samma del i rutans egen textur: texturen har en
// kantpixel från grannen på varje inre sida, så den linjära samplingen över
// en rutgräns läser grannens pixel i stället för att ge en söm.
struct ImageTile {
    gfx::Image* image = nullptr;
    float       u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    float       tu0 = 0.0f, tv0 = 0.0f, tu1 = 1.0f, tv1 = 1.0f;
};

class ImageCache {
public:
    explicit ImageCache(gfx::Renderer& r) : r_(&r) {}

    // Avkodad bild för handtaget — nullptr vid ogiltigt handtag eller
    // trasig bilddata. NEGATIV cachning: ett misslyckande lagras som
    // nullptr-post så en trasig asset inte blir en per-frame-kostnad
    // (avkodningsförsök varje repaint). Loggning sköts av loadImage.
    gfx::Image* get(AssetHandle h);

    // WIDGETARNAS UPPSLAG: källans textur i det här fönstret — assetens
    // (`get(AssetHandle)`), bildens (`get(Bitmap)`, under budgeten) eller
    // render-texturens (`get(RenderTexture, Ctx)`, MÅLAD nu om den är ny
    // eller ändrad — alltså bara inom en frame). nullptr utan bild och i de
    // tre uppslagens egna fall.
    gfx::Image* get(const ImageSource& s, const Ctx& c);

    // SKARP NEDSKALNING: källans bild omräknad på
    // CPU till EXAKT `w` × `h` fysiska px (`glzassets::resizeRgba` — ytans
    // medelvärde nedåt, bilinjärt uppåt) och uppladdad en gång, cachad per
    // (id, w, h). Skälet: GPU:ns linjära sampling läser fyra källpixlar per
    // målpixel, så en 256-bild ritad i 16 px hoppar över de flesta och blir
    // grynig — en ikon i namnlisten ska se ut som OS:ets egen. Ritas 1:1
    // (`Draw::glyphResampled` fäster recten på hela pixlar).
    //   asset         avkodad en gång, negativ cachning som `get`
    //   Bitmap        räknad ur bildens LEVANDE pixlar, om när versionen
    //                 ändrats; under budgeten (en ny storlek som inte ryms i
    //                 framen väntar, en ändrad ritas med sitt förra innehåll)
    //   RenderTexture nullptr — pixlarna finns bara på GPU:n; `Draw::
    //                 glyphResampled` ritar den med `glyph` i stället
    // nullptr också vid ogiltig källa, trasig bild eller w/h ≤ 0.
    gfx::Image* getResampled(const ImageSource& s, int w, int height);

    // Källans NATURLIGA mått i fysiska px — aldrig uppladdat (valregeln
    // behöver källornas sidor, inte deras texturer). En asset avkodas på CPU
    // och pixlarna sparas, så `getResampled` skalar ur dem; en Bitmap och en
    // RenderTexture svarar med sina egna mått. false vid ogiltig källa eller
    // trasig bild.
    bool sourceSize(const ImageSource& s, int& w, int& height);

    // Bildens textur i det här fönstret, med bildens filter och en MIP-KEDJA
    // — uppladdad eller ikapp med ändringsloggen. nullptr utan bild,
    // när texturen inte gick att skapa, eller när en NY bild inte ryms i
    // framens uppladdningsbudget (då är `uploadsDeferred()` sann); en ÄNDRAD
    // bild som inte ryms ritas med sitt förra innehåll en frame till. Också
    // nullptr för en bild större än GPU:ns största textur — den är RUTOR
    // (`tiles`), och en väg som bara tar en textur ritar den inte (loggas en
    // gång per bild och fönster).
    gfx::Image* get(const Bitmap& b);

    // RUTORNA: en bild större än `Renderer::maxTextureSize` delas
    // i rutor, var och en en egen textur med kedja och bildens filter, under
    // samma budget, uppladdningsbudget, ändringslogg och LRU som en hel bild
    // (posten är EN, med alla rutorna). Ritytans bilder (`Shape::image`,
    // `Painter::drawImage`) ritar varje platta som en fyrhörning per ruta —
    // `splitImageQuads` (canvas/build.h). nullptr för en bild som ryms i en
    // textur (då gäller `get`) och i `get`s egna fall.
    const std::vector<ImageTile>* tiles(const Bitmap& b);

    // Render-texturens bild i det här fönstret, med texturens filter —
    // MÅLAD nu om den är ny eller dess version ändrats: `pushTarget`, dess
    // funktion med `c` vars box är målets yta, `popTarget`. Anropas INOM en
    // frame (målningen ritar) och med fönstrets Ctx. nullptr utan textur,
    // när målet inte gick att skapa, och för en textur som ritar SIG SJÄLV
    // i sin egen målning (loggas en gång — rekursionen bryts).
    gfx::Image* get(const RenderTexture& t, const Ctx& c);

    // Framens början: släpp döda bilder (inget handtag kvar), flytta döda
    // kontons poster till fönstret och, för varje konto över sin budget
    // (kontots, eller `defaultBudget` när det är 0 — och alltid för
    // fönstret), vräk dess poster som legat oritade längst. Framen får sedan
    // ladda upp `uploadBudget` byte (`App::canvasUploadBudget`) — det som
    // inte ryms väntar på NÄSTA frame (`uploadsDeferred`, och AppWindow
    // begär den), så en vy full av nya plattor fylls på över några frames i
    // stället för att frysa en. Framens FÖRSTA uppladdning går alltid
    // igenom, så en bild större än budgeten också kommer upp.
    void trim(std::size_t defaultBudget, std::size_t uploadBudget);

    // KONTONA (filhuvudet). `beginAccount` öppnar kontot `id` (≠ 0) med
    // livstecknet och budgeten (0 = förvalet) och svarar med det som var
    // öppet; `endAccount` öppnar det igen. `accountBytes` är kontots
    // bildbyte. `setAccountExtra` är draperingarnas byte på ett öppnat konto
    // — `trim` jämför bilderna PLUS dem mot budgeten men vräker bara bilder.
    std::uint64_t beginAccount(std::uint64_t id, std::weak_ptr<const void> life,
                               std::size_t budget);
    void          endAccount(std::uint64_t previous) { account_ = previous; }
    std::size_t   accountBytes(std::uint64_t id) const;
    void          setAccountExtra(std::uint64_t id, std::size_t bytes);

    // Fick en bild vänta på nästa frame för budgeten? Sann till nästa trim().
    bool uploadsDeferred() const { return deferred_; }

    // FRAMENS UPPLADDNINGSBUDGET DELAD MED RITYTANS FÖREMÅL —
    // meshar, instanser och punkter strömmar i bitar (canvas/residency.h).
    // `uploadRoom` är hur mycket som får laddas upp NU, i byte: det som är
    // kvar av framens budget, eller — när ingenting laddats upp i framen —
    // hela budgeten och minst `minimum` (framens första uppladdning går
    // alltid igenom, så ett element större än budgeten också kommer upp).
    // `spendUpload` antecknar det som laddades upp och `deferUpload` att
    // något väntar på nästa frame (`uploadsDeferred` — fönstret begär den).
    std::size_t uploadRoom(std::size_t minimum) const;
    void        spendUpload(std::size_t bytes) { frameUploaded_ += bytes; }
    void        deferUpload() { deferred_ = true; }

    // INNEHÅLLETS VERSION i fönstrets textur —
    // draperingens nyckel (canvas/drape.h): bildens version när texturen
    // senast kom ikapp, respektive render-texturens målade version, PLUS ETT;
    // 0 utan textur (en ny bild som väntar på budgeten, en ogiltig). Ettan är
    // bärande: en ny bild har version 0, och utan den hade texturens ankomst
    // inte ändrat svaret. En fråga —
    // ingen uppladdning och ingen målning.
    std::uint64_t contentVersion(const Bitmap& b) const;
    std::uint64_t contentVersion(const RenderTexture& t) const;
    // INNEHÅLLETS GENERATION — stegas varje gång det någon av
    // frågorna ovan (eller en assets textur) svarar kan ha ändrats: en textur
    // skapas, kommer ikapp med sin bild, vräks eller släpps, eller en
    // render-textur målas. Draperingens markstämpel läser den i stället för
    // varje bilds version.
    std::uint64_t contentGeneration() const { return contentGeneration_; }

    // Bildernas, render-texturernas och de omräknade bildernas byte och
    // antal — budgeten räknar dem (totalen över alla konton).
    std::size_t canvasBytes() const { return bytes_; }
    std::size_t canvasCount() const
    {
        return bitmaps_.size() + targets_.size() + bitmapResampled_.size();
    }

    void clear()
    {
        map_.clear();
        resampled_.clear();
        sources_.clear();
        bitmaps_.clear();
        targets_.clear();
        bitmapResampled_.clear();
        accounts_.clear();
        bytes_ = 0;
        ++contentGeneration_;
    }

private:
    // Får `need` byte laddas upp i den här framen? Annars antecknas att
    // något väntar.
    bool mayUpload(std::size_t need);

    // EN väg för budgetens räkning: postens byte flyttas till
    // kontot `account` med storleken `bytes`, i totalen och på kontona — vid
    // en ny post, en ny storlek, en ritning av en annan rityta och (med
    // bytes 0) en borttagning.
    void book(std::uint64_t& entryAccount, std::size_t& entryBytes,
              std::uint64_t account, std::size_t bytes);

    // Assetens respektive bildens omräkning (`getResampled`s två grenar).
    gfx::Image* resampledAsset(AssetHandle h, int w, int height);
    gfx::Image* resampledBitmap(const Bitmap& b, int w, int height);

    gfx::Renderer* r_;
    std::unordered_map<std::uint32_t, std::unique_ptr<gfx::Image>> map_;

    // SKARP NEDSKALNING (getResampled/sourceSize): de avkodade källorna per
    // id (tom bild = negativ post) och de omräknade texturerna per
    // (id, w, h). Få och små — programikonen i en eller två storlekar.
    // Källan är RAK RGBA8, rad 0 överst (`gfx::decodeImage`s form); en egen
    // struct och inte `glzassets::Rgba`, för headern ingår i grindar som
    // inte ser glaze_assets.
    struct Source {
        int                       w = 0, h = 0;   // 0 = negativ post
        std::vector<std::uint8_t> px;
    };
    const Source* source(AssetHandle h);
    std::unordered_map<std::uint32_t, Source> sources_;
    std::map<std::tuple<std::uint32_t, int, int>, std::unique_ptr<gfx::Image>>
        resampled_;

    // EN TEXTUR PER RUTA: en bild som ryms i `maxTextureSize` är EN
    // ruta över hela bilden, en större flera (`tileGrid`). `x`, `y`, `w`, `h`
    // är texturens rektangel i bildens px — kantpixlarna inräknade.
    struct TileTexture {
        std::unique_ptr<gfx::Image> image;
        int                         x = 0, y = 0, w = 0, h = 0;
    };
    struct BitmapEntry {
        std::weak_ptr<const detail::BitmapData> data;   // död ⇒ släpps i trim
        std::vector<TileTexture> textures;   // tom = negativ
        std::vector<ImageTile>   tiles;      // ritvägarnas vy, samma ordning
        std::uint64_t version   = 0;   // bildens version i texturen
        std::uint64_t lastFrame = 0;   // senaste frame bilden ritades
        std::size_t   bytes     = 0;   // texturernas pixlar (budgeten)
        std::uint64_t account   = 0;   // kontot som ritade den senast
        bool          tiledLogged = false;   // `get` på en delad bild
    };
    std::unordered_map<std::uint64_t, BitmapEntry> bitmaps_;   // id → post
    // Bildens post, uppladdad eller ikapp med ändringsloggen — `get(Bitmap)`
    // och `tiles` delar den. nullptr utan bild och när en NY bild väntar på
    // framens uppladdningsbudget.
    BitmapEntry* bitmapEntry(const Bitmap& b);

    struct TargetEntry {
        std::weak_ptr<const detail::RenderTextureData> data;
        std::unique_ptr<gfx::RenderTarget>             target;  // nullptr = negativ
        std::uint64_t version   = 0;       // målad version (0 = aldrig)
        std::uint64_t lastFrame = 0;
        std::size_t   bytes     = 0;
        std::uint64_t account   = 0;
        bool          painting  = false;   // rekursionsvakten
    };
    std::unordered_map<std::uint64_t, TargetEntry> targets_;   // id → post
    bool selfPaintLogged_ = false;

    // OMRÄKNADE BILDER UR PIXLAR — nyckeln (bild-id, w, h). Posten bär
    // bildens version i texturen och dör med bilden (`weak_ptr`), som
    // `BitmapEntry`, och räknas i samma budget och LRU.
    struct ResampledEntry {
        std::weak_ptr<const detail::BitmapData> data;   // död ⇒ släpps i trim
        std::unique_ptr<gfx::Image>             image;  // nullptr = negativ
        std::uint64_t version   = 0;   // bildens version i texturen
        std::uint64_t lastFrame = 0;
        std::size_t   bytes     = 0;
        std::uint64_t account   = 0;
    };
    std::map<std::tuple<std::uint64_t, int, int>, ResampledEntry>
        bitmapResampled_;

    // KONTONA — 0 är fönstret och har inget livstecken.
    struct Account {
        std::weak_ptr<const void> life;
        std::size_t               budget = 0;   // 0 = förvalet
        std::size_t               bytes  = 0;   // postenas byte
        std::size_t               extra  = 0;   // draperingarnas (setAccountExtra)
        bool                      overLogged = false;
    };
    std::unordered_map<std::uint64_t, Account> accounts_;
    std::uint64_t                              account_ = 0;   // det öppna

    std::uint64_t frame_         = 0;   // stegas av trim()
    std::size_t   uploadBudget_  = 0;   // framens budget, satt av trim()
    std::size_t   frameUploaded_ = 0;   // byte i den här framen
    std::size_t   bytes_         = 0;   // alla bilders texturer
    bool          deferred_      = false;
    std::uint64_t contentGeneration_ = 1;   // `contentGeneration`
};

} // namespace glaze
