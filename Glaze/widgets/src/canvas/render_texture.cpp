// glaze::RenderTexture — måtten, målningen och versionen. Ingen renderer
// här: målet och målningen är fönstrets
// (`ImageCache::get(const RenderTexture&, const Ctx&)`, src/image_cache.cpp).

#include "glaze/canvas/render_texture.h"

#include <atomic>

#include "gfx/platform.h"
#include "image_data.h"

namespace glaze {
namespace {

std::atomic<std::uint64_t> g_nextTextureId{1};

} // namespace

RenderTexture::RenderTexture(int w, int h)
{
    if (w <= 0 || h <= 0) {
        gfx::log("RenderTexture: %d × %d går inte att skapa", w, h);
        return;
    }
    auto d = std::make_shared<detail::RenderTextureData>();
    d->id  = g_nextTextureId.fetch_add(1, std::memory_order_relaxed);
    d->w   = w;
    d->h   = h;
    d_     = std::move(d);
}

int RenderTexture::width() const
{
    return d_ ? d_->w : 0;
}

int RenderTexture::height() const
{
    return d_ ? d_->h : 0;
}

void RenderTexture::paint(PaintFn fn, gfx::Color clear)
{
    if (!d_)
        return;
    d_->fn    = std::move(fn);
    d_->clear = clear;
    ++d_->version;
}

void RenderTexture::repaint()
{
    if (d_)
        ++d_->version;
}

gfx::ImageFilter RenderTexture::filter() const
{
    return d_ ? d_->filter : gfx::ImageFilter::Linear;
}

void RenderTexture::setFilter(gfx::ImageFilter f)
{
    if (d_)
        d_->filter = f;
}

} // namespace glaze
