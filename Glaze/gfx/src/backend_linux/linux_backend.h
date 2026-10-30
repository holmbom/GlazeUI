#pragma once
// Intern header för Linux-backenden (Wayland/X11 + EGL/GL + FreeType/
// fontconfig/libpng) — ligger i src/, INTE include/, och får aldrig
// inkluderas utanför backend_linux/. Backend-typer (wl_*, xdg_*, Xlib, EGL,
// FT_, Fc, png_) får bara förekomma i den här mappen; utåt syns enbart
// fasaden i include/gfx/.
//
// Filen är protokoll-NEUTRAL: Wayland-typerna bor i wayland_backend.h och
// X11-typerna i x11_backend.h (Xlib förorenar det globala namnrummet med
// `Window`/`None` — de headrarna får bara dras in av sina egna .cpp).
// GL-anropen bor i renderarens filer (linux_renderer/scene/image/target/
// fill.cpp och gl_common.cpp, som äger loadern) — texten går genom
// GlRenderer-hjälparna nedan, så GL-loadern finns på ETT ställe.

#include <EGL/egl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../font_fallback.h"
#include "gfx/platform.h"   // ScreenInfo — `LinuxDisplay::placementScreens`
#include "gfx/renderer.h"
#include "gfx/text.h"
#include "gfx/window.h"

namespace gfx {

// Intern event-kö: displayernas listeners översätter protokoll-events till
// gfx::Event och köar dem här; waitEvent/pollEvent (linux_platform.cpp)
// dränerar kön. Samma mönster som d3d11_platform.cpp.
void pushEvent(const Event& e);

// Fönster-id-löpnumret (0 = ogiltigt) — samma semantik som D3D11.
WindowId nextWindowId();

class LinuxWindow;

// Registret över levande fönster (broadcast: temabyte/textskalning saknar
// naturligt fönster — ett event stämplas per fönster, som D3D11:s
// WM_SETTINGCHANGE-per-fönster). Fönstren registrerar sig i init/shutdown.
void registerLinuxWindow(LinuxWindow* w);
void unregisterLinuxWindow(LinuxWindow* w);
void broadcastEvent(EventType t);

// GRAFIKKORTET (linux_gpu.cpp). `applyGpuEnvironment` körs
// FÖRST i `gfx::init`, före första EGL-anropet, en gång per process: regeln i
// ../gpu_util.h, och `setenv(…, 0)` för det den ger — en variabel användaren
// satt skrivs aldrig över. `eglAdapterId` är id:t för kortet displayen
// FAKTISKT ritar med (EGL_EXT_device_query + EGL_EXT_device_drm), tomt för
// llvmpipe. `reportGpuOutcome` (GlRenderer::init) loggar ett mål som inte
// blev av, mjukvarurendering och en begäran ändrad efter init.
// `linuxChildEnvironment` är processens miljö ("NAMN=värde") UTAN det
// GlazeUI satte — barnprocessernas (`openUrl`/`openFile`), byggd före fork.
void                     applyGpuEnvironment();
std::string              eglAdapterId(EGLDisplay dpy);
void                     reportGpuOutcome(const std::string& adapterId,
                                          const std::string& renderer);
std::vector<std::string> linuxChildEnvironment();

// ---------------------------------------------------------------- epoll
// Pumpens vakningskällor: display-fd:n, timerfd:ar (fönster-timers +
// key-repeat), D-Bus-fd:n. waitEvent sover i epoll_wait (~0 % CPU, hård
// regel 1); en läsbar källa kör sitt onReady som översätter → pushEvent.
struct PollSource {
    int fd = -1;
    bool isDisplay = false;            // display-fd:n hanteras via
                                       // prepoll/postpoll, inte onReady
    std::function<void()> onReady;
};
// false (loggat) = epoll vägrade registrera fd:n — källan väcker då aldrig
// pumpen, och anroparen ska inte tro att den gör det (en timer
// som "går" utan att väcka sover för evigt).
bool addPollSource(PollSource* s);
void removePollSource(PollSource* s);

// EN TIMERFD-KÄLLA i pumpens epoll (linux_timer.cpp) —
// delad av fönstrens timers (`LinuxWindow::startTimer`) och appens
// (`gfx::startAppTimer`), så att armeringen, dräneringen och felvägarna står
// EN gång. Upprepande med perioden `ms` (≥ 1 — `it_value` 0 hade avväpnat
// timerfd); varje gång fd:n blir läsbar töms räknaren och ETT
// `EventType::Timer` köas med `windowId`/`timerId` — missade ticks blir ett
// event. Den gemensamma epollen täcker Wayland, X11 och headless.
//
// `make` svarar nullptr (loggat) när timerfd inte går att skapa eller armas,
// eller epoll vägrar — då är fd:n redan stängd, och timern går inte.
// Destruktorn tar källan ur epollen och stänger fd:n.
struct TimerSource {
    int        fd = -1;
    PollSource src;

    static std::unique_ptr<TimerSource> make(WindowId wid, std::uint64_t id,
                                             std::uint32_t ms);
    TimerSource() = default;
    ~TimerSource();
    TimerSource(const TimerSource&)            = delete;
    TimerSource& operator=(const TimerSource&) = delete;
};

// APPENS TIMER (linux_timer.cpp) — `gfx::shutdown()` tar
// varje källa ur epollen och stänger den. Idempotent.
void appTimerShutdown();

// Portalens text-skalningsfaktor (1.0 tills D-Bus svarat). Ingår i
// uiScale på Wayland (monitorskala × textskalning); X11:s Xft.dpi bär
// redan GNOME:s textskalning och multiplicerar INTE in den igen.
float textScaleFactor();

class GlRenderer;

// GL-handtag utan GL-headerberoende (GLuint är unsigned int).
using GlTexture = unsigned int;
using GlBuffer  = unsigned int;

// ---------------------------------------------------------------- scenen

// gfx::GpuBuffer ovanpå ett GL-buffertobjekt (linux_scene.cpp). Hör till
// rendererns kontext — släpp bufferten före fönstret (fasadens
// livstidsregel); destruktorn raderar objektet genom renderern.
class GlGpuBuffer : public GpuBuffer {
public:
    GlGpuBuffer(GlRenderer* r, BufferKind kind, std::size_t size, GlBuffer id)
        : renderer_(r), kind_(kind), size_(id ? size : 0), id_(id)
    {
    }
    ~GlGpuBuffer() override;
    GlGpuBuffer(const GlGpuBuffer&)            = delete;
    GlGpuBuffer& operator=(const GlGpuBuffer&) = delete;

    BufferKind  kind() const override { return kind_; }
    std::size_t size() const override { return size_; }
    bool        valid() const override { return id_ != 0; }

    GlBuffer id() const { return id_; }

private:
    GlRenderer* renderer_;
    BufferKind  kind_;
    std::size_t size_;
    GlBuffer    id_;
};

// ---------------------------------------------------------------- bilder

// gfx::Image ovanpå en GL-textur (linux_image.cpp). Bilder ur pixlar
// (`createImage` — loadImage avkodar med libpng och går samma väg)
// uppdateras med glTexSubImage2D. Texturen hör till rendererns GL-kontext —
// släpp bilden före fönstret (fasadens livstidsregel); destruktorn raderar
// texturen genom renderern. Default-konstruerad = ogiltig.
class GlImage : public Image {
public:
    GlImage() = default;
    GlImage(GlRenderer* r, GlTexture tex, int w, int h, ImageFormat f,
            bool premultiplied, bool flippedV, bool mipmapped)
        : renderer_(r), tex_(tex), w_(w), h_(h), format_(f),
          premultiplied_(premultiplied), flippedV_(flippedV),
          mipmapped_(mipmapped)
    {
    }
    ~GlImage() override;
    GlImage(const GlImage&)            = delete;
    GlImage& operator=(const GlImage&) = delete;

    int  width() const override { return tex_ ? w_ : 0; }
    int  height() const override { return tex_ ? h_ : 0; }
    bool valid() const override { return tex_ != 0; }
    ImageFormat format() const override { return format_; }
    bool premultiplied() const override { return premultiplied_; }
    bool flippedV() const override { return flippedV_; }

    GlTexture tex() const { return tex_; }
    // Har texturen en MIP-KEDJA? Utan kedja är GL_TEXTURE_MAX_LEVEL
    // 0, så den är komplett också för samplernas mip-filter.
    bool      mipmapped() const { return mipmapped_; }

private:
    GlRenderer* renderer_ = nullptr;   // ägs av fönstret — livstidsregeln
    GlTexture   tex_      = 0;
    int         w_ = 0, h_ = 0;
    ImageFormat format_        = ImageFormat::Rgba8;
    bool        premultiplied_ = false;
    bool        flippedV_      = false;
    bool        mipmapped_     = false;
};

// gfx::RenderTarget ovanpå en FBO med en RGBA8-textur (linux_target.cpp).
// Bilden är texturen sedd som en GlImage —
// PREMULTIPLICERAD och SPEGLAD (`flippedV`): FBO:n ritas med samma
// vertexshader som allt annat, y-nedåt-px blir NDC med y uppåt, och GL lägger
// rad 0 nederst — bildens överkant hamnar på v = 1. Ritvägarna speglar
// tillbaka. Destruktorn raderar FBO:erna och renderbuffertarna genom
// renderern (texturen raderas av bildens egen destruktor).
//
// DJUP OCH MSAA: med provtagning > 1 ritas det i en FBO
// med PROVTAGNA renderbuffertar (färg RGBA8, djup GL_DEPTH_COMPONENT32F), och
// texturen sitter i en egen upplösnings-FBO (`resolveFbo`) som
// `glBlitFramebuffer` fyller när målet lämnas. Utan MSAA sitter texturen i
// rit-FBO:n och djupet (om något) är en vanlig renderbuffert.
class GlRenderTarget : public RenderTarget {
public:
    GlRenderTarget() = default;   // ogiltigt (fabrikens svar vid fel)
    GlRenderTarget(GlRenderer* r, GlTexture tex, unsigned fbo,
                   unsigned resolveFbo, unsigned colorRb, unsigned depthRb,
                   int w, int h, int samples, bool mipmapped)
        : image_(r, tex, w, h, ImageFormat::Rgba8, true, true, mipmapped),
          renderer_(r),
          fbo_(fbo), resolveFbo_(resolveFbo), colorRb_(colorRb),
          depthRb_(depthRb), samples_(samples)
    {
    }
    ~GlRenderTarget() override;
    GlRenderTarget(const GlRenderTarget&)            = delete;
    GlRenderTarget& operator=(const GlRenderTarget&) = delete;

    Image& image() override { return image_; }
    int    samples() const override { return image_.valid() ? samples_ : 1; }
    bool   hasDepth() const override { return depthRb_ != 0; }

    unsigned fbo() const { return fbo_; }   // det som ritas i
    // FBO:n som bär bilden: upplösningens med MSAA, annars rit-FBO:n.
    unsigned imageFbo() const { return resolveFbo_ ? resolveFbo_ : fbo_; }
    unsigned resolveFbo() const { return resolveFbo_; }   // 0 utan MSAA
    const GlImage& imageView() const { return image_; }   // kedjan

private:
    GlImage     image_;
    GlRenderer* renderer_   = nullptr;
    unsigned    fbo_        = 0;
    unsigned    resolveFbo_ = 0;
    unsigned    colorRb_    = 0;
    unsigned    depthRb_    = 0;
    int         samples_    = 1;
};

// ---------------------------------------------------------------- text

// gfx::Font ovanpå FreeType. loadFamily slår upp filen via fontconfig
// (användarens riktiga systemfont — samma svar som GTK/Qt) med
// font_catalog som fallback. Glyfer rasteriseras vid behov
// (FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT → 8-bitars gråskala) och packas
// rad-för-rad i samma R8-atlas-mönster som d3d11_text.cpp (512², reset om
// full). All mätning via linearHoriAdvance (design-advance, rasteriserar
// aldrig) — spegel av d3d11:s advanceOf-väg. GLYFRESERVEN:
// en glyf fonten saknar tas ur fontconfigs SORTERADE kandidater för
// fontens mönster (`FcFontSort`, en gång per mönster och process) — regeln i
// ../font_fallback.h, upplöst EN gång per kodpunkt.
class FtFont : public Font {
public:
    FtFont() = default;
    ~FtFont() override { unload(); }
    FtFont(const FtFont&)            = delete;
    FtFont& operator=(const FtFont&) = delete;

    bool load(const std::string& path, float sizePt, int faceIndex = 0);
    // Familjen i stilen: fontconfig ("sans-serif:bold" …) → katalogen, där
    // den närmaste stilen inom första familj som går att ladda vinner.
    bool loadFamily(FontFamily f, float sizePt, FontStyle style);
    void unload();

    // Kopplar fonten till sitt fönsters renderer — atlas-texturen skapas
    // lazy vid första draw. Sätts av GlRenderer::loadFont.
    void setRenderer(GlRenderer* r) { renderer_ = r; }

    // Intern ritväg — anropas av GlRenderer::drawText.
    float draw(GlRenderer& r, float x, float y,
               const std::string& utf8, Color color);

    Sizef measure(const std::string& utf8) const override;
    float height() const override;
    bool  valid() const override { return face_ != nullptr; }

    float prefixWidth(const std::string& utf8,
                      std::size_t bytes) const override;
    std::size_t byteIndexAtX(const std::string& utf8,
                             float x) const override;
    std::size_t fitWidth(const std::string& utf8,
                         float maxW) const override;

    // Ritytans text: stilen läses ur fontens egna
    // flaggor (style_flags + OS/2-vikten) och rasteriseringen är DEN som
    // atlasen fylls med (`rasterize`).
    float     ascent() const override { return face_ ? ascent_ : 0.0f; }
    FontStyle style() const override { return style_; }
    bool      rasterGlyph(char32_t cp, GlyphRaster& out) const override;
    bool      hasGlyph(char32_t cp) const override;

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas-UV
        float w = 0, h = 0;                     // bitmappens mått (px)
        float bearingX = 0, bearingY = 0;       // offset från baslinjen
                                                // (bearingY negativ ovanför)
        float advance  = 0;                     // penn-förflyttning (px)
    };
    // En RESERVFONT, laddad i fontens egen FreeType-instans och storlek.
    struct Reserve {
        FT_Face     face = nullptr;
        std::string file;
        int         index = 0;
    };
    // En kodpunkt upplöst EN gång (också ett negativt svar): glyfen och dess
    // stegbredd — mätningen och rastret läser samma.
    struct Resolved {
        GlyphRef ref;
        float    advance = 0.0f;
    };

    const Glyph* getGlyph(char32_t cp);   // rasteriserar och packar vid behov
    float advanceOf(char32_t cp) const;   // den upplösta glyfens stegbredd
    // Facen ett uppslag pekar på: fontens egen (0) eller reserv nr i − 1.
    FT_Face faceOf(int i) const { return i == 0 ? face_ : reserves_[i - 1].face; }
    // Kodpunktens glyf enligt reservens regel (../font_fallback.h), cachad.
    const Resolved& resolve(char32_t cp) const;
    // En reservface som har kodpunkten (fontconfigs kandidater) — false utan.
    bool reserveFace(char32_t cp, GlyphRef& out) const;
    // Glyfen som pixlar — DEN rasteriseringen, delad av atlasen (getGlyph)
    // och rasterGlyph: FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT, ur fontens egen
    // face eller reservens.
    bool  rasterize(FT_Face face, FT_UInt gi, GlyphRaster& out) const;
    float advanceIn(FT_Face face, FT_UInt gi) const;   // linearHoriAdvance
    FontStyle readStyle() const;
    bool ensureAtlas();
    void resetAtlas();                    // atlas full → börja om (sällsynt)

    FT_Library library_ = nullptr;
    FT_Face    face_    = nullptr;
    std::string path_;          // fontens egen fil och face — reserven hoppar
    int         faceIndex_ = 0; //   över den
    std::string pattern_;       // fontconfig-mönstret kandidaterna sorteras mot
    float sizePx_ = 0.0f;
    float ascent_ = 0.0f;
    float height_ = 0.0f;
    FontStyle style_ = FontStyle::Regular;   // det filen FAKTISKT är

    GlRenderer* renderer_ = nullptr;   // ägs av fönstret
    GlTexture   atlasTex_ = 0;
    int penX_ = 0, penY_ = 0, rowH_ = 0;
    std::unordered_map<char32_t, Glyph> glyphs_;
    // GLYFRESERVEN: facerna i den ordning de laddades (GlyphRef::face − 1)
    // och svaret per kodpunkt (också mätningens cache — FreeType muterar
    // facen vid Load, därav mutable bakom const).
    mutable std::vector<Reserve>                   reserves_;
    mutable std::unordered_map<char32_t, Resolved> resolved_;

    static constexpr int kAtlasW = 512;
    static constexpr int kAtlasH = 512;
};

// ---------------------------------------------------------------- renderer

// gfx::Renderer ovanpå EGL + GL — protokollneutral (får display/yta av
// fönstret). Spegel av d3d11_renderer.cpp: EN pipeline med SDF-pixelshader
// (GLSL-port av HLSL:ns mode 0–5), batcher med flush vid tillståndsbyte,
// retained-FBO + composite, glScissor-klipp (y-flipp!), glReadPixels-
// readback (radflipp!). eglSwapInterval(0) — en vsync-blockerad swap på en
// skymd Wayland-yta skulle hänga hela den enkeltrådade pumpen.
class GlRenderer : public Renderer {
public:
    // surf får vara EGL_NO_SURFACE (headless/surfacelöst — allt ritas i
    // retained-FBO:n och läses via captureRetained; composite/present
    // hoppar backbuffern).
    bool init(EGLDisplay dpy, EGLConfig cfg, EGLSurface surf,
              int w, int h, bool transparent);
    void shutdownRenderer();
    bool ready() const { return context_ != EGL_NO_CONTEXT; }

    bool makeCurrent();
    // Swap-interval per yta: 0 på Wayland (skymda ytor får inga frame-
    // callbacks — vsync-swap hade hängt pumpen; pacing sker i stället i
    // WaylandWindow::beforeSwap), 1 på X11 (vblank-styrt = säkert även
    // skymt — klassisk vsync-broms för animationer).
    void setSwapInterval(int interval);
    void resize(int w, int h);           // ny FBO (fysiska px)
    void beginFrame();                   // binder retained-FBO:n + state
    void composite();                    // FBO → backbuffer (mode 5-quad)
    void present(bool swap);             // eglSwapBuffers (swap) / glFlush
    // Presentationens väntan UTANFÖR renderaren — Waylands frame-callback-
    // väntan i `LinuxWindow::renderFrame` (`RenderStats::presentNs`);
    // swappen räknar renderaren själv.
    void addPresentNs(std::uint64_t ns) { stats_.presentNs += ns; }

    bool transparent() const { return transparent_; }
    bool retainedValid() const { return retainedValid_; }

    // Fönstrets hörnradie som MASK på compositen (fysiska px; 0 =
    // fyrkantigt). Det är här ett rundat fönster får sin form: masken
    // gäller hela retained-FBO:n, alltså ALLT som ritats i den —
    // widgetträdet OCH det en app målat själv i sin frame()-krok. Ett
    // rundat KLIPP i ritvägen hade krävt stencil eller per-quad-mask i
    // varje primitiv; här räcker en rad i den quad som ändå ritas.
    void setCompositeRadius(float r) { compositeRadius_ = r; }

    // STORLEKSMARGINALEN UTANFÖR YTAN, fysiska px; 0 =
    // ingen marginal. Ytan (backbuffern) är då
    // klientytan + 2m — fönstret sätter den storleken på sin EGL-yta — och
    // compositen rensar den till helt genomskinligt och lägger retained-
    // FBO:n m px in. Allt som ritas EFTER compositen (chromet, overlayen)
    // talar klientens px som vanligt: backbufferns viewport och scissor
    // förskjuts med marginalen (`bbOrigin_`). FBO:n, `captureRetained` och
    // render-målen rörs inte — de är klientytans storlek.
    void setFrameMargin(int m) { frameMargin_ = m > 0 ? m : 0; }

    // Readback (glaze-render): retained-FBO:n → rå RGBA8, rad 0 överst
    // (GL läser nerifrån — raderna flippas här).
    bool captureRetained(std::vector<std::uint8_t>& rgba, int& w, int& h);

    void  clear(Color c) override;
    void  fillRect(const Rectf& r, Color c) override;
    void  strokeRect(const Rectf& r, Color c) override;
    void  line(float x1, float y1, float x2, float y2, Color c) override;
    void  fillRoundedRect(const Rectf& r, float radius, Color c) override;
    void  strokeRoundedRect(const Rectf& r, float radius, Color c,
                            float width) override;
    void  dropShadow(const Rectf& r, float radius,
                     float offsetX, float offsetY, float blur,
                     Color c) override;
    void  innerShadow(const Rectf& r, float radius,
                      float offsetX, float offsetY, float blur,
                      Color c) override;
    float drawText(Font& f, float x, float y,
                   const std::string& utf8, Color c) override;
    std::unique_ptr<Font> loadFont(const std::string& path,
                                   float sizePt) override;
    std::unique_ptr<Font> loadFont(FontFamily family, float sizePt,
                                   FontStyle style) override;
    std::unique_ptr<Image> loadImage(const std::string& path) override;
    std::unique_ptr<Image> loadImage(const void* data,
                                     std::size_t size) override;
    // Helbildsformen är basens; opaciteten bärs i vertexfärgens alfa.
    using Renderer::drawImage;
    void drawImage(Image& img, const Rectf& src, const Rectf& dst,
                   float opacity) override;

    // Bilder ur pixlar (linux_image.cpp) — se renderer.h.
    // Med mip-kedja räknar `generateMips` om den efter varje
    // skrivning.
    using Renderer::createImage;
    std::unique_ptr<Image> createImage(int w, int h, ImageFormat f,
                                       const void* px, std::size_t stride,
                                       bool mipmaps) override;
    bool updateImage(Image& img, int x, int y, int w, int h, const void* px,
                     std::size_t stride) override;
    int  maxTextureSize() const override { return maxTextureSize_; }

    // Render-målen (linux_target.cpp; djup, MSAA och
    // läsningen) — se renderer.h.
    using Renderer::createRenderTarget;
    std::unique_ptr<RenderTarget> createRenderTarget(
        int w, int h, const TargetDesc& d) override;
    int  supportedSamples(int requested) const override;
    bool readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba) override;
    void destroyFramebuffer(unsigned fbo);    // GlRenderTarget:s destruktor
    void destroyRenderbuffer(unsigned rb);    // dito

    // Ritning utanför en frame (linux_renderer.cpp) — gör kontexten aktuell.
    bool beginOffscreen() override;
    void endOffscreen() override;

    // Integrationen med ett annat grafik-API (gfx/native.h) — stöds inte på
    // Linux (linux_renderer.cpp).
    NativeDevice nativeDevice() const override;
    std::unique_ptr<RenderTarget> importRenderTarget(
        const ExternalTargetDesc& desc, RenderTarget* sharedAttachments,
        std::string& error) override;
    void setExternalFramePacing(bool on) override;
    std::unique_ptr<GpuCompletion> insertCompletion(std::string& error) override;

    // Texturhjälpare för text/bild — håller ALLA GL-anrop i
    // linux_renderer.cpp (makeCurrent ingår). `rowBytes` är byte mellan två
    // rader i `px` (en multipel av pixelstorleken — UNPACK_ROW_LENGTH talar
    // pixlar); `px` nullptr ger en textur utan innehåll. 0 + logg vid fel.
    // `mipmaps` lämnar plats för en MIP-KEDJA — `generateMips`
    // fyller den; utan kedja är GL_TEXTURE_MAX_LEVEL 0.
    GlTexture createTexture(ImageFormat f, int w, int h, const void* px,
                            std::size_t rowBytes, bool mipmaps);
    void generateMips(GlTexture t);
    void updateTexture(GlTexture t, ImageFormat f, int x, int y, int w, int h,
                       const void* px, std::size_t rowBytes);
    void destroyTexture(GlTexture t);
    void texturedQuad(GlTexture t, const Rectf& dst, const Rectf& uv,
                      Color c);                       // mode 0 (R8-mask)
    // En bilds quad i mode 5–7 (RGBA rakt av / premultiplicerad / R8 som
    // gråskala) med bildens filter — se drawImage i linux_image.cpp.
    void imageQuad(GlTexture t, const Rectf& dst, const Rectf& uv,
                   float opacity, float mode, ImageFilter filter);

    // SAMPLERN ÄR BATCH-TILLSTÅND — två samplerobjekt, ett per filter,
    // bundna till enhet 0 vid VARJE ritning: UI:ts flush binder batchens,
    // scenens program sitt. Ett samplerobjekt SKUGGAR texturens egna
    // parametrar, så ett som lämnats bundet hade bestämt samplingen för
    // allt efter — därför binder varje ritväg sitt eget, och text och
    // composite tar Nearest uttryckligen.
    void bindSampler(ImageFilter f);

    // Scenen (linux_scene.cpp) — se renderer.h för
    // kontraktet.
    std::unique_ptr<GpuBuffer> createBuffer(BufferKind kind, std::size_t bytes,
                                            const void* data) override;
    bool        updateBuffer(GpuBuffer& b, std::size_t offset,
                             const void* data, std::size_t bytes) override;
    BufferSlice uploadTransient(BufferKind kind, const void* data,
                                std::size_t bytes) override;
    void        drawMesh(const MeshDraw& d) override;
    void        destroyBuffer(GlBuffer b);   // GlGpuBuffer:s destruktor

    // Egna program (linux_program.cpp) — se renderer.h
    // och gfx/program.h.
    std::unique_ptr<ShaderProgram> createProgram(const ProgramDesc& d) override;
    void destroyProgram(unsigned prog);   // programmets destruktor

protected:
    // Rundat barnklipp: scissorn (glScissor, y-flipp), plus de
    // rundade maskerna som UNIFORM-state i den befintliga pipelinen —
    // inga nya buffertar, inga nya flushar, oförändrat vertexformat.
    void applyClip(const Rectf* scissor, const RoundedClip* masks,
                   std::size_t count) override;

    // Render-målen (linux_target.cpp): FBO:n, glViewport och målets storlek
    // (scissorns höjd, scenens px → NDC, UI-programmets vp) för `t`, eller
    // framens eget mål för nullptr.
    void bindTarget(RenderTarget* t) override;

private:
    // Samma vertexformat som d3d11_backend.h:s Vertex — SDF-parametrarna
    // bär varje vertex så primitiven batchas i samma pipeline.
    struct Vertex {
        float         x, y;
        float         u, v;
        std::uint32_t rgba;
        float         p0[4];   // SDF: centrum.xy, halvmått.zw (px)
        float         p1[4];   // SDF: radie, bredd/blur, offset.xy
        float         mode;
    };
    // Vertexformatet är ETT designargument (masken
    // ligger i en UNIFORM och inte per vertex just för att de 56 bytena
    // inte ska växa i det vanliga fallet). Formatet är dessutom identiskt i
    // alla tre backends — det får inte glida tyst.
    static_assert(sizeof(Vertex) == 56, "Vertex måste vara 56 byte");

    // DE RUNDADE KLIPPMASKERNA och MASKGENERATIONEN.
    // Uniformer är per PROGRAM i GL, så maskerna LAGRAS här
    // och laddas upp LAT till det program som ritar: `setClipMasks` stegar
    // generationen när de ändras, och `syncClipMasks` laddar upp till det
    // BUNDNA programmet om dess generation är gammal. Inga masker nu och
    // inga förut ⇒ ingen generation stegas och inte ett GL-anrop görs.
    void setClipMasks(const RoundedClip* masks, std::size_t count);
    void syncClipMasks(int p0Loc, int p1Loc, int nLoc, unsigned& programGen);
    float    clipP0_[kMaxRoundedClips * 4] = {};   // centrum.xy + halvmått.zw
    float    clipP1_[kMaxRoundedClips * 4] = {};   // .x = radien
    int      clipCount_   = 0;
    unsigned clipGen_     = 0;   // stegas när maskerna ändras
    unsigned uiClipGen_   = 0;   // vad UI-programmet senast fick
    // (Line- och Glyph-programmens generationer står i deras structar nedan.)

    // VILKET PROGRAM som är bundet. `drawMesh` lämnar scenens program och
    // VAO kvar; `flush()` — det enda stället UI-pipelinen ritar —
    // återbinder UI:ts program, VAO och GL_ARRAY_BUFFER innan den ritar.
    enum class Pipeline { Ui, Line, Image, Fill, Glyph, Line3, Glyph3, Custom };
    Pipeline pipeline_ = Pipeline::Ui;
    void bindUiPipeline();

    // RENDERLÄGET (linux_renderer.cpp) — GL:s tillstånd
    // per rit-anrop: djuptestet, polygon offset, gallringen och blandningen
    // med färgmasken. Varje scenprogram tillämpar anropets läge med
    // `applyMeshState`, och UI-pipelinen sätter TILLBAKA sitt (djup av, ingen
    // gallring, rak alfa) när den tas i bruk — före UI:ts flush, och i
    // `clear`, som annars lyder djup- och färgmasken. Cachen nedan är det som
    // är satt; -1 = okänt (framens början), så nästa sättare sätter.
    void applyMeshState(const RenderState& s);
    void applyUiState();
    void resetStateCache();
    void setDepthState(bool on, unsigned func, bool write);
    void setOffsetState(bool on, float slope, float units);
    void setCullState(Cull c);
    void setBlendState(Blend b);
    int   glDepthOn_   = -1;   // 0/1, -1 okänt
    int   glDepthFunc_ = -1;
    int   glDepthMask_ = -1;
    int   glOffsetOn_  = -1;
    float glSlope_     = 0.0f;
    float glUnits_     = 0.0f;
    int   glCull_      = -1;   // Cull, -1 okänt
    int   glBlend_     = -1;   // Blend, -1 okänt

    // MÅLEN MED DJUP OCH MSAA (linux_target.cpp). `bound_` är målet som är
    // bundet (nullptr = framens eget) — `bindTarget` löser upp det med en
    // blit när det lämnas. `maxSamples_` = GL_MAX_SAMPLES (init).
    GlRenderTarget* bound_         = nullptr;
    int             maxSamples_    = 1;
    // GL_MAX_TEXTURE_SIZE (init) — före init GL 3.3:s garanterade
    // minimum.
    int             maxTextureSize_ = 1024;
    bool            samplesLogged_ = false;
    void resolveTarget(GlRenderTarget& t);

    // Framen och blocket utanför den (beginOffscreen).
    bool inFrame_   = false;
    bool offscreen_ = false;

    // GPU-TIDEN (renderer.h `setGpuTiming`): en
    // ring om `kGpuQueries` frames med två `GL_TIMESTAMP`-frågor var
    // (`glQueryCounter`). `beginFrame` läser de färdiga
    // (`GL_QUERY_RESULT_AVAILABLE` — ingen väntan) och börjar en ny när
    // tidtagningen är på och en plats är ledig; `present` avslutar den före
    // swappen. Utan frågorna (gl_common.h `loadGlTimerQueries`) mäts
    // ingenting — `gpuTimingSupported_` falskt.
    struct GpuQuery {
        unsigned      begin = 0, end = 0;
        std::uint64_t serial = 0;   // framen den mäter; 0 = ledig
    };
    static constexpr int kGpuQueries = 4;
    GpuQuery             gpuQueries_[kGpuQueries];
    int                  gpuQueryOpen_ = -1;   // platsen framen mäter i
    bool initGpuQueries();
    void shutdownGpuQueries();
    void collectGpuQueries();
    void startGpuQuery();

    // Scenens livscykel (linux_scene.cpp).
    bool initScene();
    void shutdownScene();
    void beginSceneFrame();

    // Den transienta ringen per buffertslag — se d3d11_backend.h:s
    // TransientRing; här orphanas lagringen vid framens första
    // uppladdning i stället för DISCARD.
    struct GlTransientRing {
        std::unique_ptr<GlGpuBuffer>              buf;
        std::size_t                               offset = 0;
        bool                                      fresh  = true;
        std::vector<std::unique_ptr<GlGpuBuffer>> retired;
    };
    GlTransientRing transient_[2];   // [0] vertex, [1] index

    // MÅLARENS KLIPP: kantplanen som uniformer PER PROGRAM
    // (kGlslClipSrc: `uSceneClip`/`uSceneClipN`) — ett scenprograms locations
    // och hur många plan det senast fick. `findSceneClip` slår upp dem efter
    // länkningen (false när någon saknas — källan och C++-sidan har glidit
    // isär), och `setSceneClip` laddar upp anropets klipp med programmet
    // bundet: inget klipp nu och inget förut ⇒ inget GL-anrop (länkningen
    // nollar uniformerna, så programmet börjar utan klipp).
    struct SceneClipLocs {
        int planes = -1;
        int count  = -1;
        int sent   = 0;
    };
    bool findSceneClip(unsigned program, SceneClipLocs& l);
    void setSceneClip(const MeshDraw& d, SceneClipLocs& l);

    // LINE-PROGRAMMET och dess LINE3-variant (samma
    // källa med GLAZE_POS3, punkterna i xyz): två länkade program med var
    // sina locations, VAO och maskgeneration (uniformer är per program i GL).
    // Misslyckas Line3 loggas det och linjerna i rymden ritas inte.
    struct LineProgram {
        unsigned program  = 0;
        unsigned vao      = 0;
        int      viewProj = -1;
        int      params   = -1;
        int      clipP0   = -1;
        int      clipP1   = -1;
        int      clipN    = -1;
        int      paint0   = -1;   // färgen (linux_fill.cpp)
        int      paint1   = -1;
        int      stroke   = -1;   // konturen: spetsgränsen och strecken
        int      dash     = -1;
        unsigned clipGen  = 0;    // vad programmet senast fick av maskerna
        SceneClipLocs sceneClip;  // målarens klipp
    };
    LineProgram line_, line3_;
    bool buildLineProgram(bool pos3, LineProgram& p);
    void releaseLineProgram(LineProgram& p);

    // IMAGE-PROGRAMMET (linux_image.cpp): världsquads ur
    // `ImageVertex`, perspektivkorrekt sampling, de projektiva vikterna `q`
    // (attribut 2 — en bild på fyra fria hörn),
    // opaciteten, bildens flaggor och färgjusteringen (`uAdjust`).
    // Egna uniformer (per program i GL) och egen maskgeneration.
    bool initImageProgram();
    void shutdownImageProgram();
    void drawImageMesh(const MeshDraw& d);
    unsigned imageProgram_     = 0;
    unsigned imageVao_         = 0;
    int      imageViewProjLoc_ = -1;
    int      imageParamsLoc_   = -1;
    int      imageAdjustLoc_   = -1;
    int      imageClipP0Loc_   = -1;
    int      imageClipP1Loc_   = -1;
    int      imageClipNLoc_    = -1;
    unsigned imageClipGen_     = 0;   // vad Image-programmet senast fick
    SceneClipLocs imageSceneClip_;    // målarens klipp

    // FILL-PROGRAMMET (linux_fill.cpp): ytorna ur
    // `FillVertex` och färgen — se d3d11_fill.cpp. Egna uniformer och egen
    // maskgeneration, som de andra programmen.
    bool initFillProgram();
    void shutdownFillProgram();
    void drawFillMesh(const MeshDraw& d);
    // Färgens uniformer och rampen på enhet 0 med det linjära samplerobjektet
    // — delade av Fill-, Line- och Glyph-programmen (locations per program).
    void setPaint(const MeshDraw& d, int paint0Loc, int paint1Loc);
    unsigned fillProgram_     = 0;
    unsigned fillVao_         = 0;
    int      fillViewProjLoc_ = -1;
    int      fillParamsLoc_   = -1;
    int      fillPaint0Loc_   = -1;
    int      fillPaint1Loc_   = -1;
    int      fillClipP0Loc_   = -1;
    int      fillClipP1Loc_   = -1;
    int      fillClipNLoc_    = -1;
    unsigned fillClipGen_     = 0;   // vad Fill-programmet senast fick
    SceneClipLocs fillSceneClip_;    // målarens klipp

    // GLYPH-PROGRAMMET (linux_glyph.cpp): text och
    // markörer som billboards — se d3d11_glyph.cpp. Och dess GLYPH3-variant
    // (samma källa med GLAZE_POS3, ankaret i xyz), som
    // Line3 ett eget länkat program.
    bool initGlyphProgram();
    void shutdownGlyphProgram();
    void drawGlyphMesh(const MeshDraw& d);
    struct GlyphProgram {
        unsigned program  = 0;
        unsigned vao      = 0;
        int      viewProj = -1;
        int      params   = -1;
        int      paint0   = -1;
        int      paint1   = -1;
        int      clipP0   = -1;
        int      clipP1   = -1;
        int      clipN    = -1;
        unsigned clipGen  = 0;    // vad programmet senast fick av maskerna
        SceneClipLocs sceneClip;  // målarens klipp
    };
    GlyphProgram glyph_, glyph3_;
    bool buildGlyphProgram(bool pos3, GlyphProgram& p);
    void releaseGlyphProgram(GlyphProgram& p);

    // EGNA PROGRAM (linux_program.cpp). EN VAO för alla
    // egna program (`customVao_`): attributen slås på och av per rit-anrop
    // efter layouten (`customEnabled_`, bit N = plats N) — med en tom layout
    // är den helskärmspassets tomma VAO. GlazeFrame och GlazeUser i var sin
    // uniform-buffert, omskriven (orphan) per anrop och bunden på
    // bindningspunkterna 3 och 4. `curProgram_` är det BUNDNA programmets
    // löpnummer, aldrig en adress.
    bool initProgramSupport();
    void shutdownProgramSupport();
    void drawCustomMesh(const MeshDraw& d);
    unsigned      customVao_     = 0;
    unsigned      frameUbo_      = 0;
    unsigned      userUbo_       = 0;
    std::uint32_t customEnabled_ = 0;
    std::uint64_t curProgram_    = 0;

    // Samplerobjekten (se bindSampler).
    unsigned samplerNearest_ = 0;
    unsigned samplerLinear_  = 0;

    void flush();
    void setState(GlTexture tex, unsigned topo, ImageFilter filter);
    void quad(const Rectf& dst, const Rectf& uv, Color c);
    void sdfQuad(const Rectf& dst, Color c, float mode, const Rectf& prim,
                 float radius, float param, float offX, float offY);
    bool createOffscreen(int w, int h);   // retained-FBO:n (textur + FBO)
    bool buildPipeline();                 // shaders + VBO/VAO (runtime-GLSL)

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;

    unsigned program_   = 0;
    int      vpUniform_ = -1;
    // Rundade klippmasker: UI-programmets locations.
    // Värdena och generationen står vid `setClipMasks` ovan.
    int      clipP0Uniform_    = -1;
    int      clipP1Uniform_    = -1;
    int      clipCountUniform_ = -1;
    unsigned vao_       = 0;
    unsigned vbo_       = 0;
    GlTexture whiteTex_ = 0;

    // Retained-FBO:n (som d3d11:s offTex_/offRtv_/offSrv_).
    GlTexture offTex_ = 0;
    unsigned  offFbo_ = 0;
    bool      retainedValid_ = false;
    float     compositeRadius_ = 0.0f;   // fönsterformen (fysiska px)
    bool      inBackbuffer_  = false;   // efter composite (chrome/overlay)
    // STORLEKSMARGINALEN (se setFrameMargin) och det BUNDNA målets
    // förskjutning i GL:s fönsterkoordinater: marginalen när backbuffern är
    // målet, annars 0. Viewporten och scissorn läggs på den — `targetW_/H_`
    // förblir klientytans, så vertexshaderns px → NDC är orörd.
    int       frameMargin_   = 0;
    int       bbOrigin_      = 0;
    // Förskjutningen för framens eget mål när det är backbuffern (fbo 0) —
    // EN regel för compositen, popTarget och det degraderade läget.
    int       backbufferOrigin() const
    {
        return surface_ != EGL_NO_SURFACE && !offscreen_ ? frameMargin_ : 0;
    }

    std::vector<Vertex> batch_;
    GlTexture   curTex_    = 0;
    unsigned    curTopo_   = 4;   // GL_TRIANGLES
    ImageFilter curFilter_ = ImageFilter::Nearest;
    int  width_  = 0;
    int  height_ = 0;
    // Det BUNDNA målets storlek — glScissor räknar nerifrån mot DESS höjd
    // (y-flipp nr 1) och scenens vertexshader räknar px → NDC mot den.
    // Retained-FBO:n och backbuffern är lika stora; ett render-mål
    // är det inte.
    int  targetW_ = 0;
    int  targetH_ = 0;
    // Den BUNDNA FBO:n (0 = backbuffern) — det `createRenderTarget` binder
    // tillbaka efter att ha rensat sitt nya mål mitt i en frame.
    unsigned currentFbo_ = 0;
    // Vad UI-programmets vp-uniform senast fick: flush laddar upp målets
    // storlek när den skiljer (uniformen är per program, som maskerna).
    int  uiVpW_ = 0;
    int  uiVpH_ = 0;
    bool transparent_ = false;
    bool scissorOn_   = false;

    static constexpr std::size_t kMaxVerts = 65536;
};

// ---------------------------------------------------------------- fönster

// Gemensam bas för Wayland-/X11-/headless-fönstren: callbacks, chrome,
// hit-test-kartan, timerfd-timers, renderFrame-sekvensen och readbacken.
// Subklasserna äger OS-ytan och event-översättningen.
class LinuxWindow : public Window {
public:
    // Ytans FAKTISKA alfa-status (fasadens fråga): `WindowDesc::transparent`
    // är en begäran, och X11 utan ARGB-visual degraderar den till opakt.
    bool transparent() const override { return transparent_; }

    // Chromets hörnradie (fysiska px) — ramen måste följa ett rundat
    // fönster. Pushas per repaint av AppWindow. Radien går VIDARE till
    // compositen: det är där fönsterformen faktiskt klipps, så allt som
    // ritats i retained-FBO:n får den — även rects en app målat själv i
    // sin frame()-krok, som annars fyller igen hörnen.
    void setChromeRadius(float radius) override
    {
        chromeRadius_ = radius;
        renderer_.setCompositeRadius(radius);
    }

    ~LinuxWindow() override;

    WindowId id() const override { return windowId_; }
    bool processEvent(const Event& e) override;
    void setRenderCallback(RenderFn fn) override { renderCb_ = std::move(fn); }
    void setOverlayCallback(RenderFn fn) override
    {
        overlayCb_ = std::move(fn);
    }
    void renderFrame() override;
    void drawChrome() override;
    Rectf contentArea() const override;
    float titlebarHeight() const override;

    bool takeDirty() override
    {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

    void setChromeColors(const ChromeColors& c) override
    {
        chromeColors_ = c;
    }
    void setHitRegions(std::vector<HitRegion> regions) override
    {
        hitRegions_ = std::move(regions);
    }
    void setCaptionHeight(float logicalPx) override
    {
        captionH_ = logicalPx;
    }

    // timerfd i epoll — väcker waitEvent per konstruktion, ingen tråd.
    void startTimer(std::uint32_t id, std::uint32_t ms) override;
    void stopTimer(std::uint32_t id) override;
    void stopAllTimers();

    void setTextInputActive(bool) override {}   // xkbcommon levererar alltid
                                                // (zwp_text_input_v3 = senare)
    bool retainedValid() const override { return renderer_.retainedValid(); }
    bool captureFrame(std::vector<std::uint8_t>& rgba, int& w,
                      int& h) override;
    Renderer& renderer() override { return renderer_; }

    // Hit-test-uppslaget (första träffen vinner — som D3D11). Punkten i
    // KLIENTENS fysiska px. Med en storleksmarginal svarar en punkt i
    // marginalen med sin kant (`frame::marginRole`, ../frame_margin.h), och
    // kartans resize-zoner INNANFÖR klientytan hoppas över — ramen ligger
    // utanför, och zonerna hade tagit de yttersta px av allt innehåll vid
    // kanten. Utan marginal läses kartan som vanligt.
    HitRole hitRoleAt(float x, float y) const;
    // Storleksmarginalen just nu, fysiska px (0 = ingen).
    int frameMargin() const { return frameMargin_; }

    // MUSPEKAREN: den BEGÄRDA formen bor i basen, så gettern är
    // en och densamma för Wayland, X11 och headless. Sättaren är
    // protokollets — `setCursor` overridas per subklass och lagrar HÄR.
    PointerShape cursor() const override { return cursor_; }

    // Portalens textskalning ändrades (broadcast-vägen i
    // linux_platform.cpp): fönstret räknar om buffert/ytenheter ur sin
    // lagrade LOGISKA storlek. Default no-op — X11:s skala bär
    // textskalningen via Xft.dpi (egen väg: RESOURCE_MANAGER-bevakningen).
    virtual void onTextScaleChanged() {}

protected:
    // Subklassens krokar för renderFrame-sekvensen. beforeSwap körs bara
    // när swap faktiskt ska ske (Wayland: frame-callback-pacing).
    virtual bool canRender() const { return renderer_.ready(); }
    virtual bool shouldSwap() const { return true; }
    virtual void beforeSwap() {}
    virtual Sizef pixelSize() const = 0;   // buffertstorlek (fysiska px)

    GlRenderer  renderer_;
    RenderFn    renderCb_;
    RenderFn    overlayCb_;
    ChromeColors chromeColors_;
    std::vector<HitRegion> hitRegions_;
    float       captionH_ = 32.0f;   // logiska px (TopBar-defaulten)
    WindowId    windowId_ = 0;
    bool        dirty_    = false;
    Chrome      chrome_   = Chrome::Native;
    bool        transparent_ = false;
    float       chromeRadius_ = 0.0f;   // fönstrets hörn (fysiska px)
    bool        hidden_      = false;
    PointerShape cursor_      = PointerShape::Arrow;   // den begärda formen
    std::string title_;
    // STORLEKSMARGINALEN UTANFÖR YTAN i
    // fysiska px — 0 utan marginal. Ytan är klientytan + 2m, genomskinlig i
    // marginalen, och fönstrets geometri för fönsterhanteraren är klientytan:
    // Wayland `xdg_surface.set_window_geometry(m, m, b, h)`, X11
    // `_GTK_FRAME_EXTENTS` (kräver en kompositör och WM-stöd). Indata räknas
    // om med −m till klientens px, och renderaren förskjuter compositen
    // (`GlRenderer::setFrameMargin`). 0 för popuper, Native/None och
    // maximerat/tiled/helskärm, och på Wayland för ett fast fönster.
    // Subklassen sätter värdet.
    int         frameMargin_ = 0;
    // Storleksändrar marginalen? false för ett FAST fönster (min == max) som
    // ändå har en marginal (X11 — geometrin byts aldrig vid fast ↔
    // ändringsbart): en punkt där svarar då Client, ingen pil och inget drag,
    // annars hade ytan lovat något OS:et vägrar.
    bool        frameResizable_ = true;

private:
    // Fönstrets timers — samma källa som appens (`TimerSource` ovan).
    std::unordered_map<std::uint32_t, std::unique_ptr<TimerSource>> timers_;
};

// ---------------------------------------------------------------- display

// Protokollväxeln: EN implementation väljs i gfx::init (Wayland → X11 →
// headless). Pumpen i linux_platform.cpp är gemensam och pratar med
// displayen via prepoll/postpoll runt epoll_wait.
class LinuxDisplay {
public:
    virtual ~LinuxDisplay() = default;

    virtual EGLDisplay egl() const = 0;
    virtual std::unique_ptr<Window> makeWindow() = 0;

    // Pump-krokar: prepoll före epoll_wait (dispatcha köat + flusha
    // utgående; Wayland kör prepare_read-dansen), postpoll efter
    // (fd läsbar → läs + översätt → pushEvent), abortPoll när kön
    // fylldes redan i prepoll (Wayland: cancel_read).
    virtual void prepoll() {}
    virtual void postpoll(bool fdReady) { (void)fdReady; }
    virtual void abortPoll() {}
    virtual int  fd() const { return -1; }

    virtual std::string clipboardText() { return {}; }
    virtual void setClipboardText(const std::string&) {}

    // SKÄRMARNA i placeringsrymden — `gfx::screens()`
    // frågar den gällande displayen. Förval TOM: Wayland har ingen skärmrymd
    // och headless ingen skärm. X11 svarar ur XRandR och `_NET_WORKAREA`.
    virtual std::vector<ScreenInfo> placementScreens() const { return {}; }

    // Singleton-lagringen (sätts av gfx::init, rivs av gfx::shutdown).
    static LinuxDisplay*& current();
};

// Fabriker (nullptr när protokollet inte går att nå/saknar globals).
LinuxDisplay* createWaylandDisplay();
LinuxDisplay* createX11Display();
LinuxDisplay* createHeadlessDisplay();   // surfacelös EGL (glaze-render/SSH)

// Fasadens fönsterobjekt på Linux är en PROXY (konkret typ väljs först i
// init, när displayen är vald). Backendkod som får ett gfx::Window*
// utifrån (WindowDesc::owner, popupAnchorWindow) packar upp det här —
// static_cast direkt vore fel objekt.
class InnerWindowHolder {
public:
    virtual ~InnerWindowHolder() = default;
    virtual Window* innerWindow() = 0;
};
LinuxWindow* unwrapWindow(Window* w);

} // namespace gfx
