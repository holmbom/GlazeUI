#pragma once
// INTERN header — bildkällornas delade data: det som
// ett `glaze::Bitmap`- eller `glaze::RenderTexture`-handtag pekar på, och
// som fönstrens `ImageCache` läser när den laddar upp eller målar. Ingår
// inte i publika API:t; bara src/canvas/*.cpp och src/image_cache.cpp drar
// in den.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gfx/color.h>
#include <gfx/image.h>

#include "glaze/canvas/bitmap.h"
#include "glaze/canvas/render_texture.h"

namespace glaze::detail {

// En render-texturs mått, målning och version. `version` stegas av
// paint/repaint; ett fönsters mål målas om när dess version är en annan.
struct RenderTextureData {
    std::uint64_t          id = 0;   // cachens nyckel, återanvänds aldrig
    int                    w = 0, h = 0;
    RenderTexture::PaintFn fn;
    gfx::Color             clear{0, 0, 0, 0};
    gfx::ImageFilter       filter  = gfx::ImageFilter::Linear;
    std::uint64_t          version = 0;
};

// En bilds pixlar och ändringslogg. `id` är cachens NYCKEL och återanvänds
// aldrig — en ny bild på en gammal adress kan alltså inte ta en död bilds
// textur.
struct BitmapData {
    std::uint64_t             id = 0;
    int                       w = 0, h = 0;
    std::vector<std::uint8_t> px;   // RGBA8, rak alfa, täta rader
    gfx::ImageFilter          filter = gfx::ImageFilter::Linear;

    // PIXLARNA SOM SLÄPPS (`Bitmap::setKeepPixels(false)`): när en
    // fönstercache har bilden HEL och AKTUELL köas den (`releaseQueued`, med
    // den uppladdade versionen) och släpps i slutet av pumpvarvet
    // (`released` — `px` tom, loggen tömd). `missLogged`: ett fönster utan
    // textur, eller en version efter, har loggat en gång.
    bool keepPixels    = true;
    bool released      = false;
    bool releaseQueued = false;
    bool missLogged    = false;

    // ÄNDRINGSLOGGEN. `version` stegas vid varje ändring (0 = som skapad).
    // `changes` bär varje ändring med version > `base`, i ordning — en
    // textur uppladdad vid version v kan alltså komma ikapp med bara
    // rektanglarna om v ≥ base, och behöver hela bilden annars.
    struct Change {
        std::uint64_t version;
        int           x, y, w, h;
    };
    std::uint64_t       version = 0;
    std::uint64_t       base    = 0;
    std::vector<Change> changes;

    // Antecknar en ändrad rektangel. En ändring av HELA bilden tömmer
    // loggen (allt äldre behöver hela bilden ändå), och en full logg ger
    // upp sin äldre halva.
    void markChanged(int x, int y, int cw, int ch)
    {
        ++version;
        if (x == 0 && y == 0 && cw == w && ch == h) {
            changes.clear();
            base = version;
            return;
        }
        changes.push_back(Change{version, x, y, cw, ch});
        if (changes.size() > kMaxBitmapChanges) {
            const std::size_t drop = changes.size() / 2;
            base = changes[drop - 1].version;
            changes.erase(changes.begin(),
                          changes.begin() + static_cast<std::ptrdiff_t>(drop));
        }
    }
};

// SLÄPPKÖN (UI-tråden). `queuePixelRelease` köar bilden vid den
// uppladdade versionen (`ImageCache::get`); `releaseQueuedPixels` — App:s
// pump, direkt efter varvets ritloop, så varje fönster som ritar bilden i
// samma varv hinner få den — släpper pixlarna i de köade bilder som
// fortfarande har den versionen och `keepPixels` false. De andra får
// `releaseQueued` nollat, så nästa uppladdning köar dem igen.
void queuePixelRelease(const std::shared_ptr<BitmapData>& d,
                       std::uint64_t version);
void releaseQueuedPixels();

} // namespace glaze::detail
