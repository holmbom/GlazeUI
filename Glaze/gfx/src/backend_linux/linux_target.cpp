// Render-målen i GL-backenden: en FBO med en
// RGBA8-textur, bunden med `bindTarget`. Spegel av d3d11_target.cpp; här
// bara GL-skillnaderna:
//   * Y-FLIPPARNA. Målet ritas med samma vertexshader som allt annat, så
//     bildens överkant hamnar på v = 1 (`flippedV` — ritvägarna speglar
//     tillbaka, precis som compositen gör för retained-FBO:n), och
//     glScissor räknar nerifrån mot det BUNDNA målets höjd (`targetH_`, som
//     bindTarget sätter innan fasaden tillämpar klippet). `readTarget` vänder
//     raderna — glReadPixels läser nerifrån.
//   * UI-programmets vp-uniform följer målet LAT: flush laddar upp
//     `targetW_/targetH_` när de skiljer från det programmet senast fick.
//   * Ett mål som skapas MITT i en frame rensas med sin egen FBO bunden —
//     den FBO som var bunden (`currentFbo_`) binds tillbaka efteråt.
//   * DJUP OCH MSAA: provtagna renderbuffertar (färg RGBA8,
//     djup GL_DEPTH_COMPONENT32F) i rit-FBO:n och texturen i en egen
//     upplösnings-FBO, fylld med `glBlitFramebuffer` när målet LÄMNAS. Blir
//     FBO:n inte komplett med en provtagning prövas hälften, ned till ett
//     prov — loggat; `RenderTarget::samples` säger vad det blev.
// GL-anropen bor här, i linux_renderer.cpp, linux_scene.cpp, linux_image.cpp,
// linux_fill.cpp och gl_common.cpp — ingen annanstans.

#include <cstring>

#include "../image_util.h"
#include "gfx/platform.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

// FBO:erna för ett mål med `samples` prov: rit-FBO:n (texturen, eller med
// MSAA en provtagen färgbuffert) med djupet om det begärts, och med MSAA
// upplösnings-FBO:n med texturen. false — och allt det skapade raderat —
// när någon av dem inte blir komplett. Lämnar GL_FRAMEBUFFER bundet till
// något av det skapade; anroparen binder tillbaka.
bool buildTargetFbos(GlTexture tex, int w, int h, int samples, bool depth,
                     unsigned& fbo, unsigned& resolveFbo, unsigned& colorRb,
                     unsigned& depthRb)
{
    fbo = resolveFbo = colorRb = depthRb = 0;
    pglGenFramebuffers(1, &fbo);
    pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (samples > 1) {
        pglGenRenderbuffers(1, &colorRb);
        pglBindRenderbuffer(GL_RENDERBUFFER, colorRb);
        pglRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w,
                                          h);
        pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_RENDERBUFFER, colorRb);
    } else {
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, tex, 0);
    }
    if (depth) {
        // 0 prov = en vanlig renderbuffert (glRenderbufferStorage).
        pglGenRenderbuffers(1, &depthRb);
        pglBindRenderbuffer(GL_RENDERBUFFER, depthRb);
        pglRenderbufferStorageMultisample(GL_RENDERBUFFER,
                                          samples > 1 ? samples : 0,
                                          GL_DEPTH_COMPONENT32F, w, h);
        pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, depthRb);
    }
    pglBindRenderbuffer(GL_RENDERBUFFER, 0);
    bool ok = pglCheckFramebufferStatus(GL_FRAMEBUFFER) ==
              GL_FRAMEBUFFER_COMPLETE;
    if (ok && samples > 1) {
        pglGenFramebuffers(1, &resolveFbo);
        pglBindFramebuffer(GL_FRAMEBUFFER, resolveFbo);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, tex, 0);
        ok = pglCheckFramebufferStatus(GL_FRAMEBUFFER) ==
             GL_FRAMEBUFFER_COMPLETE;
    }
    if (!ok) {
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
        for (unsigned* f : {&fbo, &resolveFbo})
            if (*f)
                pglDeleteFramebuffers(1, f);
        for (unsigned* r : {&colorRb, &depthRb})
            if (*r)
                pglDeleteRenderbuffers(1, r);
        fbo = resolveFbo = colorRb = depthRb = 0;
        // Ett nekat lagringsanrop (för många prov för formatet) lämnar ett
        // GL-fel — det får inte tas för nästa försöks.
        while (glGetError() != GL_NO_ERROR) {
        }
    }
    return ok;
}

} // namespace

GlRenderTarget::~GlRenderTarget()
{
    if (!renderer_)
        return;
    renderer_->destroyFramebuffer(fbo_);
    renderer_->destroyFramebuffer(resolveFbo_);
    renderer_->destroyRenderbuffer(colorRb_);
    renderer_->destroyRenderbuffer(depthRb_);
}

void GlRenderer::destroyFramebuffer(unsigned fbo)
{
    if (!fbo || !ready())
        return;
    makeCurrent();
    pglDeleteFramebuffers(1, &fbo);
}

void GlRenderer::destroyRenderbuffer(unsigned rb)
{
    if (!rb || !ready())
        return;
    makeCurrent();
    pglDeleteRenderbuffers(1, &rb);
}

int GlRenderer::supportedSamples(int requested) const
{
    // Den närmaste ≤ det begärda som GL_MAX_SAMPLES rymmer (8, 4, 2). Ett
    // format som ändå nekar provtagningen fångas av FBO-kontrollen.
    for (int n : {8, 4, 2})
        if (n <= requested && n <= maxSamples_)
            return n;
    return 1;
}

std::unique_ptr<RenderTarget> GlRenderer::createRenderTarget(
    int w, int h, const TargetDesc& d)
{
    if (!ready() || !targetDescOk(w, h, d) || !makeCurrent())
        return std::make_unique<GlRenderTarget>();
    const GlTexture tex =
        createTexture(ImageFormat::Rgba8, w, h, nullptr, 0, d.mipmaps);
    if (!tex)
        return std::make_unique<GlRenderTarget>();   // createTexture loggade

    // Provtagningen: den närmaste GL_MAX_SAMPLES rymmer, och blir FBO:n ändå
    // inte komplett (formatets eget tak kan ligga under) hälften, ned till ett
    // prov. En lägre än den begärda loggas en gång.
    unsigned fbo = 0, resolveFbo = 0, colorRb = 0, depthRb = 0;
    bool     ok      = false;
    int      samples = supportedSamples(d.samples);
    for (; samples >= 1; samples /= 2) {
        ok = buildTargetFbos(tex, w, h, samples, d.depth, fbo, resolveFbo,
                             colorRb, depthRb);
        if (ok)
            break;
    }
    if (ok && samples < d.samples && !samplesLogged_) {
        samplesLogged_ = true;
        log("createRenderTarget: %d prov går inte här (GL_MAX_SAMPLES %d) — "
            "%d",
            d.samples, maxSamples_, samples);
    }
    if (ok) {
        // Nollställt (fasadens löfte): bilden, i den FBO som bär den. glClear
        // lyder scissorn och masken — UI:ts läge har masken på.
        applyUiState();
        pglBindFramebuffer(GL_FRAMEBUFFER, resolveFbo ? resolveFbo : fbo);
        if (scissorOn_)
            glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (scissorOn_)
            glEnable(GL_SCISSOR_TEST);
    }
    pglBindFramebuffer(GL_FRAMEBUFFER, currentFbo_);   // det som var bundet
    // Kedjan finns från skapandet: bilden kan ritas innan målet
    // någonsin lämnats, och utan nivåerna vore texturen ofullständig.
    if (ok && d.mipmaps)
        generateMips(tex);
    if (!ok) {
        log("createRenderTarget: FBO:n på %d × %d är inte komplett", w, h);
        destroyTexture(tex);
        return std::make_unique<GlRenderTarget>();
    }
    ++stats_.targetsCreated;
    return std::make_unique<GlRenderTarget>(this, tex, fbo, resolveFbo,
                                            colorRb, depthRb, w, h, samples,
                                            d.mipmaps);
}

// MSAA: rit-FBO:ns provtagna färg → bildens textur. Blitten lyder scissorn
// (och färgmasken hos somliga drivrutiner) — båda av under upplösningen.
void GlRenderer::resolveTarget(GlRenderTarget& t)
{
    applyUiState();
    if (scissorOn_)
        glDisable(GL_SCISSOR_TEST);
    const int w = t.image().width();
    const int h = t.image().height();
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, t.fbo());
    pglBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.resolveFbo());
    pglBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (scissorOn_)
        glEnable(GL_SCISSOR_TEST);
}

void GlRenderer::bindTarget(RenderTarget* t)
{
    if (!makeCurrent())
        return;
    // Det som batchats hör till målet det ritades för.
    flush();
    // MSAA: målet som LÄMNAS löses upp till sin bild — i popTarget och när ett
    // nästlat mål binds ovanpå det.
    if (bound_ && bound_->resolveFbo())
        resolveTarget(*bound_);
    // MIP-KEDJAN räknas om när målet lämnas — efter upplösningen,
    // och först när nästa FBO bundits, så nivå 0 inte sitter i den bundna.
    const GlTexture leaving =
        bound_ && bound_->imageView().mipmapped() ? bound_->imageView().tex()
                                                  : 0;
    if (t) {
        auto& rt    = static_cast<GlRenderTarget&>(*t);
        bound_      = &rt;
        currentFbo_ = rt.fbo();
        targetW_    = rt.image().width();
        targetH_    = rt.image().height();
        bbOrigin_   = 0;
    } else {
        // Framens eget mål; i ett offscreen-block standard-FBO:n (se
        // beginOffscreen i linux_renderer.cpp). Är det backbuffern ligger
        // klientytan storleksmarginalen in (E2L, `backbufferOrigin`).
        bound_      = nullptr;
        currentFbo_ = (inBackbuffer_ || offscreen_) ? 0u : offFbo_;
        targetW_    = width_;
        targetH_    = height_;
        bbOrigin_   = currentFbo_ == 0 ? backbufferOrigin() : 0;
    }
    pglBindFramebuffer(GL_FRAMEBUFFER, currentFbo_);
    glViewport(bbOrigin_, bbOrigin_, targetW_, targetH_);
    if (leaving)
        generateMips(leaving);
}

// Målets bild → CPU, rak alfa (renderer.h). glReadPixels läser NERIFRÅN
// (y-flipp nr 2) — raderna vänds, så rad 0 är överst som i de andra
// backendarna.
bool GlRenderer::readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba)
{
    if (!ready() || !readTargetOk(t) || !makeCurrent())
        return false;
    auto&             rt     = static_cast<GlRenderTarget&>(t);
    const int         w      = rt.image().width();
    const int         h      = rt.image().height();
    const std::size_t stride = static_cast<std::size_t>(w) * 4;
    std::vector<std::uint8_t> tmp(stride * static_cast<std::size_t>(h));
    pglBindFramebuffer(GL_READ_FRAMEBUFFER, rt.imageFbo());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
    pglBindFramebuffer(GL_FRAMEBUFFER, currentFbo_);   // det bundna målet
    rgba.resize(tmp.size());
    for (int y = 0; y < h; ++y)
        std::memcpy(rgba.data() + static_cast<std::size_t>(y) * stride,
                    tmp.data() + static_cast<std::size_t>(h - 1 - y) * stride,
                    stride);
    unpremultiplyRgba(rgba.data(), static_cast<std::size_t>(w) * h);
    return true;
}

} // namespace gfx
