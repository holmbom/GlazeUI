// Ikonens ramar till OS:et (fönstret och systemfältet) — kontraktet står i
// window_icon.h.

#include "window_icon.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <map>
#include <tuple>

#include <gfx/image.h>
#include <gfx/platform.h>

#include "glaze/app.h"     // detail::osIconColors — standardikonens färger
#include "glaze_assets/image.h"
#include "image_cache.h"   // detail::assetBytes / assetGeneration / stockBitmap

namespace glaze::detail {
namespace {

// Källans pixlar som en glzassets-bild — tom när källan är ogiltig, bilden
// trasig eller pixlarna bara finns på GPU:n (en RenderTexture, en Bitmap som
// släppt sina).
glzassets::Rgba avkoda(const ImageSource& s)
{
    glzassets::Rgba ut;
    switch (s.kind()) {
    case ImageSource::Kind::Asset: {
        const std::uint8_t* data = nullptr;
        std::size_t         size = 0;
        if (!s.valid() || !assetBytes(s.asset(), &data, &size) ||
            !gfx::decodeImage(data, size, ut.px, ut.width, ut.height))
            return glzassets::Rgba{};
        return ut;
    }
    case ImageSource::Kind::Bitmap: {
        // `decodeImage`s form rakt av: RGBA8, rak alfa, rad 0 överst och
        // täta rader — de LEVANDE pixlarna, i det skick bilden har nu.
        const Bitmap& b = s.bitmap();
        if (!b.valid())
            return glzassets::Rgba{};
        if (!b.pixels()) {
            // UTAN PIXLAR (`setKeepPixels(false)`): som en
            // RenderTexture — tom, EN rad per process.
            static std::atomic<bool> loggad{false};
            if (!loggad.exchange(true))
                gfx::log("OS-ikonen: bilden har släppt sina pixlar "
                         "(setKeepPixels(false)) och kan inte bli en OS-ikon — "
                         "fönstret får ingen ikon och systemfältet förvalet");
            return glzassets::Rgba{};
        }
        const std::size_t n = static_cast<std::size_t>(b.width()) *
                              static_cast<std::size_t>(b.height()) * 4;
        ut.width  = b.width();
        ut.height = b.height();
        ut.px.assign(b.pixels(), b.pixels() + n);
        return ut;
    }
    case ImageSource::Kind::Texture: {
        // AVVISAD (window_icon.h): en återläsning kräver en renderer utanför
        // en frame. EN rad per process — en animation i systemfältet hade
        // annars loggat varje bildruta.
        static std::atomic<bool> loggad{false};
        if (s.valid() && !loggad.exchange(true))
            gfx::log("OS-ikonen: en RenderTexture kan inte bli en OS-ikon "
                     "(pixlarna finns bara på GPU:n) — fönstret får ingen "
                     "ikon och systemfältet förvalet; ge en Bitmap eller en "
                     "asset");
        return glzassets::Rgba{};
    }
    case ImageSource::Kind::Stock: {
        // STANDARDIKONEN rastreras i den STÖRSTA ramen och skalas ned som en
        // asset, i appens färger i OS:ets läge (`osIconColors`).
        const int    sida = std::end(glzassets::kIconSizes)[-1];
        const Bitmap b    =
            stockBitmap(s.stock(), sida, s.stockStyle(), osIconColors());
        if (!b.valid() || !b.pixels())
            return glzassets::Rgba{};
        ut.width  = b.width();
        ut.height = b.height();
        ut.px.assign(b.pixels(), b.pixels() + static_cast<std::size_t>(sida) *
                                                 static_cast<std::size_t>(sida) * 4);
        return ut;
    }
    case ImageSource::Kind::None:
        break;
    }
    return glzassets::Rgba{};
}

// Ramarna ur två källor, utan cache.
std::vector<gfx::IconImage> bygg(const ImageSource& large,
                                 const ImageSource& smallIcon, int maxSide)
{
    // Källorna i valregelns form: sidan är den LÄNGSTA (den passas in).
    const glzassets::Rgba källor[] = {avkoda(large), avkoda(smallIcon)};
    std::vector<int>      sidor;
    for (const glzassets::Rgba& k : källor)
        sidor.push_back(k.empty() ? 0 : std::max(k.width, k.height));

    std::vector<gfx::IconImage> ramar;
    for (const int sida : glzassets::kIconSizes) {
        if (sida > maxSide)
            break;   // storlekarna är stigande — resten ligger över taket
        const int i = glzassets::pickIconSource(sidor, sida);
        if (i < 0)
            break;   // ingen källa alls — ingen ram i någon storlek
        glzassets::Rgba ram =
            glzassets::fitSquare(källor[static_cast<std::size_t>(i)], sida);
        if (ram.empty())
            continue;
        gfx::IconImage bild;
        bild.width  = ram.width;
        bild.height = ram.height;
        bild.rgba   = std::move(ram.px);
        ramar.push_back(std::move(bild));
    }
    return ramar;
}

using Nyckel = std::tuple<std::uint32_t, std::uint32_t, std::uint64_t, int>;

std::map<Nyckel, std::vector<gfx::IconImage>>& ramCache()
{
    static std::map<Nyckel, std::vector<gfx::IconImage>> cache;
    return cache;
}

} // namespace

std::vector<gfx::IconImage> iconFrames(const ImageSource& large,
                                       const ImageSource& smallIcon,
                                       int maxSide)
{
    // Bara assets och ogiltiga källor går i cachen — en Bitmap ändras, och
    // dess ramar räknas ur pixlarna varje gång (window_icon.h).
    const auto cachebar = [](const ImageSource& s) {
        return !s.valid() || s.kind() == ImageSource::Kind::Asset;
    };
    if (!cachebar(large) || !cachebar(smallIcon))
        return bygg(large, smallIcon, maxSide);

    // Ramverkets handtag (bit 31) och ogiltiga källor beror inte på appens
    // assets.bin — en omladdning ska inte bygga förvalets ramar på nytt.
    const auto appens = [](const ImageSource& s) {
        return s.valid() && (s.asset().id & 0x80000000u) == 0;
    };
    const Nyckel nyckel{large.valid() ? large.asset().id : 0u,
                        smallIcon.valid() ? smallIcon.asset().id : 0u,
                        appens(large) || appens(smallIcon) ? assetGeneration() : 0,
                        maxSide};
    auto&      cache = ramCache();
    const auto it    = cache.find(nyckel);
    if (it != cache.end())
        return it->second;
    return cache.emplace(nyckel, bygg(large, smallIcon, maxSide)).first->second;
}

std::size_t iconFrameCacheSize()
{
    return ramCache().size();
}

AssetHandle defaultIconLarge()
{
    static const AssetHandle h = builtinAsset("glaze", "icon");
    return h;
}

AssetHandle defaultIconSmall()
{
    static const AssetHandle h = builtinAsset("glaze", "icon16");
    return h;
}

std::pair<ImageSource, ImageSource> effectiveIcons(const ImageSource& icon,
                                                   const ImageSource& smallIcon)
{
    if (icon.valid())
        return {icon, smallIcon};
    return {ImageSource(defaultIconLarge()), ImageSource(defaultIconSmall())};
}

gfx::WindowIcon osIcon(const ImageSource& icon, const ImageSource& smallIcon,
                       int maxSide)
{
    const auto [stor, liten] = effectiveIcons(icon, smallIcon);
    gfx::WindowIcon ut;
    ut.images       = iconFrames(stor, liten, maxSide);
    ut.programFirst = !icon.valid();
    return ut;
}

} // namespace glaze::detail
