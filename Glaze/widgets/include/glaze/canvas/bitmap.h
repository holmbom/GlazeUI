#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

#include <gfx/color.h>
#include <gfx/image.h>

// glaze::Bitmap — EN BILD UR PIXLAR. Appens egna
// RGBA-pixlar: en kartplatta från en server, en avkodad PNG, en heatmap som
// räknas fram. Ritytan visar den med `Shape::image`, liksom
// varje bildbärande widget (`Picture::setImg`, knapparna,
// trädets och tabellernas rader, menyposter, programikonen — genom
// `ImageSource`); fönstret laddar upp den till GPU:n FÖRST när den ritas,
// och sedan bara det som ändrats.
//
//     auto platta = glaze::Bitmap::decode(png.data(), png.size());   // valfri tråd
//     platta.setPixels(0, 0, 16, 16, rad.data());   // bara rektangeln laddas upp igen
//     platta.setFilter(gfx::ImageFilter::Nearest);  // förvalet är Linear
//
// ETT HANDTAG, inte ett värde: kopior DELAR pixlarna (en platta i två lager
// är EN bild och EN textur per fönster). Pixlarna lever så länge något
// handtag gör det; fönstrens texturer släpps i början av nästa frame efter
// det sista handtaget.
//
// PIXLARNA är RGBA8 med RAK alfa, rad 0 överst och täta rader (w × 4 byte)
// — gfx/image.h:s form. Texturen i ett fönster följer med VARJE ändring
// genom en ÄNDRINGSLOGG: `setPixels` antecknar sin rektangel, och nästa
// ritning i varje fönster laddar upp de rektanglar som tillkommit sedan
// fönstret senast laddade upp — två fönster i olika takt får båda rätt
// pixlar. Blir loggen lång laddas allt upp i stället (`kMaxBitmapChanges`).
//
// TRÅDARNA: `decode` och konstruktorerna går att köra i vilken tråd som
// helst (en arbetartråd avkodar plattor och lämnar dem till UI-tråden med
// `glaze::app().post`). Ändringar och ritning sker i UI-tråden. Ändrar appen
// pixlarna i en bild som redan syns ska den invalidera det som ritar den —
// bilden är data, inte en widget. En bild med `keepPixels() == false` släpper
// sina pixlar i UI-tråden; ingen annan tråd får då läsa dem.
//
// PIXLARNA SOM SLÄPPS: `setKeepPixels(false)` släpper CPU-kopian i
// slutet av det pumpvarv där bilden första gången laddats upp HEL och
// AKTUELL i något fönster — varje fönster som ritar den i samma varv hinner
// få den. Efter släppet ÄRLIGT:
//   * `pixels()` ger nullptr; `valid()`, `width()` och `height()` står kvar.
//     `setPixels` och `fill` loggar och gör ingenting, och
//     `setKeepPixels(true)` loggar (pixlarna kommer inte tillbaka).
//   * Fönstren som har texturen ritar den så länge något handtag lever, och
//     budgetens LRU vräker den aldrig (den går inte att ladda upp igen) — den
//     räknas i budgeten, och loggas när taket inte hålls.
//   * Ett fönster UTAN textur ritar INGENTING för bilden och loggar en gång:
//     ett nytt fönster, en rityta som flyttats till ett flytande
//     dockningsfönster, ett fönster som stängts och öppnats igen. Ett fönster
//     som låg en version efter står kvar på sin version.
//   * Ingen OS-ikon kan göras av bilden (som en RenderTexture), och
//     `Draw::glyphResampled` ritar den med GPU:ns filter.
// Mönstret: plattor i EN rityta i ett fönster som inte stängs och öppnas
// igen, och flaggan satt före posten till UI-tråden.
//
// Regel 8: pixlar som räknas fram i drift har ingen `.glazeui`-form; en
// bild ur appens assets heter `glaze::img(...)` och är redan uttryckbar.

namespace glaze {

namespace detail {
struct BitmapData;   // src/canvas/image_data.h
}

// Hur många ändringar en bild antecknar innan loggen ger upp och nästa
// ritning laddar upp hela bilden.
inline constexpr std::size_t kMaxBitmapChanges = 32;

class Bitmap {
public:
    // Ingen bild: `valid()` falskt, ritas inte.
    Bitmap() = default;

    // w × h genomskinliga pixlar.
    Bitmap(int w, int h);

    // w × h pixlar kopierade ur `rgba` (`stride` = byte mellan två rader,
    // 0 = täta rader). Mått ≤ 0, en stride som inte rymmer en rad eller en
    // bild som inte får plats i minnet ger ingen bild + logg.
    Bitmap(int w, int h, const std::uint8_t* rgba, std::size_t stride = 0);

    // PNG-data → bild, genom `gfx::decodeImage` — BARA PNG på alla
    // plattformar; annan avkodning är appens, och
    // pixlarna kommer in genom konstruktorn ovan. TRÅDSÄKER. Trasig data ger
    // ingen bild + logg.
    static Bitmap decode(const void* data, std::size_t size);

    bool valid() const { return d_ != nullptr; }
    int  width() const;
    int  height() const;

    // Pixlarna, rad 0 överst, w × 4 byte per rad. nullptr utan bild — och
    // efter släppet (`setKeepPixels(false)`, ovan).
    const std::uint8_t* pixels() const;

    // Skriver rektangeln (x, y, w, h) ur `rgba` (`stride` som ovan) och
    // antecknar den: nästa ritning laddar upp BARA den. Utanför bilden, tom
    // eller utan pixlar ⇒ logg och ingenting skrivs.
    void setPixels(int x, int y, int w, int h, const std::uint8_t* rgba,
                   std::size_t stride = 0);

    // Hela bilden i en färg (rak alfa).
    void fill(gfx::Color c);

    // Samplingen när bilden ritas skalad (gfx/image.h): Linear som förval,
    // Nearest för det som ska förbli kantigt. Gäller nästa ritning. Texturen
    // har en MIP-KEDJA, så filtret gäller också mellan nivåerna:
    // Linear är MJUKT (trilinjärt), Nearest SKARPT (närmaste pixel och
    // närmaste nivå — pixlarna syns inzoomat, ingen moiré utzoomat).
    //
    // STÖRRE ÄN GPU:NS STÖRSTA TEXTUR (`gfx::Renderer::
    // maxTextureSize` — 16384 på de flesta) delar fönstret bilden i RUTOR,
    // så en Bitmap av valfri storlek ritas på ritytan (`Shape::image`,
    // `Painter::drawImage`); en widget och ett draperat lager ritar bara en
    // bild som ryms i en textur (loggas en gång).
    gfx::ImageFilter filter() const;
    void             setFilter(gfx::ImageFilter f);

    // CPU-KOPIAN: false släpper pixlarna efter första fulla
    // uppladdningen (kontraktet ovan). Förval true. Delas av kopiorna.
    void setKeepPixels(bool keep);
    bool keepPixels() const;

private:
    std::shared_ptr<detail::BitmapData> d_;
    friend class ImageCache;    // fönstrets texturer (src/image_cache.h)
    friend class ImageSource;   // ritytans "samma bild" (canvas/shape.h)
};

} // namespace glaze
