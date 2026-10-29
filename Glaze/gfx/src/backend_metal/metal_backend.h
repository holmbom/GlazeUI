#pragma once
// Intern header för Metal-backenden — ligger i src/, INTE include/, och får
// aldrig inkluderas utanför backend_metal/. Bär ObjC-typer (ARC gör pekar-
// medlemmarna i C++-klasserna implicit __strong — sådana objekt får ALDRIG
// memcpy:as/memset:as) och får därför bara inkluderas av .mm-filer.
// AppKit-/Metal-typer får bara förekomma här och i backend_metal/*.mm;
// utåt syns enbart fasaden i include/gfx/.
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../font_fallback.h"
#include "gfx/renderer.h"
#include "gfx/scene.h"
#include "gfx/text.h"
#include "gfx/window.h"

@class GlazeView;
@class GlazeWindowDelegate;

namespace gfx {

// Intern event-kö: GlazeView/fönster-delegaten översätter NSEvents till
// gfx::Event och köar dem här; waitEvent/pollEvent (metal_platform.mm)
// dränerar kön. pushEvent postar ALLTID ett väcknings-event
// (NSEventTypeApplicationDefined) — annars sover nextEventMatchingMask
// vidare när NSTimer/KVO köat något utan att producera ett NSEvent
// (motsvarigheten till D3D11-pumpens fälla).
void pushEvent(const Event& e);

// Fönsterregistret för SystemThemeChanged-utskicket: KVO:n på
// NSApp.effectiveAppearance (metal_platform.mm) skickar ett event per
// öppet fönster — spegeln av WM_SETTINGCHANGE-per-fönster i D3D11.
void registerWindowId(WindowId id);
void unregisterWindowId(WindowId id);

// Klippmaskernas MSL (metal_renderer.mm): `#include <metal_stdlib>`,
// sdRoundRect, `struct ClipMasks` och clipCoverage. Klistras före VARJE
// bibliotek vars fragmentfunktion ritar i ett mål med klippstacken —
// UI-pipelinens och scenens. Och målarens klipp:
// `struct SceneClip` och `sceneClip(p, sc)`, som scenens fragmentfunktioner
// multiplicerar alfan med och som KASTAR fragmentet utanför.
extern const char kMslClipSrc[];

// Färgens MSL (metal_fill.mm): `struct PaintU` och
// `paintColor` — klistras efter klippmaskernas del i Fill-, Line- och
// Glyph-programmen.
extern const char kMslPaintSrc[];

// Färgjusteringens MSL (metal_program.mm) — `glaze_colorAdjust(c,
// a)`, gfx/program.h: HLSL:ns kHlslAdjustSrc rad för rad. EN definition: i de
// egna programmens förspel (där ritytans Mesh-program når den) och i
// Image-programmet, efter klippmaskernas del (som bär metal_stdlib).
extern const char kMslAdjustSrc[];

// De skärmvända hörnens djup (metal_program.mm) —
// `glaze_standingZ(m, c, off)`, gfx/program.h: HLSL:ns kHlslStandSrc rad för
// rad. EN definition: i de egna programmens förspel (där ritytans
// Points-program når den) och efter färgens del i Line- och Glyph-programmen.
extern const char kMslStandSrc[];

// GPU-PREFERENSENS enhet (metal_gpu.mm): kortet `gpuAdapter()` när det finns och inte är
// isHeadless, annars ur MTLCopyAllDevices den första som inte är (resp. är)
// isLowPower, annars systemets förval — loggat en gång. GlazeView:s lager och
// renderaren tar den (gfx/platform.h `setGpuPreference`/`setGpuAdapter`).
// `metalAdapterId` är enhetens `GpuAdapter::id` (tomt utan träff), och
// `logDevice` skriver varje NY enhet med id:t.
id<MTLDevice> preferredMetalDevice();
std::string   metalAdapterId(id<MTLDevice> d);
void          logDevice(id<MTLDevice> d, const std::string& adapterId);

// SYSTEMFÄLTETS LIVSTID (metal_tray.mm): `trayInit` öppnar
// fasaden i gfx/tray.h (sist i `gfx::init`), `trayShutdown` tar bort varje
// statuspost och stänger den igen (först i `gfx::shutdown`). Utanför paret
// vägrar `setTrayIcon` och resten gör ingenting — fasadens löfte att vara
// säker före init och efter shutdown, också vid ett nytt init.
void trayInit();
void trayShutdown();

// APPENS TIMER (metal_timer.mm) — samma livstid som
// systemfältet: `appTimerInit` öppnar fasaden i gfx/timer.h (i `gfx::init`),
// `appTimerShutdown` invaliderar varje timer och stänger den (i
// `gfx::shutdown`). Utanför paret svarar `startAppTimer` false.
void appTimerInit();
void appTimerShutdown();

// IKONEN SOM BITMAPSREPRESENTATION (metal_window.mm) — delad av dockans ikon
// och systemfältets, så formen står EN gång. `iconRep` gör en ram till en
// representation med RAK alfa, rad 0 överst och täta rader (ramens egen
// form); `points` är punktstorleken, alltså tätheten och inte pixlarna. nil
// när AppKit vägrar. `bundleHasIcon` säger att paketet har en egen ikon
// (`CFBundleIconFile`) — den står i dockan och vinner med `programFirst`.
NSBitmapImageRep* iconRep(const IconImage& im, NSSize points);
bool              bundleHasIcon();

// PLACERINGSRYMDEN (metal_window.mm): en AppKit-rect (punkter, origo nere till
// vänster på huvudskärmen) vänd mot huvudskärmens höjd till fasadens
// top-left-konvention — samma rymd som `setPosition`/`screenArea`. Delad av
// fönstret och systemfältet, så vändningen står EN gång.
Rectf toPlacement(NSRect r);

class MetalRenderer;

// gfx::GpuBuffer ovanpå en MTLBuffer (metal_scene.mm). Statiska buffertar
// ligger i DELAD lagring på en GPU med delat minne (Apple silicon) och i
// MANAGED på en diskret, och skrivs på den väg STÄMPELN
// väljer (`MetalRenderer::uploadPathFor`): aldrig använd ⇒ direkt i
// `contents`, använd i en föregående frame ⇒ en blit i uppladdningspasset före
// framen, använd i SAMMA frame ⇒ ett encoder-brott. Den transienta ringens är
// delade. Hör till rendererns device — släpp bufferten före fönstret.
class MetalGpuBuffer : public GpuBuffer {
public:
    MetalGpuBuffer(BufferKind kind, std::size_t size, id<MTLBuffer> buf)
        : kind_(kind), size_(buf ? size : 0), buf_(buf)
    {
    }

    BufferKind  kind() const override { return kind_; }
    std::size_t size() const override { return size_; }
    bool        valid() const override { return buf_ != nil; }

    id<MTLBuffer> buffer() const { return buf_; }

    // STÄMPELN: framen (eller offscreen-blocket) som senast REFERERADE
    // bufferten i ett kommando, 0 = aldrig. Sätts bara av `drawMesh`, som når
    // bufferten genom en const-del (`BufferSlice`) — därav `mutable`.
    std::uint64_t usedIn() const { return usedIn_; }
    void          noteUse(std::uint64_t serial) const { usedIn_ = serial; }

private:
    BufferKind            kind_;
    std::size_t           size_;
    id<MTLBuffer>         buf_    = nil;
    mutable std::uint64_t usedIn_ = 0;
};

// gfx::Image ovanpå en Metal-textur (metal_image.mm). Bilder ur pixlar
// (`createImage` — loadImage avkodar med Image I/O och går samma väg) fylls
// med replaceRegion när de skapas och uppdateras på stämpelns väg, som
// buffertarna: aldrig ritad ⇒ replaceRegion, ritad i en föregående frame ⇒
// uppladdningspasset, ritad i SAMMA frame ⇒ ett encoder-brott (se
// `uploadPathFor`). CG ritar BARA premultiplicerat, så avkodningen
// av-premultiplicerar på CPU — pipelinen blandar rak alfa, exakt som
// WIC-vägen. Texturen hör till rendererns device — släpp bilden före
// fönstret. Default-konstruerad = ogiltig.
class MetalImage : public Image {
public:
    MetalImage() = default;
    MetalImage(id<MTLTexture> tex, int w, int h, ImageFormat f,
               bool premultiplied, bool mipmapped)
        : tex_(tex), w_(w), h_(h), format_(f), premultiplied_(premultiplied),
          mipmapped_(mipmapped)
    {
    }

    int  width() const override { return tex_ ? w_ : 0; }
    int  height() const override { return tex_ ? h_ : 0; }
    bool valid() const override { return tex_ != nil; }
    ImageFormat format() const override { return format_; }
    bool premultiplied() const override { return premultiplied_; }
    bool flippedV() const override { return false; }   // rad 0 överst alltid

    id<MTLTexture> texture() const { return tex_; }
    // Har texturen en MIP-KEDJA? `MetalRenderer::generateMips`
    // räknar den ur nivå 0.
    bool           mipmapped() const { return mipmapped_; }

    // STÄMPELN (som `MetalGpuBuffer`s): sätts av `drawMesh` och `drawImage`,
    // 0 = aldrig ritad. Också av `generateMips` — en kodad blit refererar
    // texturen lika mycket som ett rit-anrop.
    std::uint64_t usedIn() const { return usedIn_; }
    void          noteUse(std::uint64_t serial) const { usedIn_ = serial; }

private:
    id<MTLTexture>        tex_ = nil;
    int                   w_ = 0, h_ = 0;
    ImageFormat           format_        = ImageFormat::Rgba8;
    bool                  premultiplied_ = false;
    bool                  mipmapped_     = false;
    mutable std::uint64_t usedIn_        = 0;
};

// gfx::RenderTarget ovanpå en Metal-textur som render-attachment
// (metal_target.mm). BGRA8Unorm som retained-RT:n
// — PSO:erna är byggda för just det formatet. Bilden är texturen sedd som en
// PREMULTIPLICERAD MetalImage, rättvänd (Metal har rad 0 överst). Hör till
// rendererns device — släpp målet före fönstret.
//
// DJUP OCH MSAA: med provtagning > 1 ritas det i en
// egen 2DMultisample-textur (`msaa`) som ett eget UPPLÖSNINGSPASS löser upp
// till bilden när målet lämnas; djupet är Depth32Float med samma provtagning
// (Apple silicon saknar Depth24Stencil8). PSO:n BAKAR provtagningen och
// djupformatet, så varje program har en variant per mål (`psoFor`).
class MetalRenderTarget : public RenderTarget {
public:
    MetalRenderTarget() = default;   // ogiltigt (fabrikens svar vid fel)
    MetalRenderTarget(id<MTLTexture> tex, id<MTLTexture> msaa,
                      id<MTLTexture> depth, int w, int h, int samples,
                      bool mipmapped)
        : image_(tex, w, h, ImageFormat::Rgba8, true, mipmapped), msaa_(msaa),
          depth_(depth), samples_(samples)
    {
    }

    Image& image() override { return image_; }
    int    samples() const override { return image_.valid() ? samples_ : 1; }
    bool   hasDepth() const override { return depth_ != nil; }

    id<MTLTexture> texture() const { return image_.texture(); }   // bilden
    bool mipmapped() const { return image_.mipmapped(); }   // har mip-kedja
    // Det som RITAS i: den provtagna ytan med MSAA, annars bilden själv.
    id<MTLTexture> colorTexture() const
    {
        return msaa_ ? msaa_ : image_.texture();
    }
    id<MTLTexture> msaaTexture() const { return msaa_; }    // nil utan MSAA
    id<MTLTexture> depthTexture() const { return depth_; }  // nil utan djup

private:
    MetalImage     image_;
    id<MTLTexture> msaa_    = nil;
    id<MTLTexture> depth_   = nil;
    int            samples_ = 1;
};

// gfx::Font ovanpå Core Text — spegeln av D3dFont/DirectWrite-vägen:
// descriptor från fil (klarar .ttc), design-advances utan rasterisering,
// gråskale-glyfer (kCGImageAlphaOnly, ingen subpixel) → samma R8-atlas-
// packare. Atlasen hör till renderern — fonten släpps före device:n
// (samma städordning som D3D11). font_ är ett CF-objekt: MANUELL
// CFRelease (CF-typer ägs inte av ARC). GLYFRESERVEN:
// en glyf fonten saknar tas ur Core Texts kedja
// (`CTFontCreateForString`) — regeln i ../font_fallback.h, upplöst EN gång per
// kodpunkt; reservfonterna är också CF-objekt med manuell CFRelease.
class MetalFont : public Font {
public:
    MetalFont() = default;
    ~MetalFont() override { unload(); }
    MetalFont(const MetalFont&)            = delete;
    MetalFont& operator=(const MetalFont&) = delete;

    bool load(const std::string& path, float sizePt);
    // Familjen i stilen via font_catalog — samlingsfilerna (.ttc) bär
    // stilarna själva, så Core Text väljer stilen ur dem
    // (CTFontCreateCopyWithSymbolicTraits).
    bool loadFamily(FontFamily f, float sizePt, FontStyle style);
    void unload();

    // Kopplar fonten till rendererns device — atlas-texturen skapas lazy
    // vid första draw. Sätts av MetalRenderer::loadFont/loadFamily.
    void setDevice(id<MTLDevice> dev) { device_ = dev; }

    // Intern ritväg — anropas av MetalRenderer::drawText och chromet.
    // Ritar UTF-8-text med övre vänstra hörnet i (x, y); returnerar bredden.
    float draw(MetalRenderer& r, float x, float y,
               const std::string& utf8, Color color);

    Sizef measure(const std::string& utf8) const override;
    float height() const override;
    bool  valid() const override { return font_ != nullptr; }

    // Text-API:t — allt via design-advances (samma väg som measure;
    // rasteriserar aldrig, funkar utan device).
    float prefixWidth(const std::string& utf8,
                      std::size_t bytes) const override;
    std::size_t byteIndexAtX(const std::string& utf8,
                             float x) const override;
    std::size_t fitWidth(const std::string& utf8,
                         float maxW) const override;

    // Ritytans text: stilen läses ur fontens
    // symboliska drag och rasteriseringen är DEN som atlasen fylls med
    // (`rasterize`).
    float     ascent() const override { return font_ ? ascent_ : 0.0f; }
    FontStyle style() const override { return style_; }
    bool      rasterGlyph(char32_t cp, GlyphRaster& out) const override;
    bool      hasGlyph(char32_t cp) const override;

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas-UV
        float w = 0, h = 0;                     // bitmappens mått (px)
        float bearingX = 0, bearingY = 0;       // offset från baslinjen
                                                // (bearingY negativ ovanför
                                                //  — D3D11-konventionen)
        float advance  = 0;                     // penn-förflyttning (px)
    };
    // En kodpunkt upplöst EN gång (också ett negativt svar): glyfen och dess
    // stegbredd — mätningen och rastret läser samma.
    struct Resolved {
        GlyphRef ref;
        float    advance = 0.0f;
    };

    const Glyph* getGlyph(char32_t cp);   // rasteriserar och packar vid behov
    float advanceOf(char32_t cp) const;   // den upplösta glyfens stegbredd
    // Kodpunkt → glyfindex i en font (0 = saknas) — fontens egen eller en
    // reservs.
    CGGlyph glyphIndexIn(CTFontRef f, char32_t cp) const;
    // Fonten ett uppslag pekar på: fontens egen (0) eller reserv nr i − 1.
    CTFontRef faceOf(int i) const { return i == 0 ? font_ : reserves_[i - 1]; }
    // Kodpunktens glyf enligt reservens regel (../font_fallback.h), cachad.
    const Resolved& resolve(char32_t cp) const;
    // En reservfont som har kodpunkten (Core Texts kedja) — false utan.
    bool reserveFace(char32_t cp, GlyphRef& out) const;
    float advanceIn(CTFontRef f, CGGlyph gi) const;   // designmetriken
    // Glyfen som pixlar — DEN rasteriseringen, delad av atlasen (getGlyph)
    // och rasterGlyph: kCGImageAlphaOnly, gråskale-AA, ur fontens egen
    // font eller reservens.
    bool rasterize(CTFontRef f, CGGlyph gi, GlyphRaster& out) const;
    void applyStyle(FontStyle wanted);   // närmaste stil ur familjen
    FontStyle readStyle() const;          // CTFontGetSymbolicTraits
    void readMetrics();                   // ascent_/height_ ur font_
    bool ensureAtlas();
    void resetAtlas();                    // atlas full → börja om (sällsynt)

    CTFontRef font_   = nullptr;   // CF — CFRelease i unload
    float     sizePx_ = 0.0f;      // em-storlek i px
    float     ascent_ = 0.0f;
    float     height_ = 0.0f;
    FontStyle style_  = FontStyle::Regular;   // det fonten FAKTISKT är

    // Glyf-atlasen (R8 = ren täckningsgrad; färgen kommer ur vertexfärgen)
    id<MTLDevice>  device_   = nil;   // ägs av renderern
    id<MTLTexture> atlasTex_ = nil;
    int penX_ = 0, penY_ = 0, rowH_ = 0;
    std::unordered_map<char32_t, Glyph> glyphs_;
    // GLYFRESERVEN: reservfonterna i den ordning de hittades (GlyphRef::face
    // − 1; CF — CFRelease i unload) och svaret per kodpunkt.
    mutable std::vector<CTFontRef>                 reserves_;
    mutable std::unordered_map<char32_t, Resolved> resolved_;

    static constexpr int kAtlasW = 512;
    static constexpr int kAtlasH = 512;
};

// gfx::Renderer ovanpå Metal — spegeln av D3dRenderer: EN pipeline för
// allt (MSL-porten av HLSL:en, kompilerad i runtime via
// newLibraryWithSource — ingen .metallib-verktygskedja), batcher som
// flushar vid tillståndsbyte, retained-RT + composite (mode 5),
// scissor-klipp. VIKTIG skillnad mot D3D11: topologin är ett
// drawPrimitives-argument (ingen PSO-växling för line()) och vertexdata
// går genom en ring med tre delade buffertar + semafor (trippelbuffring —
// samma semantik som map-DISCARD).
class MetalRenderer : public Renderer {
public:
    // Layern ägs av GlazeView (den ÄR vyns backing layer) — renderern
    // lånar pekaren och skapar device/kö. transparent styr composite-vägen.
    bool init(CAMetalLayer* layer, int w, int h, bool transparent = false);
    void shutdownRenderer();

    // Alfa-hit-test för transparenta fönster: sant om pixeln (fysiska px)
    // är tillräckligt täckande för att fånga klick. Speglas från retained-
    // RT:n per present (ENDAST transparenta fönster — opaka betalar
    // inget). Degraderat läge (ingen spegel) ⇒ opak hit-test (alltid sant).
    bool alphaHit(int x, int y) const;
    bool transparent() const { return transparent_; }

    void resize(int w, int h);
    void beginFrame();   // ny commandbuffer + encoder mot retained-RT:n
    void composite();    // retained → drawable (fullskärms-quad, mode 5)
    void present();      // flush + presentDrawable + commit

    // Bär retained-RT:n förra framens innehåll? Falskt tills första
    // compositen efter att RT:n (åter)skapats, och i degraderat läge
    // (ingen RT) — då måste callbacket rita hela ytan varje frame.
    bool retainedValid() const { return retainedValid_; }
    // Syns fönstret? Ett DOLT fönster presenterar inte: `nextDrawable` är
    // vsync-styrd också för ett fönster utanför skärmen, och väntan hade
    // räknats som presentationens (profiler.h: "ett dolt fönster väntar
    // inte"). Retained-innehållet ritas ändå, så `captureFrame` och nästa
    // visning har det. Satt av MetalWindow (init och setVisible).
    void setPresenting(bool on) { presenting_ = on; }

    // Readback (glaze-render): retained-RT:n → rå RGBA8 på CPU (blit till
    // delad buffert + waitUntilCompleted, BGRA→RGBA-swizzle — samma
    // synk-mönster som alfa-spegeln). Anropas EFTER present (cmd_ nil);
    // köordningen garanterar att framens ritning exekverat först.
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

    // Bilder ur pixlar (metal_image.mm) — se renderer.h.
    // Uppdateringen går stämpelns väg (`uploadPathFor`), och
    // en bild med MIP-KEDJA får kedjan omräknad efteråt
    // (`generateMips`).
    using Renderer::createImage;
    std::unique_ptr<Image> createImage(int w, int h, ImageFormat f,
                                       const void* px, std::size_t stride,
                                       bool mipmaps) override;
    bool updateImage(Image& img, int x, int y, int w, int h, const void* px,
                     std::size_t stride) override;
    // Varje Mac-GPU i Metals funktionstabell (Apple-familjerna och Mac2)
    // har 16384 som största 2D-textur.
    int  maxTextureSize() const override { return 16384; }
    // Kedjan ur nivå 0 med blittens `generateMipmapsForTexture:` — i
    // uppladdningspasset före framen, med ett encoder-brott när bilden redan
    // ritats i framen, och i en egen commandbuffer utanför en frame. Bilden
    // STÄMPLAS (`noteUse`), så nästa skrivning inte går den direkta vägen
    // medan bliten kan läsa nivå 0.
    void generateMips(const MetalImage& im);

    // Render-målen (metal_target.mm; djup, MSAA och
    // läsningen) — se renderer.h.
    using Renderer::createRenderTarget;
    std::unique_ptr<RenderTarget> createRenderTarget(
        int w, int h, const TargetDesc& d) override;
    int  supportedSamples(int requested) const override;
    bool readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba) override;

    // Ritning utanför en frame (metal_renderer.mm) — en egen command
    // buffer, som endOffscreen committar och väntar in.
    bool beginOffscreen() override;
    void endOffscreen() override;

    // Intern ritväg för glyf-/bild-quads — batchar mot texturen.
    void texturedQuad(id<MTLTexture> tex, const Rectf& dst, const Rectf& uv,
                      Color c);

    // Scenen (metal_scene.mm) — se renderer.h för
    // kontraktet.
    std::unique_ptr<GpuBuffer> createBuffer(BufferKind kind, std::size_t bytes,
                                            const void* data) override;
    bool        updateBuffer(GpuBuffer& b, std::size_t offset,
                             const void* data, std::size_t bytes) override;
    BufferSlice uploadTransient(BufferKind kind, const void* data,
                                std::size_t bytes) override;
    void        drawMesh(const MeshDraw& d) override;

    // Egna program (metal_program.mm) — se
    // renderer.h och gfx/program.h.
    std::unique_ptr<ShaderProgram> createProgram(const ProgramDesc& d) override;

    id<MTLDevice> device() { return device_; }

protected:
    void applyClip(const Rectf* scissor, const RoundedClip* masks,
                   std::size_t count) override;

    // Render-målen (metal_target.mm): ett encoder-brott — encodern avslutas
    // och startas om på `t` (eller framens eget mål, `frameTarget_`) med
    // Load, så det som redan ritats i målet står kvar när man återvänder.
    void bindTarget(RenderTarget* t) override;

private:
    // SDF-primitiven bär sina parametrar per vertex så de batchas i samma
    // pipeline som allt annat — samma 56-bytes Vertex som D3D11.
    // mode: 0 = texturmask (geometri/text), 1 = fylld rundad rektangel,
    // 2 = kontur, 3 = drop shadow, 4 = inner shadow, 5 = RGBA rakt av
    // (compositen av retained-RT:n), 6 = RGBA PREMULTIPLICERAD (ett
    // render-måls bild), 7 = R8 som GRÅSKALA.
    struct Vertex {
        float         x, y;
        float         u, v;
        std::uint32_t rgba;    // uchar4Normalized — packad gfx::Color
        float         p0[4];   // SDF: centrum.xy, halvmått.zw (px)
        float         p1[4];   // SDF: radie, bredd/blur, offset.xy
        float         mode;
    };
    // Vertexformatet är ETT designargument (masken
    // ligger i en UNIFORM och inte per vertex just för att de 56 bytena
    // inte ska växa i det vanliga fallet). Formatet är dessutom identiskt i
    // alla tre backends — det får inte glida tyst.
    static_assert(sizeof(Vertex) == 56, "Vertex måste vara 56 byte");

    // Fragmentbufferten för klippmaskerna: samma layout
    // som MSL:ns `struct ClipMasks`. MSL packar float4-arrayer 16-byte-
    // alignat och float[4] gör detsamma här.
    struct ClipMaskBuf {
        float p0[4][4];   // centrum.xy + halvmått.zw per mask
        float p1[4][4];   // .x = radie
        float meta[4];    // .x = antal aktiva masker
    };
    static_assert(sizeof(ClipMaskBuf) == 144 && sizeof(ClipMaskBuf) % 16 == 0,
                  "ClipMaskBuf måste vara 16-byte-packad som MSL:ns struct");

    void setClipMasks(const RoundedClip* masks, std::size_t count);
    void bindClipMasks();   // ÄVEN från startEncoder — se metal_renderer.mm

    // MÅLARENS KLIPP: fragmentbuffert 4 för scenens program, samma
    // layout som MSL:ns `struct SceneClip` (kMslClipSrc) — kantplanen som
    // float4 och antalet i meta.x. Index 4 ligger utanför de egna programmens
    // block (2 och 3), så en Custom-ritning rör det inte. `bindSceneClip`
    // binder anropets klipp när encodern saknar en bindning
    // (`sceneClipBound_`, nollad i startEncoder — encoder-state försvinner
    // vid varje ny encoder) eller när klippet skiljer från `sceneClip_`.
    struct SceneClipBuf {
        float planes[kMaxClipPlanes][4];
        float meta[4];
    };
    static_assert(sizeof(SceneClipBuf) == 144 && sizeof(SceneClipBuf) % 16 == 0,
                  "SceneClipBuf måste vara 16-byte-packad som MSL:ns struct");
    void      bindSceneClip(const MeshDraw& d);
    SceneClip sceneClip_;               // det encodern har bundet
    bool      sceneClipBound_ = false;

    void flush();   // ritar batchade vertex med aktuell textur/topologi
    void setState(id<MTLTexture> tex, MTLPrimitiveType topo,
                  ImageFilter filter);
    void quad(const Rectf& dst, const Rectf& uv, Color c);

    // SAMPLERN ÄR BATCH-TILLSTÅND: flush sätter batchens sampler vid
    // varje ritning, scenens program sitt — text, geometri och compositen
    // tar Nearest uttryckligen.
    id<MTLSamplerState> samplerFor(ImageFilter f) const
    {
        return f == ImageFilter::Linear ? samplerLinear_ : samplerNearest_;
    }
    void sdfQuad(const Rectf& dst, Color c, float mode, const Rectf& prim,
                 float radius, float param, float offX, float offY);
    bool createPipeline();
    bool createRetained(int w, int h);
    // MSL I RUNTIME — `newLibraryWithSource` med tiden i
    // `RenderStats::compileNs`. Varje program går hit: UI:t,
    // scenens, bildens, ytornas, textens och de egna. PSO-varianterna tas i
    // `buildPso`.
    id<MTLLibrary> compileMsl(NSString* src, MTLCompileOptions* opts,
                              NSError** err);
    // Encoder-hantering: clear/composite bryter encodern (Metal saknar
    // clear mitt i en pass; scissor/pipeline återsätts per encoder-start).
    void startEncoder(id<MTLTexture> target, MTLLoadAction load,
                      MTLClearColor cc);
    void endEncoder();
    void applyScissor();   // aktuellt klipp → encodern (klampat mot målet)
    // Retained-alfan → CPU (blit + waitUntilCompleted) — hit-testens
    // spegel, uppdateras per present, ENDAST transparenta fönster.
    void updateAlphaMirror(id<MTLCommandBuffer> cmd);

    id<MTLDevice>       device_ = nil;
    id<MTLCommandQueue> queue_  = nil;
    CAMetalLayer*       layer_  = nil;   // ägs av GlazeView

    id<MTLSamplerState>        samplerNearest_ = nil;   // text, geometri, composite
    id<MTLSamplerState>        samplerLinear_  = nil;   // bilder (förvalet)
    id<MTLTexture>             whiteTex_       = nil;   // 1×1 R8=255 för geometri

    // Persistent retained-RT (drawable-storlek, privat lagring) — frame-
    // innehållet ritas hit och ÖVERLEVER mellan frames (loadAction=Load);
    // composite lyfter den till drawablen. Återskapas vid resize.
    id<MTLTexture> retainedTex_ = nil;
    bool retainedValid_ = false;
    bool presenting_    = true;   // setPresenting

    // Frame-state: EN commandbuffer per frame; encodern byts vid clear/
    // composite — encoder-bryten bevarar målarordningen exakt.
    id<MTLCommandBuffer>        cmd_       = nil;
    id<MTLRenderCommandEncoder> encoder_   = nil;
    id<MTLTexture>              curTarget_ = nil;
    id<CAMetalDrawable>         drawable_  = nil;   // hämtas SENT (composite)
    // FRAMENS EGET mål — `bindTarget(nullptr)` återvänder
    // hit: retained-RT:n (eller drawablen i degraderat läge) från
    // beginFrame, drawablen efter composite.
    id<MTLTexture>              frameTarget_ = nil;
    // Det BUNDNA målets storlek — viewport-uniformen (px → NDC) och scenens
    // uniformer räknar mot den. Lika med width_/height_ utom i ett
    // render-mål.
    int targetW_ = 0;
    int targetH_ = 0;

    // Ring med tre delade vertexbuffertar + semafor = trippelbuffring
    // (CPU:n skriver aldrig i en buffert GPU:n läser — DISCARD-semantiken).
    // Små flushar går via setVertexBytes; stora sub-allokerar ur ringen.
    static constexpr int kRingSlots = 3;
    id<MTLBuffer>        ringBufs_[kRingSlots] = {};
    dispatch_semaphore_t ringSem_    = nil;
    int                  ringIndex_  = 0;
    std::size_t          ringOffset_ = 0;

    // VILKET PROGRAM som är bundet i encodern. `drawMesh`
    // lämnar scenens PSO och uniformer kvar (index 1 i vertexsteget är UI:ts
    // viewport); `flush()` återbinder UI-pipelinen innan den ritar, och
    // `startEncoder` binder den alltid.
    // Line3 och Glyph3 är Line- och Glyph-källan med
    // GLAZE_POS3 — egna bibliotek, vertexdeskriptorer och PSO-varianter.
    // `Custom` står SIST och utanför `progDesc_`: ett
    // eget program bär sina PSO-varianter själv (metal_program.mm).
    enum class Pipeline { Ui, Line, Image, Fill, Glyph, Line3, Glyph3, Custom };
    Pipeline pipeline_ = Pipeline::Ui;
    void bindUiPipeline();
    // RenderStats::programSwitches räknas mot det program den SENASTE
    // RITNINGEN tog i bruk, inte mot `pipeline_`: encodern tappar sin
    // bindning vid varje brott (blit, mål, clear, compositen), så `pipeline_`
    // blir Ui utan att något ritats med UI:t — och UI:ts egen ritning efter
    // compositen hade inte räknats alls. D3D11 och GL behåller bindningen
    // över målbyten och räknar när en ritning byter program; `countProgram`
    // ger samma tal här (scene_pixel_gate). Framens början nollar tyst,
    // som deras `bindPipelineState`.
    Pipeline      drawn_       = Pipeline::Ui;
    std::uint64_t drawnSerial_ = 0;   // Custom: programmets löpnummer
    void countProgram(Pipeline p, std::uint64_t serial = 0)
    {
        if (drawn_ == p && drawnSerial_ == serial)
            return;
        drawn_       = p;
        drawnSerial_ = serial;
        ++stats_.programSwitches;
    }

    // PSO-VARIANTERNA. En Metal-PSO BAKAR
    // provtagningen, djupformatet och blandningen — så en PSO byggd för ett prov
    // utan djup med rak alfa räcker inte för alla mål. Varje program lämnar därför sin DESKRIPTOR (funktionerna och
    // vertexdeskriptorn) i `progDesc_`, och `psoFor` bygger och cachar
    // varianten för det BUNDNA målets provtagning och djup (`curSamples_`,
    // `curDepthTex_`) och anropets blandning. Alla PSO-bindningar går genom
    // `setPso` — en PSO som inte matchar passets attachments är ett
    // valideringsfel i Metal.
    bool hasProgram(Pipeline p) const
    {
        return progDesc_[static_cast<int>(p)] != nil;
    }
    id<MTLRenderPipelineState> psoFor(Pipeline p, Blend b);
    id<MTLRenderPipelineState> psoVariant(Pipeline p, Blend b, int samples,
                                          bool depth);
    // Lämnar programmets deskriptor och bygger grundvarianten (ett prov,
    // inget djup, rak alfa) — false + logg om den inte går att bygga.
    bool registerProgram(Pipeline p, MTLRenderPipelineDescriptor* pd,
                         const char* name);
    // VARIANTEN ur en deskriptor (funktionerna och vertexdeskriptorn):
    // provtagningen, djupformatet och blandningen pålagda på en kopia —
    // nil + logg (`what` i loggen) om den inte går att bygga. De inbyggda
    // programmen (`psoVariant`) och de egna delar den.
    id<MTLRenderPipelineState> buildPso(MTLRenderPipelineDescriptor* base,
                                        Blend b, int samples, bool depth,
                                        const char* what);
    void setPso(id<MTLRenderPipelineState> pso);
    // En per INBYGGT program, Ui … Glyph3 (index = Pipeline).
    MTLRenderPipelineDescriptor* progDesc_[7] = {};
    std::unordered_map<std::uint32_t, id<MTLRenderPipelineState>> psoCache_;
    id<MTLRenderPipelineState> curPso_ = nil;   // bundet i encodern

    // RENDERLÄGET: djupläget ur en cache per
    // (test, skrivning), gallringen och biasen — per anrop i scenens program,
    // tillbaka till UI:ts (av/ingen/0) i bindUiPipeline och i startEncoder
    // (encoderns tillstånd följer inte med ett encoder-brott).
    void applyMeshState(const RenderState& s);
    void applyUiState();
    id<MTLDepthStencilState> depthStateFor(DepthTest t, bool write);
    id<MTLDepthStencilState> depthStates_[7][2] = {};

    // MÅLEN MED DJUP OCH MSAA (metal_target.mm). `bound_` är målet som
    // är bundet (nullptr = framens eget); `curSamples_`/`curDepthTex_` är
    // encoderns attachments, som `startEncoder` och `psoFor` läser.
    // `resolveTarget` är UPPLÖSNINGSPASSET när ett MSAA-mål lämnas: färgen
    // Load + StoreAndMultisampleResolve om målet ligger kvar i stacken (man
    // kommer tillbaka), annars MultisampleResolve och djupet DontCare.
    MetalRenderTarget* bound_       = nullptr;
    int                curSamples_  = 1;
    id<MTLTexture>     curDepthTex_ = nil;
    void resolveTarget(MetalRenderTarget& t, bool final);
    bool samplesLogged_  = false;
    bool readLogged_     = false;
    bool offscreen_      = false;   // beginOffscreen:s egen command buffer

    // Scenens livscykel (metal_scene.mm).
    bool initScene();
    void shutdownScene();
    void beginSceneFrame();   // efter semaforen — slottens ring är fri

    // UPPLADDNINGARNA — TRE vägar per uppladdning plus den
    // utanför en frame, valda av EN regel ur resursens STÄMPEL (`usedIn`, den
    // frame som senast refererade den i ett kommando — 0 = aldrig):
    //   Direct  aldrig använd: CPU:n skriver i resursen (`memcpy` i en
    //           buffert, + `didModifyRange:` i Managed; `replaceRegion` i en
    //           bild). Inget kommando har rört den, så ingen läsning kan
    //           pågå.
    //   Pass    använd i en FÖREGÅENDE frame (eller ett föregående block): en blit
    //           ur stagingringen i UPPLADDNINGSPASSET, en egen commandbuffer
    //           köad FÖRE framens (`uploadEncoder`). Ordningen i kön gör att
    //           framen ser det nya innehållet; Metals spårning låter bliten
    //           vänta på förra framens läsningar (på GPU:n, aldrig CPU:n).
    //   Break   använd i DEN HÄR framen: encoder-brottet (`beginBlit`) med
    //           staging ur ringen — det som redan ritats i framen behåller
    //           det gamla innehållet (renderer.h). Räknas i
    //           `RenderStats::uploadBreaks`.
    //   Own     utanför en frame, redan använd: en egen commandbuffer med en
    //           egen staging (`beginBlit` utan frame).
    // STÄMPELNS TÄCKNING ÄR DET SOM BÄR Direct: bara `drawMesh` och
    // `drawImage` binder buffertarna och bilderna, och båda stämplar. En ny
    // väg som binder en resurs utan att stämpla den gör den direkta
    // skrivningen osäker.
    enum class UploadPath { Direct, Pass, Break, Own };
    UploadPath uploadPathFor(std::uint64_t usedIn) const
    {
        if (usedIn == 0)
            return UploadPath::Direct;
        if (!cmd_)
            return UploadPath::Own;
        if (usedIn == frameSerial_)
            return UploadPath::Break;
        return UploadPath::Pass;
    }
    // Stämplar anropets delar och bilder (FÖRST i `drawMesh`, före
    // förgreningen — också ett anrop som sedan avvisas: konservativt, aldrig
    // en osäker Direct).
    void noteUse(const MeshDraw& d) const;
    // Framens löpnummer är basens `frameSerial_` (renderer.h) —
    // stegat i beginFrame och beginOffscreen, så 0 betyder aldrig.
    // Statiska buffertar i Shared på en GPU med delat minne, annars Managed
    // (`device.hasUnifiedMemory`, satt i init). Managed-grenen kompileras
    // bara för en minsta macOS-version under 27 — se metal_scene.mm
    // `bufferStorage`.
    bool unifiedMemory_ = true;

    // EN BLIT, ORDNAD MOT RIT-ANROPEN (metal_renderer.mm) — Break och Own.
    // Mitt i en frame flushar `beginBlit` batchen, avslutar render-encodern
    // (`RenderStats::uploadBreaks`) och ger en blit-encoder i framens
    // commandbuffer; `endBlit` avslutar den och startar om render-encodern på
    // SAMMA mål med Load. Utanför en frame får bliten en egen commandbuffer
    // som `endBlit` committar (kön kör den före nästa frames). Det som redan
    // ritats behåller därmed det gamla innehållet.
    id<MTLBlitCommandEncoder> beginBlit();
    void                      endBlit(id<MTLBlitCommandEncoder> blit);
    id<MTLCommandBuffer>      blitCmd_    = nil;   // egen, utanför en frame
    id<MTLTexture>            blitTarget_ = nil;   // målet att återuppta

    // UPPLADDNINGSPASSET (metal_scene.mm). `uploadCmd_` skapas LATT vid
    // framens första Pass-uppladdning och köas direkt (`enqueue`) — framens
    // `cmd_` köas först vid sin commit, så passet ligger före framen. EN
    // blit-encoder åt gången; ett nytt pass startas bara när en blit skulle
    // ÖVERLAPPA en tidigare till samma mål i passet (ordningen inom ett pass
    // är inte dokumenterad, mellan pass synkroniserar Metal). Bokföringen är
    // målets skrivna rektanglar (`uploadWrites_`; en buffert är ett intervall
    // med höjden 1). VARJE väg som avslutar en frame skickar passet —
    // `commitFrame` (present, shutdownRenderer), endOffscreen och en
    // försvarsväg i beginFrame: en köad commandbuffer som aldrig skickas
    // låser kön.
    struct UploadRect {
        std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // [x0, x1) × [y0, y1)
        bool        overlaps(const UploadRect& o) const
        {
            return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
        }
    };
    id<MTLBlitCommandEncoder> uploadEncoder(const void* target,
                                            const UploadRect& r);
    // Avslutar och skickar passet (`done` på dess completion, nil = ingen)
    // och svarar med det — nil när inget pass fanns.
    id<MTLCommandBuffer> finishUploadPass(MTLCommandBufferHandler done);
    // FRAMENS INSKICK (metal_renderer.mm): passet och `cmd_`, med ring-slotten
    // fri först när BÅDA körts klart (en delad räknare — exekveringsordningen
    // i kön är dokumenterad, inte ordningen de blir klara i). Svarar med `cmd_`.
    id<MTLCommandBuffer> commitFrame();
    // GPU-TIDEN (renderer.h `setGpuTiming`): framen som
    // beginFrame började med tidtagningen på (0 = ingen). `commitFrame` ger
    // dess commandbuffer en completion handler som lämnar GPUEndTime −
    // GPUStartTime i brevlådan — i Metals tråd, under brevlådans lås, och
    // genom en egen referens till den, så att svaret kan komma efter att
    // renderaren rivits.
    std::uint64_t gpuSerial_ = 0;
    id<MTLCommandBuffer>      uploadCmd_  = nil;
    id<MTLBlitCommandEncoder> uploadBlit_ = nil;
    std::unordered_map<const void*, std::vector<UploadRect>> uploadWrites_;
    bool uploadLeftLogged_ = false;   // försvarsvägen i beginFrame

    // Image-programmets uniformer, samma layout som MSL:ns `struct ImageU`
    // (index 1 i båda stegen). Line, Fill och Glyph tar `PaintU` nedan.
    struct SceneU {
        float viewProj[16];
        float params[4];   // .xy målets storlek, .z opaciteten, .w flaggorna
        // FÄRGJUSTERINGEN (`sceneImageAdjust`): .x mättnaden, .y
        // kontrasten, .z ljusheten.
        float adjust[4];
    };
    static_assert(sizeof(SceneU) == 96, "SceneU måste matcha MSL:ns struct");

    // IMAGE-PROGRAMMET (metal_image.mm):
    // världsquads ur `ImageVertex`, perspektivkorrekt sampling, de projektiva
    // vikterna `q` (attribut 2 — en bild på fyra fria hörn),
    // opaciteten, bildens flaggor och färgjusteringen.
    // Uniformerna i `SceneU`-formen på index 1 i båda stegen.
    bool initImageProgram();
    void shutdownImageProgram();
    void drawImageMesh(const MeshDraw& d);

    // FÄRGENS uniformer, samma layout som MSL:ns
    // `struct PaintU` — index 1 i båda stegen för Fill-, Line- och
    // Glyph-programmen.
    struct PaintU {
        float viewProj[16];
        float params[4];   // .xy målets storlek; .zw per program
        float paint0[4];   // linjär: från.xy, till.zw; radiell: mitt, r0, r1
        float paint1[4];   // .x färgens slag, .y opaciteten, .zw per program
        // KONTUREN (Line): .x spetsgränsen, .y strecken (antal kumulativa
        // slut), .z mönsterenheter per världsenhet, .w förskjutningen — och
        // de kumulativa sluten.
        float stroke[4];
        float dash[8];
    };
    static_assert(sizeof(PaintU) == 160, "PaintU måste matcha MSL:ns struct");

    // FILL-PROGRAMMET (metal_fill.mm): ytorna ur
    // `FillVertex` och färgen — se d3d11_fill.cpp.
    bool initFillProgram();
    void shutdownFillProgram();
    void drawFillMesh(const MeshDraw& d);
    // Färgens fält och rampens bindning (index 0, linjär sampler) — delade
    // av Fill-, Line- och Glyph-programmen.
    void fillPaint(PaintU& pu, const MeshDraw& d);
    void bindPaintRamp(const MeshDraw& d);

    // GLYPH-PROGRAMMET (metal_glyph.mm): text och
    // markörer som billboards — se d3d11_glyph.cpp.
    bool initGlyphProgram();
    void shutdownGlyphProgram();
    void drawGlyphMesh(const MeshDraw& d);

    // EGNA PROGRAM (metal_program.mm): GlazeFrame
    // och GlazeUser går med setVertexBytes/setFragmentBytes på buffer(2) och
    // buffer(3), strömmarna på buffer(0) och buffer(1). `curProgram_` är det
    // BUNDNA programmets löpnummer — aldrig en adress.
    void          drawCustomMesh(const MeshDraw& d);
    std::uint64_t curProgram_ = 0;

    // Den transienta ringen per ring-SLOT och buffertslag — samma
    // trippelbuffring som UI-batcherns ring. En utväxt buffert kan bära
    // delar som redan lämnats ut; den lever tills slotten används igen.
    struct MetalTransientRing {
        std::unique_ptr<MetalGpuBuffer>              buf;
        std::size_t                                  offset = 0;
        std::vector<std::unique_ptr<MetalGpuBuffer>> retired;
    };
    MetalTransientRing transient_[kRingSlots][2];   // [slot][vertex, index]

    // STAGINGRINGEN — Pass- och Break-uppladdningarnas källa,
    // den transienta ringens mönster: en delad buffert per ring-SLOT, nollad
    // i `beginSceneFrame` efter semaforen (som släpps först när framen OCH
    // dess uppladdningspass körts), dubblad från 1 MiB när den inte räcker —
    // den utväxta lever i `retired` tills slotten används igen. Utanför en
    // frame, och för en enskild uppladdning över 8 MiB, en egen buffert som
    // commandbuffern håller vid liv (`stagingAlloc`). Högvattennivån står
    // kvar per slot (den krymps inte).
    struct StagingRing {
        id<MTLBuffer>              buf    = nil;
        std::size_t                offset = 0;
        std::vector<id<MTLBuffer>> retired;
    };
    StagingRing staging_[kRingSlots];
    // `bytes` byte staging, justerat på 256: bufferten, offseten och CPU:ns
    // pekare dit. `buf` nil (loggat) när ingen buffert kunde skapas.
    struct Staging {
        id<MTLBuffer> buf    = nil;
        std::size_t   offset = 0;
        std::uint8_t* ptr    = nullptr;
    };
    Staging stagingAlloc(std::size_t bytes);

    std::vector<Vertex> batch_;
    id<MTLTexture>      curTex_    = nil;   // nil = whiteTex_
    MTLPrimitiveType    curTopo_   = MTLPrimitiveTypeTriangle;
    ImageFilter         curFilter_ = ImageFilter::Nearest;

    bool  hasClip_ = false;   // aktuellt klipp (återappliceras per encoder)
    Rectf clip_{};
    // Rundade klippmasker — återbinds per encoder av EXAKT samma skäl.
    ClipMaskBuf clipMasks_{};

    // Transparent-vägen: CPU-spegel av retained-alfan (en byte per pixel)
    // för klick-genomsläppets hit-test. Blit-bufferten återanvänds.
    bool                      transparent_ = false;
    id<MTLBuffer>             alphaBuf_    = nil;
    std::vector<std::uint8_t> alphaMirror_;
    int                       alphaW_ = 0;
    int                       alphaH_ = 0;

    int  width_  = 0;
    int  height_ = 0;

    static constexpr std::size_t kMaxVerts = 65536;
};

// gfx::Window ovanpå AppKit: NSWindow (eller NSPanel för noActivate/
// toolWindow) + GlazeView (rå NSView vars backing layer är en CAMetalLayer
// — MetalKit används INTE, MTKView äger sin egen draw-loop och bryter hård
// regel 1). Event-översättningen bor i GlazeView/fönster-delegaten
// (metal_window.mm) — spegeln av WndProc-tabellen. Live-resize: macOS kör
// drag/resize i en modal runloop-fas där pumpen står still — delegatens
// windowDidResize ritar direkt (motsvarigheten till WM_SIZE-ritningen).
class MetalWindow : public Window {
public:
    // Ytans FAKTISKA alfa-status (fasadens fråga): begäran kan ha
    // degraderats vid init.
    bool transparent() const override { return transparent_; }

    // Chromets hörnradie (fysiska px). LAGRAS men används inte:
    // rundade fönster är påslagna på Linux — macOS rundar själv och
    // Windows har inget eget pass. Raden finns så att fasaden är
    // komplett.
    void setChromeRadius(float radius) override { chromeRadius_ = radius; }

    MetalWindow() = default;
    ~MetalWindow() override { shutdown(); }
    MetalWindow(const MetalWindow&)            = delete;
    MetalWindow& operator=(const MetalWindow&) = delete;

    bool init(const WindowDesc& desc) override;
    void shutdown() override;
    WindowId id() const override { return windowId_; }
    void raise() override;
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
    float uiScale() const override;       // backingScaleFactor × userScale
    void  userScaleChanged() override;    // se window.h-kontraktet
    // SKÄRMENS mm — mätt i `measureDisplay`.
    DisplayMetrics displayMetrics() const override { return metrics_; }
    bool  maximized() const override;
    // FÖNSTRETS GEOMETRI — se metal_window.mm.
    WindowPlacement placement() const override;
    void  minimize() override;
    void  maximize() override;
    void  restore() override;

    bool takeDirty() override
    {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }

    void setSize(int w, int h) override;
    void setSizeLimits(int minW, int minH, int maxW, int maxH) override;
    void setTopmost(bool on) override;
    bool topmost() const override;
    void setTitle(const std::string& t) override;
    void setVisible(bool on) override;
    void setPosition(int x, int y) override;
    Pointf clientToScreen(float px, float py) const override;
    Rectf screenArea() const override;
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
    // AppKit:s capture är implicit (mouseDragged går till mouse-down-vyn,
    // ingen SetCapture-motsvarighet att släppa) — dokumenterad no-op.
    void releaseMouseCapture() override {}
    // …och lika lite att TA: draget levereras till
    // fönstret där trycket började, också en nonactivating panel som inte är
    // key — trycket stannar där, och svaret är false (fasadens tabell). Ingen
    // tagen capture att fråga om heller.
    bool captureMouse() override { return false; }
    bool hasMouseCapture() const override { return false; }
    // MUSPEKAREN — `[NSCursor set]` direkt när pekaren
    // står i vyn eller en knapp hålls, och vyns `cursorUpdate:` sätter den
    // igen när AppKit annars hade återställt pilen (se metal_window.mm).
    void        setCursor(PointerShape shape) override;
    PointerShape cursor() const override { return cursor_; }
    // PROGRAMIKONEN — dockans ikon är APPENS, inte
    // fönstrets: `[NSApp setApplicationIconImage:]`, paketets ikon vinner
    // alltid och annars äger första fönstret dockan (se metal_window.mm).
    void setIcon(const WindowIcon& icon) override;
    bool iconApplied() const override { return iconApplied_; }
    // Upprepande NSTimer på main-runloopen (CommonModes — tickar även
    // under menyer/drag); callbacket pushar Timer + väcknings-eventet.
    // Samma id = invalidate + ny (omstartad period — SetTimer-semantiken).
    void startTimer(std::uint32_t id, std::uint32_t ms) override;
    void stopTimer(std::uint32_t id) override;
    void setTextInputActive(bool) override {}   // insertText levereras alltid
    bool retainedValid() const override
    {
        return gfxRenderer_.retainedValid();
    }
    bool captureFrame(std::vector<std::uint8_t>& rgba, int& w,
                      int& h) override;
    Renderer& renderer() override { return gfxRenderer_; }

    // ---- interna för ObjC-sidan (GlazeView/delegaten) — inte fasad ----
    void onDidResize();          // events + live-resize-ritning
    // NORMALRUTAN: ramens hörn spåras när fönstret
    // står i normalläge — vid flytt, storleksändring och precis FÖRE zoom
    // och miniatyr (delegatens will-/should-krokar), medan ramen ännu är
    // normalrutan.
    void trackNormalFrame();
    void onBackingChanged();     // ny skala → layer-geometri + events
    void onScreenChanged();      // windowDidChangeScreen → skärmens mm
    void onOcclusionVisible();   // fönstret blev synligt → WindowExposed
    // Vanliga ägda fönster (se `ownerWin_` nedan):
    void liftOwned();                   // ägaren blev key → ägda över den
    void hideOwnedForMinimize();        // ägaren minimeras → ägda döljs
    void restoreOwnedAfterMinimize();   // ägaren tillbaka → ägda fram igen
    // Custom-chromets dragyta: sant om (x, y) (fysiska px) är caption —
    // titelraden minus knapparna (resize-kanterna äger OS:et redan).
    bool captionHitTest(float x, float y) const;
    // Transparent-vägens klick-genomsläpp: slå upp alfa-spegeln och toggla
    // ignoresMouseEvents (macOS saknar WM_NCHITTEST — anropas från både
    // lokala mouseMoved och den globala monitorn).
    void updateClickThrough(float x, float y);
    // Blir fönstret aldrig key (`WindowDesc::noActivate` — popuper, menyer)?
    // GlazeView väljer spårytans flaggor ur svaret: ett sådant
    // fönster får `ActiveAlways`, annars kommer inga `mouseMoved:`. Satt i
    // `init` före vyn skapas.
    bool noActivate() const { return noActivate_; }

private:
    Sizef clientSize() const;
    void  syncLayerGeometry();   // contentsScale + drawableSize ← vyn × skala

    NSWindow*            nsWindow_ = nil;
    GlazeView*           view_     = nil;
    GlazeWindowDelegate* delegate_ = nil;
    MetalRenderer gfxRenderer_;
    Chrome        chrome_      = Chrome::Native;
    bool          transparent_ = false;   // flaggan lagras
    float         chromeRadius_ = 0.0f;   // fönstrets hörn (fysiska px)
    ChromeColors  chromeColors_;
    std::string   title_;
    RenderFn      renderCb_;
    RenderFn      overlayCb_;
    WindowId      windowId_ = 0;   // löpnummer ur init (0 = inget fönster)
    bool          dirty_      = false;

    // Hit-test-kartan: glaze trycker ner regionerna
    // + captionens höjd ur widget-trädet; mouseDown-draget slår upp här.
    std::vector<HitRegion> hitRegions_;
    float                  captionH_ = 32.0f;   // logiska px (default =
                                                // TopBar-defaulten)
    bool          noActivate_ = false;
    // FÖRSTA VISNINGEN AKTIVERAR INTE (`WindowDesc::showInactive`):
    // den första `orderFront:` i `setVisible`.
    bool          showInactive_ = false;
    bool          wasZoomed_  = false;   // isZoomed-flank → Maximized/Restored

    // FÖNSTRETS GEOMETRI. Normallägets RAMHÖRN i
    // placeringsrymden (samma rymd som `setPosition`/`setFrameTopLeftPoint`),
    // spårat i `trackNormalFrame` och satt i init innan fönstret visats — så
    // en zoomad start bär den begärda rutan. Storleken är
    // `logicalW_`/`logicalH_` nedan, som redan är skyddade mot zoom,
    // skalresize och återställningsflanken.
    Pointf        normalTopLeft_{0.0f, 0.0f};
    // STARTLÄGET: `desc.startState`, tillämpat vid FÖRSTA synligheten
    // (`setVisible(true)` — i init eller efter en dold start).
    WindowState   startState_ = WindowState::Normal;
    bool          firstShown_ = false;

    // ÄGARSKAPET. Bara
    // popup-kontraktets ANKRADE fönster (listor, menyer, tooltips, färg- och
    // datumpopuper) blir AppKit-barnfönster, för de ska följa sin ägare. Ett
    // `noActivate`-fönster UTAN ankare (det flytande verktygsfältet) är ett
    // vanligt ägt fönster — det hade annars flyttat med huvudfönstret och
    // låsts till dess Space. Ett VANLIGT ägt fönster (dialog,
    // andrafönster) blir det ALDRIG: ett barnfönster flyttar med föräldern
    // och är låst till dess Space, så det försvann vid skärm- och
    // Space-byte, och fasaden lovar att ägarskap bär Z-ordning och
    // minimering men ALDRIG koordinater. De två emuleras i stället: ägaren
    // lyfter sina ägda när den blir key och döljer/återställer dem kring
    // sin minimering. Råa pekare åt BÅDA hållen — `shutdown` nollar dem.
    MetalWindow*              ownerWin_ = nullptr;   // satt för vanligt ägt
    std::vector<MetalWindow*> owned_;                // mina vanliga ägda
    std::vector<MetalWindow*> hiddenByMinimize_;     // dolda av MIN minimering

    // Lagrad LOGISK klientstorlek (invarianten). Metal-specialen:
    // drawable-px är ALLTID punkter × backingScaleFactor, så det är
    // PUNKTstorleken som bär userScale (punkter = logiska px × userScale).
    // Sätts i init/setSize; räknas tillbaka (punkter ÷ userScale) vid
    // ANVÄNDAR-resize i onDidResize — aldrig ur vår egen skalresize
    // (pendingScaleResize_-vakten) och aldrig zoomad (restore-storleken).
    int  logicalW_ = 0, logicalH_ = 0;
    // STORLEKSGRÄNSERNA i LOGISKA px, 0 = obegränsat.
    // Lagras för att kunna ÅTERAPPLICERAS: contentMin/MaxSize talar
    // PUNKTER (= logiska px × userScale), så ett userScale-byte gör de
    // gamla talen fel.
    int  limMinW_ = 0, limMinH_ = 0, limMaxW_ = 0, limMaxH_ = 0;
    void applySizeLimits();
    // ALLTID ÖVERST — begäran, lagrad för tiden FÖRE
    // NSWindow finns. Efter det är `nsWindow_.level` sanningen.
    bool topmost_ = false;
    // MUSPEKAREN — den begärda formen; lagras även före init.
    PointerShape cursor_ = PointerShape::Arrow;
    // PROGRAMIKONEN — ramarna, lagrade även före init (init
    // tillämpar dem), och om DET HÄR fönstrets senaste setIcon gick fram.
    WindowIcon icon_;
    bool       iconApplied_ = false;
    void       applyIcon();
    bool pendingScaleResize_ = false;

    // Timers: id → NSTimer (alla invalideras i shutdown — inga ticks efter
    // fönstrets död)
    std::unordered_map<std::uint32_t, NSTimer*> timers_;

    // Transparent: global mouseMoved-monitor — enda vägen tillbaka från
    // ignoresMouseEvents=YES (fönstret får då inga lokala events).
    // ::id — metoden id() skuggar ObjC-typen i klass-scope.
    ::id globalMouseMonitor_ = nil;

    // SKÄRMENS FYSISKA UPPLÖSNING:
    // CGDisplayScreenSize mot bufferten (punkter × backing) för skärmen
    // fönstret står på; Apples 72-dpi-uppskattning räknas som Assumed.
    // Mäts vid init, vid skärmbyte och vid ny backing; `announce` köar
    // WindowDisplayChanged när svaret blev ett annat.
    void           measureDisplay(bool announce);
    DisplayMetrics metrics_;
};

} // namespace gfx
