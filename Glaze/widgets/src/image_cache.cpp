// ImageCache — lat uppladdning till fönstrets renderer: assets (avkodade
// en gång) och bilder ur pixlar (minnesbudget, LRU, uppladdningsbudget och
// ändringslogg), widgetarnas uppslag `get(ImageSource, Ctx)` och de
// omräknade bilderna ur pixlar, budgetens KONTON per rityta, bilderna som
// släpper sina pixlar och innehållets GENERATION (draperingens markstämpel).
// Livstidsregeln bor i App::executeClose (images_ före font_ före shutdown);
// kontraktet står i image_cache.h.

#include "image_cache.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "canvas/image_data.h"
#include "gfx/platform.h"
#include "glaze_assets/image.h"   // resizeRgba — getResampled
#include "glaze/theme.h"    // Theme::Colors::text — standardikonens färg
#include "glaze/widget.h"   // Ctx — render-texturens målning

namespace glaze {

gfx::Image* ImageCache::get(AssetHandle h)
{
    if (!h.valid())
        return nullptr;
    auto it = map_.find(h.id);
    if (it != map_.end())
        return it->second.get();   // även negativ post (nullptr)

    const std::uint8_t* data = nullptr;
    std::size_t         size = 0;
    std::unique_ptr<gfx::Image> img;
    if (detail::assetBytes(h, &data, &size)) {
        img = r_->loadImage(data, size);
        if (img && !img->valid())
            img.reset();           // trasig avkodning ⇒ negativ post
    }
    gfx::Image* raw = img.get();
    map_[h.id] = std::move(img);   // nullptr = negativ cachning
    ++contentGeneration_;          // assetens textur finns nu (eller aldrig)
    return raw;
}

gfx::Image* ImageCache::get(const ImageSource& s, const Ctx& c)
{
    switch (s.kind()) {
    case ImageSource::Kind::Asset:   return get(s.asset());
    case ImageSource::Kind::Bitmap:  return get(s.bitmap());
    case ImageSource::Kind::Texture: return get(s.texture(), c);
    case ImageSource::Kind::Stock:
        return get(detail::stockBitmap(
            s.stock(), static_cast<int>(std::lround(s.sizeClass() * c.scale)),
            s.stockStyle(), c.theme->colors));
    case ImageSource::Kind::None:    break;
    }
    return nullptr;
}

// ------------------------------------------------- skarp nedskalning

// Källan avkodas EN gång per handtag och fönster — `gfx::decodeImage` är
// samma avkodning som `loadImage` går genom, bara utan uppladdning.
const ImageCache::Source* ImageCache::source(AssetHandle h)
{
    if (!h.valid())
        return nullptr;
    auto it = sources_.find(h.id);
    if (it == sources_.end()) {
        Source              s;
        const std::uint8_t* data = nullptr;
        std::size_t         size = 0;
        if (!detail::assetBytes(h, &data, &size) ||
            !gfx::decodeImage(data, size, s.px, s.w, s.h)) {
            s = Source{};   // negativ post — försöks inte varje frame
        }
        it = sources_.emplace(h.id, std::move(s)).first;
    }
    return it->second.w > 0 && it->second.h > 0 ? &it->second : nullptr;
}

bool ImageCache::sourceSize(const ImageSource& src, int& w, int& height)
{
    switch (src.kind()) {
    case ImageSource::Kind::Asset: {
        const Source* s = source(src.asset());
        if (!s)
            return false;
        w      = s->w;
        height = s->h;
        return true;
    }
    // Måtten står i handtaget — ingen avkodning, ingen uppladdning.
    case ImageSource::Kind::Bitmap:
        if (!src.valid())
            return false;
        w      = src.bitmap().width();
        height = src.bitmap().height();
        return true;
    case ImageSource::Kind::Texture:
        if (!src.valid())
            return false;
        w      = src.texture().width();
        height = src.texture().height();
        return true;
    // En vektor har ingen egen upplösning: klassens sida, som en asset i sin
    // klass vid skala 1.
    case ImageSource::Kind::Stock:
        w = height = static_cast<int>(src.sizeClass());
        return true;
    case ImageSource::Kind::None:
        break;
    }
    return false;
}

gfx::Image* ImageCache::getResampled(const ImageSource& s, int w, int height)
{
    switch (s.kind()) {
    case ImageSource::Kind::Asset:  return resampledAsset(s.asset(), w, height);
    case ImageSource::Kind::Bitmap: return resampledBitmap(s.bitmap(), w, height);
    case ImageSource::Kind::Texture:   // pixlarna finns bara på GPU:n
    case ImageSource::Kind::Stock:     // rastreras i målets storlek redan —
                                       // `Draw::glyphResampled` går via `glyph`
    case ImageSource::Kind::None:
        break;
    }
    return nullptr;
}

gfx::Image* ImageCache::resampledAsset(AssetHandle h, int w, int height)
{
    if (!h.valid() || w <= 0 || height <= 0)
        return nullptr;
    const auto key = std::make_tuple(h.id, w, height);
    auto       it  = resampled_.find(key);
    if (it != resampled_.end())
        return it->second.get();   // även negativ post (nullptr)

    std::unique_ptr<gfx::Image> img;
    if (const Source* s = source(h)) {
        glzassets::Rgba src;
        src.width  = s->w;
        src.height = s->h;
        src.px     = s->px;
        const glzassets::Rgba ut = glzassets::resizeRgba(src, w, height);
        if (!ut.empty()) {
            img = r_->createImage(ut.width, ut.height, gfx::ImageFormat::Rgba8,
                                  ut.px.data(), 0);
            if (img && !img->valid())
                img.reset();
        }
    }
    gfx::Image* raw = img.get();
    resampled_[key] = std::move(img);   // nullptr = negativ cachning
    return raw;
}

// En BILD UR PIXLAR omräknad till w × h — ur de LEVANDE pixlarna, och om när
// bildens version ändrats (en asset ändras aldrig, en Bitmap gör det). Posten
// ligger i bildernas budget och LRU: en NY storlek som inte ryms i framens
// uppladdningsbudget väntar på nästa frame (nullptr nu, och `uploadsDeferred`
// ber fönstret om den), en ÄNDRAD bild ritas med sitt förra innehåll en
// frame till — det är redan på GPU:n.
gfx::Image* ImageCache::resampledBitmap(const Bitmap& b, int w, int height)
{
    const std::shared_ptr<detail::BitmapData>& d = b.d_;
    if (!d || w <= 0 || height <= 0)
        return nullptr;
    const auto key = std::make_tuple(d->id, w, height);
    auto       it  = bitmapResampled_.find(key);
    if (it != bitmapResampled_.end()) {
        it->second.lastFrame = frame_;
        book(it->second.account, it->second.bytes, account_, it->second.bytes);
        // En bild utan pixlar svarar med det den har.
        if (it->second.version == d->version || d->released)
            return it->second.image.get();   // även negativ post (nullptr)
    }
    if (d->released)
        return nullptr;   // ingenting att räkna om ur — `glyph` ritar den
    const std::size_t need = static_cast<std::size_t>(w) *
                             static_cast<std::size_t>(height) * 4;
    if (!mayUpload(need))
        return it != bitmapResampled_.end() ? it->second.image.get() : nullptr;

    glzassets::Rgba src;
    src.width  = d->w;
    src.height = d->h;
    src.px     = d->px;
    const glzassets::Rgba ut = glzassets::resizeRgba(src, w, height);

    if (it == bitmapResampled_.end()) {
        ResampledEntry ny;
        ny.data    = d;
        ny.account = account_;
        it         = bitmapResampled_.emplace(key, std::move(ny)).first;
    }
    ResampledEntry& e = it->second;
    e.lastFrame       = frame_;
    e.version         = d->version;   // också en negativ post: försöks igen
                                      // först när bilden ändras
    if (ut.empty()) {
        book(e.account, e.bytes, account_, 0);
        e.image.reset();
        return nullptr;
    }
    if (e.image) {
        // Samma storlek (den står i nyckeln) — bara pixlarna byts.
        r_->updateImage(*e.image, 0, 0, ut.width, ut.height, ut.px.data(), 0);
        return e.image.get();
    }
    e.image = r_->createImage(ut.width, ut.height, gfx::ImageFormat::Rgba8,
                              ut.px.data(), 0);
    if (e.image && !e.image->valid())
        e.image.reset();   // negativ post — försöks igen vid nästa version
    book(e.account, e.bytes, account_, e.image ? need : 0);
    return e.image.get();
}

// ------------------------------------------------------ bilder ur pixlar

// Framens FÖRSTA uppladdning går alltid igenom (också en större än
// budgeten); varje senare måste rymmas i det som är kvar. Det som är kvar
// räknas med `min`, så en första uppladdning över budgeten lämnar noll kvar
// i stället för att slå runt.
bool ImageCache::mayUpload(std::size_t need)
{
    if (need > uploadRoom(need)) {
        deferred_ = true;
        return false;
    }
    frameUploaded_ += need;
    return true;
}

std::size_t ImageCache::uploadRoom(std::size_t minimum) const
{
    if (frameUploaded_ == 0)
        return std::max(uploadBudget_, minimum);
    return uploadBudget_ - std::min(frameUploaded_, uploadBudget_);
}

namespace {

// En bild utan pixlar som ett fönster inte har, eller har en
// version efter: loggas EN gång per bild.
void missLog(detail::BitmapData& d, bool none)
{
    if (d.missLogged)
        return;
    d.missLogged = true;
    if (none)
        gfx::log("Bitmap: bilden (%d × %d) har släppt sina pixlar "
                 "(setKeepPixels(false)) och fönstret har ingen textur — den "
                 "ritas inte här; loggas en gång",
                 d.w, d.h);
    else
        gfx::log("Bitmap: bilden (%d × %d) har släppt sina pixlar "
                 "(setKeepPixels(false)) — fönstret står kvar på en äldre "
                 "version; loggas en gång",
                 d.w, d.h);
}

// RUTNÄTET: en bild som ryms i `max` är EN ruta över hela bilden. En
// större delas med steget `max − 2`, så varje ruta får en kantpixel från
// grannen på varje inre sida och ändå ryms. `x`–`h` är texturens rektangel i
// bildens px (kanterna inräknade), `view` rutans uv utan textur. Tom när
// ingen ruta ryms med sina kanter (`max` < 3).
struct TileRect {
    int       x = 0, y = 0, w = 0, h = 0;
    ImageTile view;
};

std::vector<TileRect> tileGrid(int width, int height, int max)
{
    std::vector<TileRect> out;
    if (width <= max && height <= max) {
        TileRect t;
        t.w = width;
        t.h = height;
        out.push_back(t);
        return out;
    }
    if (max < 3)
        return out;
    const int step = max - 2;
    for (int y0 = 0; y0 < height; y0 += step) {
        for (int x0 = 0; x0 < width; x0 += step) {
            const int x1 = std::min(width, x0 + step);
            const int y1 = std::min(height, y0 + step);
            TileRect  t;
            t.x         = x0 > 0 ? x0 - 1 : 0;
            t.y         = y0 > 0 ? y0 - 1 : 0;
            t.w         = (x1 < width ? x1 + 1 : x1) - t.x;
            t.h         = (y1 < height ? y1 + 1 : y1) - t.y;
            t.view.u0   = static_cast<float>(x0) / static_cast<float>(width);
            t.view.v0   = static_cast<float>(y0) / static_cast<float>(height);
            t.view.u1   = static_cast<float>(x1) / static_cast<float>(width);
            t.view.v1   = static_cast<float>(y1) / static_cast<float>(height);
            t.view.tu0  = static_cast<float>(x0 - t.x) / static_cast<float>(t.w);
            t.view.tv0  = static_cast<float>(y0 - t.y) / static_cast<float>(t.h);
            t.view.tu1  = static_cast<float>(x1 - t.x) / static_cast<float>(t.w);
            t.view.tv1  = static_cast<float>(y1 - t.y) / static_cast<float>(t.h);
            out.push_back(t);
        }
    }
    return out;
}

} // namespace

ImageCache::BitmapEntry* ImageCache::bitmapEntry(const Bitmap& b)
{
    const std::shared_ptr<detail::BitmapData>& d = b.d_;
    if (!d)
        return nullptr;
    const std::size_t full = d->px.size();
    const std::size_t row  = static_cast<std::size_t>(d->w) * 4;
    const auto        at   = [&](int x, int y) {
        return d->px.data() + static_cast<std::size_t>(y) * row +
               static_cast<std::size_t>(x) * 4;
    };

    auto it = bitmaps_.find(d->id);
    if (it == bitmaps_.end()) {
        BitmapEntry e;
        e.data    = d;
        e.version = d->version;
        e.account = account_;
        std::size_t bytes = 0;
        if (d->released) {
            // UTAN PIXLAR: en negativ post — en tom buffert hade gett en
            // svart textur.
            missLog(*d, true);
        } else {
            // NY i fönstret: hela bilden — om den ryms i framens budget — som
            // EN textur eller, större än GPU:ns största, som rutor,
            // alla med mip-kedja. En ruta som inte går att skapa gör posten
            // negativ — försöks inte varje frame.
            if (!mayUpload(full))
                return nullptr;
            const int max = r_->maxTextureSize();
            const std::vector<TileRect> grid = tileGrid(d->w, d->h, max);
            if (grid.empty())
                gfx::log("Bitmap: bilden (%d × %d) går inte att dela i rutor "
                         "för GPU:ns största textur (%d) — den ritas inte",
                         d->w, d->h, max);
            for (const TileRect& t : grid) {
                TileTexture tex;
                tex.image = r_->createImage(t.w, t.h, gfx::ImageFormat::Rgba8,
                                            at(t.x, t.y), row, true);
                if (!tex.image->valid()) {
                    e.textures.clear();
                    e.tiles.clear();
                    bytes = 0;
                    break;
                }
                tex.x            = t.x;
                tex.y            = t.y;
                tex.w            = t.w;
                tex.h            = t.h;
                ImageTile& view  = e.tiles.emplace_back(t.view);
                view.image       = tex.image.get();
                e.textures.push_back(std::move(tex));
                // Budgeten räknar texturens PIXLAR (nivå 0, kanterna
                // inräknade) — mip-kedjan är GPU:ns tredjedel till ovanpå.
                bytes += static_cast<std::size_t>(t.w) *
                         static_cast<std::size_t>(t.h) * 4;
            }
        }
        it = bitmaps_.emplace(d->id, std::move(e)).first;
        book(it->second.account, it->second.bytes, account_, bytes);
        ++contentGeneration_;   // en ny post — `contentVersion` svarar annat
    }

    BitmapEntry& e = it->second;
    e.lastFrame    = frame_;
    book(e.account, e.bytes, account_, e.bytes);
    if (e.textures.empty())
        return &e;

    if (e.version != d->version && d->released) {
        missLog(*d, false);   // står kvar på sin version
    } else if (e.version != d->version) {
        // IKAPP med ändringsloggen: rektanglarna sedan texturens version om
        // loggen räcker dit (version ≥ base), annars hela bilden.
        std::size_t need  = full;
        const bool  rects = e.version >= d->base;
        if (rects) {
            need = 0;
            for (const detail::BitmapData::Change& c : d->changes)
                if (c.version > e.version)
                    need += static_cast<std::size_t>(c.w) *
                            static_cast<std::size_t>(c.h) * 4;
        }
        // Ryms inte ändringen i framen ritas det förra innehållet en frame
        // till — det är redan på GPU:n.
        if (!mayUpload(std::min(need, full)))
            return &e;
        // Varje ruta får sin del: ändringen skuren mot texturens rektangel,
        // kanterna inräknade, så grannens kantpixel följer med.
        for (TileTexture& t : e.textures) {
            if (!(rects && need < full)) {
                r_->updateImage(*t.image, 0, 0, t.w, t.h, at(t.x, t.y), row);
                continue;
            }
            for (const detail::BitmapData::Change& c : d->changes) {
                if (c.version <= e.version)
                    continue;
                const int x0 = std::max(c.x, t.x);
                const int y0 = std::max(c.y, t.y);
                const int x1 = std::min(c.x + c.w, t.x + t.w);
                const int y1 = std::min(c.y + c.h, t.y + t.h);
                if (x0 < x1 && y0 < y1)
                    r_->updateImage(*t.image, x0 - t.x, y0 - t.y, x1 - x0,
                                    y1 - y0, at(x0, y0), row);
            }
        }
        e.version = d->version;
        ++contentGeneration_;   // texturen kom ikapp med bilden
    }
    for (TileTexture& t : e.textures)
        t.image->setFilter(d->filter);
    // SLÄPPET: texturen är hel och aktuell — pixlarna släpps i
    // slutet av pumpvarvet, när varje fönster som ritar bilden i varvet fått
    // den.
    if (e.version == d->version && !d->keepPixels && !d->released &&
        !d->releaseQueued)
        detail::queuePixelRelease(d, d->version);
    return &e;
}

gfx::Image* ImageCache::get(const Bitmap& b)
{
    BitmapEntry* e = bitmapEntry(b);
    if (!e || e->textures.empty())
        return nullptr;
    if (e->textures.size() == 1)
        return e->textures.front().image.get();
    if (!e->tiledLogged)
        gfx::log("Bitmap: bilden (%d × %d) är större än GPU:ns största textur "
                 "(%d) och delad i %zu rutor — bara ritytans bilder "
                 "(Shape::image, Painter::drawImage) ritar rutor, den ritas "
                 "inte här; loggas en gång per fönster",
                 b.width(), b.height(), r_->maxTextureSize(),
                 e->textures.size());
    e->tiledLogged = true;
    return nullptr;
}

const std::vector<ImageTile>* ImageCache::tiles(const Bitmap& b)
{
    BitmapEntry* e = bitmapEntry(b);
    return e && e->textures.size() > 1 ? &e->tiles : nullptr;
}

std::uint64_t ImageCache::contentVersion(const Bitmap& b) const
{
    if (!b.d_)
        return 0;
    const auto it = bitmaps_.find(b.d_->id);
    return it != bitmaps_.end() && !it->second.textures.empty()
               ? it->second.version + 1
               : 0;
}

std::uint64_t ImageCache::contentVersion(const RenderTexture& t) const
{
    if (!t.d_)
        return 0;
    const auto it = targets_.find(t.d_->id);
    return it != targets_.end() && it->second.target ? it->second.version + 1 : 0;
}

// ------------------------------------------------------ render-texturer

gfx::Image* ImageCache::get(const RenderTexture& t, const Ctx& c)
{
    const std::shared_ptr<detail::RenderTextureData>& d = t.d_;
    if (!d)
        return nullptr;
    auto it = targets_.find(d->id);
    if (it == targets_.end()) {
        TargetEntry e;
        e.data    = d;
        e.account = account_;
        // Med MIP-KEDJA — omräknad varje gång målningen lämnar målet;
        // budgeten räknar pixlarna, som för en bild.
        gfx::TargetDesc desc;
        desc.mipmaps = true;
        e.target     = r_->createRenderTarget(d->w, d->h, desc);
        if (!e.target->image().valid())
            e.target.reset();   // negativ post — försöks inte varje frame
        const std::size_t bytes = e.target ? static_cast<std::size_t>(d->w) *
                                                 static_cast<std::size_t>(d->h) * 4
                                           : 0;
        it = targets_.emplace(d->id, std::move(e)).first;
        book(it->second.account, it->second.bytes, account_, bytes);
        ++contentGeneration_;
    }

    // Referensen håller: en målning som ritar en ANNAN ny textur lägger till
    // i mappen, och unordered_map flyttar aldrig sina element.
    TargetEntry& e = it->second;
    e.lastFrame    = frame_;
    book(e.account, e.bytes, account_, e.bytes);
    if (!e.target)
        return nullptr;
    if (e.painting) {
        if (!selfPaintLogged_)
            gfx::log("RenderTexture: en textur ritar sig själv i sin egen "
                     "målning — den ritas inte där");
        selfPaintLogged_ = true;
        return nullptr;
    }

    // MÅLA: ny, eller en ny version (paint/repaint). Klippstacken sparas och
    // läggs tillbaka av pushTarget/popTarget, så målningen går lika bra mitt
    // i en partiell omritning.
    if (e.version != d->version) {
        e.painting = true;
        r_->pushTarget(*e.target, d->clear);
        if (d->fn) {
            Ctx rc  = c;
            rc.box  = gfx::Rectf{0.0f, 0.0f, static_cast<float>(d->w),
                                 static_cast<float>(d->h)};
            rc.area = rc.box;
            d->fn(rc);
        }
        r_->popTarget();
        e.painting = false;
        e.version  = d->version;
        ++contentGeneration_;   // målad om
    }
    gfx::Image& img = e.target->image();
    img.setFilter(d->filter);
    return &img;
}

// ------------------------------------------------------ framens början

void ImageCache::book(std::uint64_t& entryAccount, std::size_t& entryBytes,
                      std::uint64_t account, std::size_t bytes)
{
    // Det vanliga fallet — en ritning av samma rityta, samma storlek — ändrar
    // ingenting: inga uppslag i kontona per bild och frame.
    if (entryAccount == account && entryBytes == bytes)
        return;
    bytes_ -= entryBytes;
    accounts_[entryAccount].bytes -= entryBytes;
    entryAccount = account;
    entryBytes   = bytes;
    bytes_ += bytes;
    accounts_[account].bytes += bytes;
}

std::uint64_t ImageCache::beginAccount(std::uint64_t id,
                                       std::weak_ptr<const void> life,
                                       std::size_t budget)
{
    const std::uint64_t previous = account_;
    if (id == 0)
        return previous;   // 0 är fönstret — inget att öppna
    Account& a = accounts_[id];
    a.life     = std::move(life);
    a.budget   = budget;   // gäller från den här ritningen
    account_   = id;
    return previous;
}

std::size_t ImageCache::accountBytes(std::uint64_t id) const
{
    const auto it = accounts_.find(id);
    return it != accounts_.end() ? it->second.bytes : 0;
}

void ImageCache::setAccountExtra(std::uint64_t id, std::size_t bytes)
{
    if (const auto it = accounts_.find(id); it != accounts_.end())
        it->second.extra = bytes;
}

namespace {

// Bilden går inte att ladda upp igen: släppt eller köad för släpp —
// den, och dess omräkningar, vräks aldrig.
template <class Entry>
bool pinned(const Entry& e)
{
    const auto d = e.data.lock();
    return d && (d->released || d->releaseQueued);
}

} // namespace

void ImageCache::trim(std::size_t defaultBudget, std::size_t uploadBudget)
{
    // 1. DÖDA bilder och texturer — appen har släppt sista handtaget. En
    //    omräknad bild dör med sin källa.
    const auto släppDöda = [this](auto& m) {
        for (auto it = m.begin(); it != m.end();) {
            if (it->second.data.expired()) {
                book(it->second.account, it->second.bytes, 0, 0);
                it = m.erase(it);
                ++contentGeneration_;
            } else {
                ++it;
            }
        }
    };
    släppDöda(bitmaps_);
    släppDöda(targets_);
    släppDöda(bitmapResampled_);

    // 2. DÖDA KONTON — en riven rityta lämnar sina poster åt
    //    fönstrets konto. Fönstrets konto finns FÖRE vandringen, så `book`
    //    aldrig lägger till ett konto mitt i den.
    accounts_[0];
    for (auto it = accounts_.begin(); it != accounts_.end();) {
        if (it->first == 0 || !it->second.life.expired()) {
            ++it;
            continue;
        }
        const std::uint64_t död    = it->first;
        const auto          flytta = [&](auto& m) {
            for (auto& kv : m)
                if (kv.second.account == död)
                    book(kv.second.account, kv.second.bytes, 0, kv.second.bytes);
        };
        flytta(bitmaps_);
        flytta(targets_);
        flytta(bitmapResampled_);
        it = accounts_.erase(it);
    }
    if (!accounts_.count(account_))
        account_ = 0;

    // 3. ÖVER BUDGETEN, PER KONTO: de som legat oritade längst först (LRU),
    //    men aldrig en som ritades i framen som just tog slut (lastFrame ==
    //    frame_) och aldrig en bild utan pixlar (`pinned`). En post vräks bara
    //    medan DESS konto ligger över sin budget — draperingarna (extra)
    //    inräknade. Bilder, omräknade bilder och texturer i SAMMA ordning — en
    //    vräkt bild laddas upp igen, en vräkt omräkning räknas om, en vräkt
    //    textur målas om.
    const auto budgetOf = [&](std::uint64_t id, const Account& a) {
        return id != 0 && a.budget > 0 ? a.budget : defaultBudget;
    };
    const auto overAcc = [&](std::uint64_t id, const Account& a) {
        return a.bytes + a.extra > budgetOf(id, a);
    };
    const auto over = [&](std::uint64_t id) {
        const auto it = accounts_.find(id);
        return it != accounts_.end() && overAcc(id, it->second);
    };
    bool anyOver = false;
    for (const auto& [id, a] : accounts_)
        anyOver = anyOver || overAcc(id, a);
    if (anyOver) {
        enum class Slag { Bild, Omräknad, Textur };
        using Nyckel = std::tuple<std::uint64_t, int, int>;
        struct Kandidat {
            std::uint64_t lastFrame;
            Slag          slag;
            Nyckel        nyckel;   // bild- eller textur-id, (id, w, h)
            bool operator<(const Kandidat& o) const
            {
                return lastFrame != o.lastFrame ? lastFrame < o.lastFrame
                       : slag != o.slag         ? slag < o.slag
                                                : nyckel < o.nyckel;
            }
        };
        std::vector<Kandidat> kandidater;
        for (const auto& [id, e] : bitmaps_)
            if (e.lastFrame < frame_ && over(e.account) && !pinned(e))
                kandidater.push_back(
                    Kandidat{e.lastFrame, Slag::Bild, Nyckel{id, 0, 0}});
        for (const auto& [nyckel, e] : bitmapResampled_)
            if (e.lastFrame < frame_ && over(e.account) && !pinned(e))
                kandidater.push_back(Kandidat{e.lastFrame, Slag::Omräknad, nyckel});
        for (const auto& [id, e] : targets_)
            if (e.lastFrame < frame_ && over(e.account))
                kandidater.push_back(
                    Kandidat{e.lastFrame, Slag::Textur, Nyckel{id, 0, 0}});
        std::sort(kandidater.begin(), kandidater.end());
        for (const Kandidat& k : kandidater) {
            if (k.slag == Slag::Textur) {
                const auto it = targets_.find(std::get<0>(k.nyckel));
                if (!over(it->second.account))
                    continue;
                book(it->second.account, it->second.bytes, 0, 0);
                targets_.erase(it);
            } else if (k.slag == Slag::Omräknad) {
                const auto it = bitmapResampled_.find(k.nyckel);
                if (!over(it->second.account))
                    continue;
                book(it->second.account, it->second.bytes, 0, 0);
                bitmapResampled_.erase(it);
            } else {
                const auto it = bitmaps_.find(std::get<0>(k.nyckel));
                if (!over(it->second.account))
                    continue;
                book(it->second.account, it->second.bytes, 0, 0);
                bitmaps_.erase(it);
            }
            ++contentGeneration_;   // vräkt — `contentVersion` svarar 0
        }
    }
    // EN loggrad per konto och episod — det synliga, och bilderna utan
    // pixlar, överskrider budgeten.
    for (auto& [id, a] : accounts_) {
        if (!overAcc(id, a)) {
            a.overLogged = false;
            continue;
        }
        if (!a.overLogged)
            gfx::log("ImageCache: %s tar %zu byte (bilder) + %zu byte "
                     "(draperingar), och allt ritades i förra framen eller "
                     "saknar pixlar (setKeepPixels(false)) — budgeten %zu byte "
                     "hålls inte (Canvas::setMemoryBudget, "
                     "App::setCanvasMemoryBudget)",
                     id == 0 ? "fönstret utanför ritytorna" : "en rityta",
                     a.bytes, a.extra, budgetOf(id, a));
        a.overLogged = true;
    }

    // 3. En ny frame: en ny uppladdningsbudget.
    ++frame_;
    uploadBudget_  = uploadBudget;
    frameUploaded_ = 0;
    deferred_      = false;
}

} // namespace glaze
