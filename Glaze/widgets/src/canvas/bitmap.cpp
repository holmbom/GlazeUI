// glaze::Bitmap — pixlarna och ändringsloggen. Ingen
// renderer här: uppladdningen är fönstrets (`ImageCache::get(const Bitmap&)`,
// src/image_cache.cpp), som läser loggen i image_data.h.

#include "glaze/canvas/bitmap.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <vector>

#include "gfx/platform.h"
#include "image_data.h"

namespace glaze {
namespace {

// Bildernas id — atomiskt, för konstruktorerna och `decode` får köras i
// vilken tråd som helst.
std::atomic<std::uint64_t> g_nextBitmapId{1};

// Ryms w × h RGBA-pixlar i minnet över huvud taget? (w × h × 4 får inte
// spricka i size_t.)
bool sizeOk(int w, int h)
{
    return w > 0 && h > 0 &&
           static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) <=
               std::numeric_limits<std::size_t>::max() / 8;
}

std::shared_ptr<detail::BitmapData> makeData(int w, int h)
{
    auto d = std::make_shared<detail::BitmapData>();
    d->id  = g_nextBitmapId.fetch_add(1, std::memory_order_relaxed);
    d->w   = w;
    d->h   = h;
    return d;
}

// SLÄPPKÖN — UI-trådens, som allt som rör uppladdningen: bilden och
// den uppladdade versionen.
struct Queued {
    std::weak_ptr<detail::BitmapData> data;
    std::uint64_t                     version = 0;
};
std::vector<Queued>& releaseQueue()
{
    static std::vector<Queued> q;
    return q;
}

} // namespace

namespace detail {

void queuePixelRelease(const std::shared_ptr<BitmapData>& d,
                       std::uint64_t version)
{
    d->releaseQueued = true;
    releaseQueue().push_back(Queued{d, version});
}

void releaseQueuedPixels()
{
    std::vector<Queued>& q = releaseQueue();
    if (q.empty())
        return;
    for (const Queued& k : q) {
        const std::shared_ptr<BitmapData> d = k.data.lock();
        if (!d)
            continue;
        d->releaseQueued = false;
        if (d->keepPixels || d->released || d->version != k.version)
            continue;   // ångrad, redan släppt eller ändrad sedan uppladdningen
        std::vector<std::uint8_t>().swap(d->px);
        d->changes.clear();
        d->released = true;
    }
    q.clear();
}

} // namespace detail

Bitmap::Bitmap(int w, int h) : Bitmap(w, h, nullptr, 0) {}

Bitmap::Bitmap(int w, int h, const std::uint8_t* rgba, std::size_t stride)
{
    const std::size_t row = sizeOk(w, h) ? static_cast<std::size_t>(w) * 4 : 0;
    if (!row || (rgba && stride != 0 && stride < row)) {
        gfx::log("Bitmap: %d × %d med stride %zu går inte att skapa", w, h,
                 stride);
        return;
    }
    auto d = makeData(w, h);
    d->px.assign(row * static_cast<std::size_t>(h), 0);
    if (rgba) {
        const std::size_t src = stride ? stride : row;
        for (int y = 0; y < h; ++y)
            std::memcpy(d->px.data() + static_cast<std::size_t>(y) * row,
                        rgba + static_cast<std::size_t>(y) * src, row);
    }
    d_ = std::move(d);
}

Bitmap Bitmap::decode(const void* data, std::size_t size)
{
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    Bitmap                    b;
    if (!gfx::decodeImage(data, size, px, w, h))   // loggar själv
        return b;
    auto d = makeData(w, h);
    d->px  = std::move(px);
    b.d_   = std::move(d);
    return b;
}

int Bitmap::width() const
{
    return d_ ? d_->w : 0;
}

int Bitmap::height() const
{
    return d_ ? d_->h : 0;
}

const std::uint8_t* Bitmap::pixels() const
{
    return d_ && !d_->released ? d_->px.data() : nullptr;
}

void Bitmap::setPixels(int x, int y, int w, int h, const std::uint8_t* rgba,
                       std::size_t stride)
{
    if (d_ && d_->released) {
        gfx::log("Bitmap::setPixels: bilden har släppt sina pixlar "
                 "(setKeepPixels(false)) — ingenting skrivs");
        return;
    }
    const std::size_t row = w > 0 ? static_cast<std::size_t>(w) * 4 : 0;
    if (!d_ || !rgba || w <= 0 || h <= 0 || x < 0 || y < 0 ||
        w > d_->w - x || h > d_->h - y || (stride != 0 && stride < row)) {
        gfx::log("Bitmap::setPixels: (%d, %d, %d × %d) i en bild på %d × %d "
                 "— utanför, tom eller utan pixlar; ingenting skrivs",
                 x, y, w, h, width(), height());
        return;
    }
    const std::size_t src  = stride ? stride : row;
    const std::size_t dRow = static_cast<std::size_t>(d_->w) * 4;
    for (int r = 0; r < h; ++r)
        std::memcpy(d_->px.data() + static_cast<std::size_t>(y + r) * dRow +
                        static_cast<std::size_t>(x) * 4,
                    rgba + static_cast<std::size_t>(r) * src, row);
    d_->markChanged(x, y, w, h);
}

void Bitmap::fill(gfx::Color c)
{
    if (!d_)
        return;
    if (d_->released) {
        gfx::log("Bitmap::fill: bilden har släppt sina pixlar "
                 "(setKeepPixels(false)) — ingenting skrivs");
        return;
    }
    for (std::size_t i = 0; i < d_->px.size(); i += 4) {
        d_->px[i]     = c.r;
        d_->px[i + 1] = c.g;
        d_->px[i + 2] = c.b;
        d_->px[i + 3] = c.a;
    }
    d_->markChanged(0, 0, d_->w, d_->h);
}

gfx::ImageFilter Bitmap::filter() const
{
    return d_ ? d_->filter : gfx::ImageFilter::Linear;
}

void Bitmap::setFilter(gfx::ImageFilter f)
{
    if (d_)
        d_->filter = f;
}

void Bitmap::setKeepPixels(bool keep)
{
    if (!d_)
        return;
    if (keep && d_->released) {
        gfx::log("Bitmap::setKeepPixels(true): pixlarna är redan släppta och "
                 "kommer inte tillbaka");
        return;
    }
    d_->keepPixels = keep;
}

bool Bitmap::keepPixels() const
{
    return d_ ? d_->keepPixels : true;
}

} // namespace glaze
