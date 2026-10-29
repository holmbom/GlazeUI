#pragma once
// Intern header för D3D11-backenden — ligger i src/, INTE include/, och får
// aldrig inkluderas utanför backend_d3d11/. Win32-/D3D-/DWrite-typer får bara
// förekomma här och i backend_d3d11/*.cpp; utåt syns enbart fasaden i
// include/gfx/.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <dwrite_2.h>   // IDWriteFactory2/IDWriteFontFallback — glyfreserven
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../font_fallback.h"
#include "d3d11_touchpad.h"
#include "gfx/platform.h"   // GpuAdapter — `DxgiAdapter`
#include "gfx/renderer.h"
#include "gfx/scene.h"
#include "gfx/text.h"
#include "gfx/window.h"

namespace gfx {

using Microsoft::WRL::ComPtr;

// Intern event-kö: WndProc:en översätter Win32-meddelanden till gfx::Event
// och köar dem här; waitEvent/pollEvent (d3d11_platform.cpp) dränerar kön.
void pushEvent(const Event& e);

// HJULET OCH STYRPLATTANS GEST (d3d11_window.cpp) — EN väg för båda källorna,
// WndProc:ens WM_MOUSEWHEEL och Direct Manipulation (d3d11_touchpad.cpp), så
// att modifierarna stämplas på ett ställe: `GetKeyState` i meddelandets takt,
// som för musen. Ctrl/Cmd + styrplattans drag kretsar på ritytan, och
// kontraktet står vid `kModCtrl` i gfx/events.h.
void pushWheel(WindowId wid, float x, float y, float dx, float dy,
               bool precise = false, GesturePhase phase = GesturePhase::None);
void pushGesture(WindowId wid, float x, float y, GesturePhase phase,
                 float scale, float rotation);

// UTF-8 ↔ UTF-16 (Win32-API:erna är W-varianter).
std::wstring toWide(const std::string& utf8);
std::string  toUtf8(const std::wstring& wide);

// Sant mellan `gfx::init()` och `gfx::shutdown()` (d3d11_platform.cpp).
// Systemfältet (d3d11_tray.cpp) gör ingenting utanför det, som gfx/tray.h
// lovar — en `glaze::TrayIcon` som lever längre än appen får inte skapa ett
// nytt mottagarfönster efter shutdown.
bool platformInitialized();

// IKONEN SOM HICON (d3d11_window.cpp) — delad av fönstrets programikon och
// systemfältets ikon (d3d11_tray.cpp), så att reglerna är EN sanning.
// `iconFromImage` gör en ram till en HICON: en 32-bitars DIB med RAK alfa
// (som en 32-bitars bild i en .ico-fil) plus en AND-mask ur alfan.
// `programIcon` laddar programfilens ikongrupp nr 1 — den `glaze_app_icon`
// kompilerar in, och den `programFirst` (gfx/window.h) låter vinna — i sidan
// `side` px, nullptr när exe:n saknar den. Båda ger EGNA ikoner som anroparen
// river med DestroyIcon, och nullptr när GDI vägrar.
//
// `cursor` gör ramen till en PEKARE med hotspoten
// (`hotX`, `hotY`) i ramens pixlar — samma DIB och mask, `ICONINFO.fIcon =
// FALSE`. Den stängda handen, som Windows saknar, går den vägen; anroparen
// river den med DestroyCursor.
HICON iconFromImage(const IconImage& im, bool cursor = false, int hotX = 0,
                    int hotY = 0);
HICON programIcon(int side);

// ETT DOLT VERKTYGSFÖNSTER (d3d11_window.cpp) — toppnivå, `WS_POPUP` +
// `WS_EX_TOOLWINDOW` (aldrig i aktivitetsfältet eller Alt+Tab, om något
// skulle visa det), aldrig visat, av klassen `cls` med `proc`. Delat av
// aktivitetsfältets ägarfönster och systemfältets mottagare. nullptr + en
// loggrad med `vem` när klassen eller fönstret vägras.
HWND createHiddenToolWindow(const wchar_t* cls, WNDPROC proc, const char* vem);

// ARBETSYTAN (`rcWork`, utan aktivitetsfältet) på skärmen `mon` i fysiska px
// — placeringsrymden. Tom rect när den inte går att läsa. Delad av
// `D3dWindow::screenArea` och systemfältets `trayIconRect`.
Rectf workArea(HMONITOR mon);

// Systemfältet (d3d11_tray.cpp) — `gfx::shutdown()` tar bort
// varje ikon ur fältet (NIM_DELETE: ingen spökikon kvar efter avslutet) och
// river mottagarfönstret. Idempotent.
void trayShutdown();

// APPENS TIMER (d3d11_timer.cpp) — `gfx::shutdown()` river
// meddelandefönstret, och timrarna dör med det. Idempotent.
void appTimerShutdown();

// GRAFIKKORTEN (d3d11_gpu.cpp). `enumerateAdapters` går
// `EnumAdapters1` i DXGI:s ordning (index 0 = förvalet) med listans post,
// adaptern och LUID:en; slaget ur kärnans hybridflaggor, och med `probeKind`
// annars ur en prob-enhet (bara listan `gfx::gpuAdapters` probar).
// `chosenAdapter` är det en ny renderare tar: kortet `gpuAdapter()` om det
// finns, annars GPU-preferensens (`preferredAdapter`, loggat) — nullptr =
// DXGI:s första. `adapterIdOf` är id:t för enhetens LUID (tomt utan
// träff), och `logAdapter` listar korten en gång per process och därefter varje
// NY adapter som tas i bruk.
struct DxgiAdapter {
    GpuAdapter            info;
    ComPtr<IDXGIAdapter1> adapter;
    LUID                  luid{};
};
std::vector<DxgiAdapter> enumerateAdapters(IDXGIFactory1& f, bool probeKind);
ComPtr<IDXGIAdapter1>    preferredAdapter(IDXGIFactory1& f, GpuPreference p);
ComPtr<IDXGIAdapter1>    chosenAdapter(IDXGIFactory1& f,
                                       const std::vector<DxgiAdapter>& kort);
std::string adapterIdOf(const std::vector<DxgiAdapter>& kort, const LUID& luid);
void        logAdapter(const std::vector<DxgiAdapter>& kort,
                       const DXGI_ADAPTER_DESC& vald, const std::string& id);

// Klippmaskernas HLSL (cbuffer b1 + sdRoundRect + clipCoverage) — klistras
// före VARJE pixelshader som ritar i ett mål med klippstacken, annars gäller
// de rundade maskerna inte för programmet. Och målarens klipp:
// cbuffer b5 + `sceneClip(p)`, som scenens program multiplicerar
// alfan med och som KASTAR fragmentet utanför (`SceneClip`, gfx/scene.h).
extern const char kHlslClipSrc[];

// Färgens HLSL (d3d11_fill.cpp): scenens cbuffer b2 med
// alla fält, rampen på t0, den linjära samplern på s0 och `paintColor(p)` —
// klistras efter klippmaskernas del i Fill-, Line- och Glyph-programmen.
extern const char kHlslPaintSrc[];

// FÄRGJUSTERINGENS HLSL (d3d11_program.cpp) — `glaze_colorAdjust(c,
// a)`, gfx/program.h. EN definition: i de egna programmens förspel (där
// ritytans Mesh-program når den) och i Image-programmet.
extern const char kHlslAdjustSrc[];

// DE SKÄRMVÄNDA HÖRNENS DJUP (d3d11_program.cpp) — `glaze_standingZ(m,
// c, off)`, gfx/program.h. EN definition: i de egna programmens förspel (där
// ritytans Points-program når den) och efter färgens del i Line- och
// Glyph-programmen.
extern const char kHlslStandSrc[];

class D3dRenderer;

// gfx::GpuBuffer ovanpå en ID3D11Buffer (d3d11_scene.cpp). Statiska
// buffertar har DEFAULT-användning och uppdateras med UpdateSubresource; den
// transienta ringens är DYNAMIC och mappas. Hör till rendererns device —
// släpp bufferten före fönstret.
class D3dGpuBuffer : public GpuBuffer {
public:
    D3dGpuBuffer(BufferKind kind, std::size_t size, ComPtr<ID3D11Buffer> buf)
        : kind_(kind), size_(buf ? size : 0), buf_(std::move(buf))
    {
    }

    BufferKind  kind() const override { return kind_; }
    std::size_t size() const override { return size_; }
    bool        valid() const override { return buf_ != nullptr; }

    ID3D11Buffer* buffer() const { return buf_.Get(); }

private:
    BufferKind           kind_;
    std::size_t          size_;
    ComPtr<ID3D11Buffer> buf_;
};

// gfx::Image ovanpå en D3D11-textur + SRV (d3d11_image.cpp). Bilder ur
// pixlar (`createImage` — loadImage avkodar med WIC och går samma väg) har
// DEFAULT-användning och uppdateras med UpdateSubresource. Texturen hör
// till rendererns device — släpp bilden före fönstret. Default-konstruerad
// = ogiltig (fabrikens svar vid fel). MED MIP-KEDJA är texturen
// också RTV-bindbar med GENERATE_MIPS, och `GenerateMips` på SRV:n räknar om
// nivåerna ur nivå 0.
class D3dImage : public Image {
public:
    D3dImage() = default;
    D3dImage(ComPtr<ID3D11Texture2D> tex, ComPtr<ID3D11ShaderResourceView> srv,
             int w, int h, ImageFormat f, bool premultiplied,
             bool mipmapped = false)
        : tex_(std::move(tex)), srv_(std::move(srv)), w_(w), h_(h),
          format_(f), premultiplied_(premultiplied), mipmapped_(mipmapped)
    {
    }

    bool mipmapped() const { return mipmapped_; }

    int  width() const override { return srv_ ? w_ : 0; }
    int  height() const override { return srv_ ? h_ : 0; }
    bool valid() const override { return srv_ != nullptr; }
    ImageFormat format() const override { return format_; }
    bool premultiplied() const override { return premultiplied_; }
    bool flippedV() const override { return false; }   // rad 0 överst alltid

    ID3D11ShaderResourceView* srv() const { return srv_.Get(); }
    ID3D11Texture2D*          texture() const { return tex_.Get(); }

private:
    ComPtr<ID3D11Texture2D>          tex_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    int         w_ = 0, h_ = 0;
    ImageFormat format_        = ImageFormat::Rgba8;
    bool        premultiplied_ = false;
    bool        mipmapped_     = false;
};

// gfx::RenderTarget ovanpå en D3D11-textur med RTV (d3d11_target.cpp).
// Bilden är texturen sedd som en PREMULTIPLICERAD bild
// — en D3dImage, så drawImage och Image-programmet tar den som vilken bild
// som helst. Hör till rendererns device — släpp målet före fönstret.
//
// DJUP OCH MSAA: med provtagning > 1 ritas det i en EGEN
// provtagen textur (`msaa`, RTV:n sitter på den) som löses upp till bildens
// textur med `ResolveSubresource` när målet lämnas; annars sitter RTV:n på
// bilden själv. Djupet är en D32F-textur med samma provtagning (`dsv`).
class D3dRenderTarget : public RenderTarget {
public:
    D3dRenderTarget() = default;   // ogiltigt (fabrikens svar vid fel)
    D3dRenderTarget(ComPtr<ID3D11Texture2D> tex,
                    ComPtr<ID3D11ShaderResourceView> srv,
                    ComPtr<ID3D11Texture2D> msaa,
                    ComPtr<ID3D11RenderTargetView> rtv,
                    ComPtr<ID3D11DepthStencilView> dsv, int w, int h,
                    int samples, bool mipmapped)
        : image_(std::move(tex), std::move(srv), w, h, ImageFormat::Rgba8,
                 true, mipmapped),
          msaa_(std::move(msaa)), rtv_(std::move(rtv)), dsv_(std::move(dsv)),
          samples_(samples)
    {
    }

    Image& image() override { return image_; }
    int    samples() const override { return image_.valid() ? samples_ : 1; }
    bool   hasDepth() const override { return dsv_ != nullptr; }

    ID3D11RenderTargetView* rtv() const { return rtv_.Get(); }
    ID3D11DepthStencilView* dsv() const { return dsv_.Get(); }
    // Den provtagna texturen — nullptr utan MSAA (då ritas det i bilden).
    ID3D11Texture2D*        msaaTexture() const { return msaa_.Get(); }
    ID3D11Texture2D*        imageTexture() const { return image_.texture(); }
    // Bildens kedja — räknas om när målet lämnas (bindTarget).
    const D3dImage&         imageView() const { return image_; }

private:
    D3dImage                       image_;
    ComPtr<ID3D11Texture2D>        msaa_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    int                            samples_ = 1;
};

// gfx::Font ovanpå DirectWrite. Glyfer rasteriseras vid behov via
// IDWriteGlyphRunAnalysis (ren DWrite, ingen D2D) till en R8-atlas på GPU:n;
// drawText ritar texturerade quads ur atlasen. Atlasen hör till renderern —
// fonten måste släppas före device:n (delad städordning i alla backends).
// GLYFRESERVEN: en glyf fonten saknar tas ur
// systemets reservkedja (`IDWriteFontFallback::MapCharacters`) — regeln i
// ../font_fallback.h, upplöst EN gång per kodpunkt för mätningen, atlasen och
// rasterGlyph tillsammans.
class D3dFont : public Font {
public:
    D3dFont() = default;
    ~D3dFont() override { unload(); }
    D3dFont(const D3dFont&)            = delete;
    D3dFont& operator=(const D3dFont&) = delete;

    bool load(const std::string& path, float sizePt);
    // Familjen i stilen via font_catalog — närmaste stil inom första familj
    // som går att ladda.
    bool loadFamily(FontFamily f, float sizePt, FontStyle style);
    void unload();

    // Kopplar fonten till renderns device — atlas-texturen skapas lazy vid
    // första draw. Sätts av D3dRenderer::loadFont/loadDefaultFont.
    void setDevice(ID3D11Device* dev, ID3D11DeviceContext* ctx)
    {
        device_  = dev;
        context_ = ctx;
    }

    // Intern ritväg — anropas av D3dRenderer::drawText och chromet.
    // Ritar UTF-8-text med övre vänstra hörnet i (x, y); returnerar bredden.
    float draw(D3dRenderer& r, float x, float y,
               const std::string& utf8, Color color);

    Sizef measure(const std::string& utf8) const override;
    float height() const override;
    bool  valid() const override { return own_.face != nullptr; }

    // Text-API:t — allt via design-advances (samma väg
    // som measure; rasteriserar aldrig, funkar utan device).
    float prefixWidth(const std::string& utf8,
                      std::size_t bytes) const override;
    std::size_t byteIndexAtX(const std::string& utf8,
                             float x) const override;
    std::size_t fitWidth(const std::string& utf8,
                         float maxW) const override;

    // Ritytans text: stilen läses ur fontens OS/2-tabell
    // och rasteriseringen är DEN som atlasen fylls med (`rasterize`).
    float     ascent() const override { return own_.face ? ascent_ : 0.0f; }
    FontStyle style() const override { return style_; }
    bool      rasterGlyph(char32_t cp, GlyphRaster& out) const override;
    bool      hasGlyph(char32_t cp) const override;

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas-UV
        float w = 0, h = 0;                     // bitmappens mått (px)
        float bearingX = 0, bearingY = 0;       // bitmapp-offset från baslinjen
        float advance  = 0;                     // penn-förflyttning (px)
    };
    // EN FONT-FACE i fontens storlek: fontens egen (`own_`) eller en reserv.
    struct Face {
        ComPtr<IDWriteFontFace> face;
        float       emSize = 0.0f;   // em-storleken i px (× reservens skala)
        float       scale  = 0.0f;   // designenheter → px
        std::string key;             // reservens STABILA identitet (filernas
                                     // referensnycklar, index, simuleringar,
                                     // skala); tom för fontens egen
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
    const Face& faceOf(int i) const { return i == 0 ? own_ : reserves_[i - 1]; }
    // Kodpunktens glyf enligt reservens regel (../font_fallback.h), cachad.
    const Resolved& resolve(char32_t cp) const;
    // En reservface som har kodpunkten (systemets kedja) — false utan.
    bool reserveFace(char32_t cp, GlyphRef& out) const;
    IDWriteFontFallback* fallback() const;   // lat; nullptr före Windows 8.1
    // Glyfen som pixlar — DEN rasteriseringen, delad av atlasen (getGlyph)
    // och rasterGlyph: ClearType-3x1 medlad till gråskala.
    bool  rasterize(const Face& f, UINT16 gi, GlyphRaster& out) const;
    float advanceIn(const Face& f, UINT16 gi) const;   // designmetriken
    void  readStyle();                    // stilen och vikten ur OS/2-tabellen
    bool ensureAtlas();
    void resetAtlas();                    // atlas full → börja om (sällsynt)

    ComPtr<IDWriteFactory>  factory_;
    Face  own_;              // fontens egen face (ingen nyckel)
    float ascent_  = 0.0f;
    float height_  = 0.0f;
    FontStyle          style_  = FontStyle::Regular;   // det filen FAKTISKT är
    DWRITE_FONT_WEIGHT weight_ = DWRITE_FONT_WEIGHT_NORMAL;   // OS/2-vikten

    // GLYFRESERVEN: facerna i den ordning de hittades (GlyphRef::face − 1),
    // kedjan och svaret per kodpunkt.
    mutable std::vector<Face>                      reserves_;
    mutable ComPtr<IDWriteFontFallback>            fallback_;
    mutable bool                                   fallbackTried_  = false;
    mutable bool                                   reservesLogged_ = false;
    mutable std::unordered_map<char32_t, Resolved> resolved_;

    // Glyf-atlasen (R8 = ren täckningsgrad; färgen kommer ur vertexfärgen)
    ID3D11Device*        device_  = nullptr;   // ägs av renderern
    ID3D11DeviceContext* context_ = nullptr;
    ComPtr<ID3D11Texture2D>          atlasTex_;
    ComPtr<ID3D11ShaderResourceView> atlasSrv_;
    int penX_ = 0, penY_ = 0, rowH_ = 0;
    std::unordered_map<char32_t, Glyph> glyphs_;

    static constexpr int kAtlasW = 512;
    static constexpr int kAtlasH = 512;
};

// gfx::Renderer ovanpå D3D11 — allt är quads/linjer genom EN pipeline
// (3D-first: 2D = ortho-projicerade trianglar). Batchern samlar vertex i
// ritordning och flushar vid byte av textur/topologi, vid clear och vid
// present — målar-ordningen (bakifrån och fram) bevaras exakt.
class D3dRenderer : public Renderer {
public:
    // Skapar device, swapchain (mot fönstret), shaders och batch-resurser.
    // transparent väljer en EGEN swapchain-väg (CreateSwapChainForComposition
    // + premultiplicerad alfa + DirectComposition); opaka fönster behåller
    // exakt ForHwnd+FLIP_DISCARD — vägen får inte läcka in dit.
    // Misslyckas composition-vägen degraderas fönstret till opakt (loggat).
    bool init(HWND hwnd, int w, int h, bool transparent = false);
    void shutdownRenderer();
    // Finns enheten? Fönstrets egen storleksändring i init (WM_SIZE före
    // `init` här) ska inte rita — ritanropen hade lagt sig i batchen utan en
    // kontext att tömma den i (som GL-sidans `GlRenderer::ready`).
    bool ready() const { return context_.Get() != nullptr; }

    // Alfa-hit-test för transparenta fönster: sant om pixeln (klient-px)
    // är tillräckligt täckande för att fånga klick. Speglas från vår egen
    // retained-RT per present (endast transparenta fönster — opaka betalar
    // inget). Degraderat läge (ingen spegel) ⇒ opak hit-test (alltid sant).
    bool alphaHit(int x, int y) const;
    bool transparent() const { return transparent_; }

    void resize(int w, int h);   // WM_SIZE: ResizeBuffers + ny RTV + ny offscreen-RT
    void beginFrame();           // binder RETAINED-RT:n + viewport, ortho-CB
    void composite();            // retained RT → backbuffer (fullskärms-quad);
                                 // efterföljande ritning (chrome/overlay) går
                                 // direkt i backbuffern
    void present();              // flush + Present(1, 0) — vsync

    // Osynkad Present (SyncInterval 0) mellan WM_ENTERSIZEMOVE och
    // WM_EXITSIZEMOVE: under den modala resize-loopen hinner en vsync-
    // blockerad Present efter — DWM visar då förra framens backbuffer
    // uppskalad ("sträcks konstigt"). Vsync åter vid exit.
    void setUnsyncedPresent(bool on) { unsyncedPresent_ = on; }

    // Bär retained-RT:n förra framens innehåll? Falskt tills första
    // compositen efter att RT:n (åter)skapats, och alltid i degraderat
    // läge (ingen RT) — då måste callbacket rita hela ytan varje frame.
    bool retainedValid() const { return retainedValid_; }

    // Readback (glaze-render): retained-RT:n → rå RGBA8 på CPU (egen
    // staging + CopyResource + Map(READ) — alphaStaging_-mönstret, men
    // alla fyra byten och BGRA→RGBA-swizzle). Anropas EFTER renderFrame.
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

    // Bilder (d3d11_image.cpp): WIC → RGBA-pixlar → `createImage`; ritas i
    // shader-mode 5 (RGBA rakt av — compositens väg) i samma batch-pipeline,
    // med opaciteten i vertexfärgens alfa. Helbildsformen är basens.
    std::unique_ptr<Image> loadImage(const std::string& path) override;
    std::unique_ptr<Image> loadImage(const void* data,
                                     std::size_t size) override;
    using Renderer::drawImage;
    void drawImage(Image& img, const Rectf& src, const Rectf& dst,
                   float opacity) override;

    // Bilder ur pixlar (d3d11_image.cpp) — DEFAULT-
    // texturer; `updateImage` flushar batchen och tar UpdateSubresource.
    // Med mip-kedja är texturen RTV-bindbar med GENERATE_MIPS och
    // kedjan räknas om med `GenerateMips` efter varje skrivning.
    using Renderer::createImage;
    std::unique_ptr<Image> createImage(int w, int h, ImageFormat f,
                                       const void* px, std::size_t stride,
                                       bool mipmaps) override;
    bool updateImage(Image& img, int x, int y, int w, int h, const void* px,
                     std::size_t stride) override;
    int  maxTextureSize() const override;

    // Render-målen (d3d11_target.cpp; djup, MSAA och
    // läsningen) — se renderer.h.
    using Renderer::createRenderTarget;
    std::unique_ptr<RenderTarget> createRenderTarget(
        int w, int h, const TargetDesc& d) override;
    int  supportedSamples(int requested) const override;
    bool readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba) override;

    // Ritning utanför en frame (d3d11_renderer.cpp) — binder UI-pipelinens
    // tillstånd, som beginFrame annars gör.
    bool beginOffscreen() override;
    void endOffscreen() override;

    // Scenen (d3d11_scene.cpp): buffertarna, den
    // transienta ringen och Line-programmet — se renderer.h för kontraktet.
    std::unique_ptr<GpuBuffer> createBuffer(BufferKind kind, std::size_t bytes,
                                            const void* data) override;
    bool        updateBuffer(GpuBuffer& b, std::size_t offset,
                             const void* data, std::size_t bytes) override;
    BufferSlice uploadTransient(BufferKind kind, const void* data,
                                std::size_t bytes) override;
    void        drawMesh(const MeshDraw& d) override;

    // Egna program (d3d11_program.cpp) — se renderer.h
    // och gfx/program.h.
    std::unique_ptr<ShaderProgram> createProgram(const ProgramDesc& d) override;

    // Intern ritväg för glyf-quads (anropas av D3dFont::draw).
    void texturedQuad(ID3D11ShaderResourceView* srv,
                      const Rectf& dst, const Rectf& uv, Color c);

    ID3D11Device*        device()  { return device_.Get(); }
    ID3D11DeviceContext* context() { return context_.Get(); }

protected:
    // Klipp-stacken (fasadens pushClip/popClip) landar här: flusha batchen
    // (scissor är context-state — allt ritat före bytet ska ha gamla
    // klippet), sätt scissor-rect + rätt rasterizer.
    //
    // Rundat barnklipp: maskerna går till PS-cbuffern b1 (b0 är VS:ens
    // viewportbuffer och RÖRS INTE) — se applyClip i d3d11_renderer.cpp.
    void applyClip(const Rectf* scissor, const RoundedClip* masks,
                   std::size_t count) override;

    // Render-målen (d3d11_target.cpp): RTV:n, viewporten och b0 för `t`,
    // eller framens eget mål (`frameRtv_`) för nullptr.
    void bindTarget(RenderTarget* t) override;

private:
    // HLSL i runtime (d3d11_renderer.cpp): kompilerar `src` till en blob,
    // nullptr + logg vid fel. Delad av UI-pipelinen, scenens program och de
    // egna programmen, som dessutom ger DEFINITIONERNA (en
    // nullterminerad D3D_SHADER_MACRO-lista) och ett `error` som tar emot
    // kompilatorns text i stället för loggen. Bytekoden cachas per PROCESS
    // (den är enhetsoberoende). En medlem för räknarens skull:
    // en kompilering — en miss i cachen — läggs i `RenderStats::compileNs`,
    // och en träff kostar ingenting. (ID3DBlob och D3D_SHADER_MACRO kommer
    // ur d3dcommon.h via d3d11.h.)
    ComPtr<ID3DBlob> compileHlsl(const std::string& src, const char* entry,
                                 const char* target,
                                 const D3D_SHADER_MACRO* defines = nullptr,
                                 std::string* error = nullptr);

    // GPU-TIDEN (renderer.h `setGpuTiming`): en ring om
    // `kGpuQueries` frames med en DISJOINT-fråga och två tidsstämplar var.
    // `beginFrame` läser de färdiga utan att tömma kön
    // (D3D11_ASYNC_GETDATA_DONOTFLUSH) och börjar en ny när tidtagningen är på
    // och en plats är ledig; `present` avslutar den före `Present`. En frame
    // med `Disjoint` kastas. Frågorna skapas i init — misslyckas det mäts
    // ingenting (`gpuTimingSupported_` falskt).
    struct GpuQuery {
        ComPtr<ID3D11Query> disjoint, begin, end;
        std::uint64_t       serial = 0;   // framen den mäter; 0 = ledig
    };
    static constexpr int kGpuQueries = 4;
    GpuQuery             gpuQueries_[kGpuQueries];
    int                  gpuQueryOpen_ = -1;   // platsen framen mäter i
    bool initGpuQueries();
    void collectGpuQueries();
    void startGpuQuery();

    // SDF-primitiven (rundade hörn, skuggor) bär sina parametrar per vertex
    // så de batchas i samma pipeline som allt annat — inga extra cbuffer-
    // uppdateringar eller egna Draw-anrop (målarordningen bevaras).
    // mode: 0 = texturmask (geometri/text), 1 = fylld rundad rektangel,
    // 2 = kontur, 3 = drop shadow, 4 = inner shadow, 5 = RGBA rakt av
    // (compositen av retained-RT:n — mode 0 tolkar texturen som R8-mask),
    // 6 = RGBA PREMULTIPLICERAD (ett render-måls bild, av-premultipliceras),
    // 7 = R8 som GRÅSKALA (en bild i formatet R8).
    struct Vertex {
        float         x, y;
        float         u, v;
        std::uint32_t rgba;    // R8G8B8A8_UNORM — packad gfx::Color
        float         p0[4];   // SDF: centrum.xy, halvmått.zw (px)
        float         p1[4];   // SDF: radie, bredd/blur, offset.xy
        float         mode;
    };
    // Vertexformatet är ETT designargument (masken
    // ligger i en UNIFORM och inte per vertex just för att de 56 bytena
    // inte ska växa i det vanliga fallet). Formatet är dessutom identiskt i
    // alla tre backends — det får inte glida tyst.
    static_assert(sizeof(Vertex) == 56, "Vertex måste vara 56 byte");

    // PS-cbuffern b1: samma layout som HLSL:ns
    // `cbuffer ClipMasks`. HLSL packar float4-arrayer 16-byte-alignat, och
    // float[4] gör detsamma här — därför en RÅ memcpy i uploadClipMasks.
    struct ClipMaskCb {
        float p0[4][4];   // centrum.xy + halvmått.zw per mask
        float p1[4][4];   // .x = radie
        float meta[4];    // .x = antal aktiva masker
    };
    static_assert(sizeof(ClipMaskCb) == 144 && sizeof(ClipMaskCb) % 16 == 0,
                  "ClipMaskCb måste vara 16-byte-packad som HLSL:ns b1");

    // De rundade klippmaskerna → b1.
    void uploadClipMasks(const RoundedClip* masks, std::size_t count);

    // VILKET PROGRAM som är bundet. `drawMesh` lämnar
    // scenens program kvar; `flush()` — det enda stället UI-pipelinen ritar
    // — återbinder UI-pipelinen innan den ritar nästa gång.
    enum class Pipeline { Ui, Line, Image, Fill, Glyph, Line3, Glyph3, Custom };
    Pipeline pipeline_ = Pipeline::Ui;
    void bindUiPipeline();
    // UI-pipelinens hela bindning — det beginFrame och beginOffscreen delar:
    // tillståndet, layouten, shadrarna, b0/b1, vertexbufferten och maskerna.
    void bindPipelineState();

    // RENDERLÄGET (d3d11_renderer.cpp). Varje scenprogram
    // tillämpar anropets läge med `applyMeshState` före sin Draw, och
    // UI-pipelinen sätter TILLBAKA sitt eget med `applyUiState` (djup av,
    // ingen gallring, rak alfa) när den tas i bruk igen — D3D11:s FÖRVALDA
    // djupläge har djupet PÅ så snart en DSV är bunden, så UI:t får aldrig
    // ärva något. Tillståndsobjekten cachas; `cur*` är det som är bundet och
    // `resetStateCache` glömmer det (framens början), så nästa sättare binder.
    void applyMeshState(const RenderState& s);
    void applyUiState();
    void resetStateCache();
    void setDepthState(ID3D11DepthStencilState* s);
    void setRasterState(ID3D11RasterizerState* s);
    void setBlendState(ID3D11BlendState* s);
    // Rasteriseraren för gallringen, biasen och djupklippningen — med
    // scissorn (`scissorOn_`) och MSAA (det bundna målets provtagning) som
    // gäller nu. UI:ts läge i ett mål utan MSAA är exakt raster_/
    // rasterScissor_, så 2D-vägen ritar som vanligt.
    ID3D11RasterizerState* rasterFor(Cull cull, int bias, float slope,
                                     bool depthClip);
    ComPtr<ID3D11DepthStencilState> depthStates_[7][2];   // [DepthTest][skriv]
    ComPtr<ID3D11BlendState>        blendStates_[5];      // [Blend]
    std::map<std::tuple<int, float, unsigned>, ComPtr<ID3D11RasterizerState>>
                             rasterCache_;
    bool                     rasterCacheFullLogged_ = false;
    // Det som är bundet — giltigt bara när motsvarande *Known_ är sann.
    ID3D11DepthStencilState* curDepth_    = nullptr;
    ID3D11RasterizerState*   curRaster_   = nullptr;
    ID3D11BlendState*        curBlend_    = nullptr;
    bool                     depthKnown_  = false;
    bool                     rasterKnown_ = false;
    bool                     blendKnown_  = false;
    bool                     scissorOn_   = false;   // senaste applyClip

    // MÅLEN MED DJUP OCH MSAA (d3d11_target.cpp). `bound_` är målet som är
    // bundet (nullptr = framens eget) — `bindTarget` löser upp det när det
    // lämnas — och `activeDsv_` dess djup. `msaaOk_[i]` = provtagningen
    // 2 << i går för både färgen och djupet (CheckMultisampleQualityLevels,
    // frågat EN gång i init).
    D3dRenderTarget*        bound_     = nullptr;
    ID3D11DepthStencilView* activeDsv_ = nullptr;
    bool                    msaaOk_[3] = {};
    bool                    samplesLogged_ = false;

    // Framen och blocket utanför den (beginOffscreen).
    bool inFrame_   = false;
    bool offscreen_ = false;

    // Scenens livscykel (d3d11_scene.cpp): programmet skapas i init och
    // rivs i shutdownRenderer; framens början nollar de transienta ringarna.
    bool initScene();
    void shutdownScene();
    void beginSceneFrame();

    // cbuffer b2 — samma layout som HLSL:ns `cbuffer Scene`: matrisen i
    // kolumnordning och parametrarna (målets storlek, breddens enhet och
    // läge). b0 (viewporten) och b1 (maskerna) rörs inte.
    struct SceneCb {
        float viewProj[16];
        float params[4];   // .xy målets storlek, .z px per breddenhet,
                           // .w 1 = bredden i världen
        // FÄRGEN (`kHlslPaintSrc`) — Fill-, Line- och
        // Glyph-programmen. Program som inte läser fälten deklarerar en
        // kortare cbuffer; det är tillåtet i D3D11.
        float paint0[4];   // linjär: från.xy, till.zw; radiell: mitt, r0, r1
        float paint1[4];   // .x färgens slag, .y opaciteten, .zw per program
        // KONTUREN (Line-programmet): .x spetsgränsen, .y strecken (antal
        // kumulativa slut, 0 = heldragen), .z mönsterenheter per
        // världsenhet, .w mönstrets förskjutning — och de kumulativa sluten.
        float stroke[4];
        float dash[8];
    };
    static_assert(sizeof(SceneCb) == 160 && sizeof(SceneCb) % 16 == 0,
                  "SceneCb måste vara 16-byte-packad som HLSL:ns b2");

    // IMAGE-PROGRAMMETS form av b2 (d3d11_image.cpp) — samma buffert, egen
    // kortare layout: matrisen, parametrarna (.xy målets storlek, .z
    // opaciteten, .w bildens flaggor) och FÄRGJUSTERINGEN
    // (`sceneImageAdjust`: .x mättnaden, .y kontrasten, .z ljusheten).
    struct ImageCb {
        float viewProj[16];
        float params[4];
        float adjust[4];
    };
    static_assert(sizeof(ImageCb) == 96 && sizeof(ImageCb) <= sizeof(SceneCb),
                  "ImageCb måste vara 16-byte-packad som Image-programmets b2 "
                  "och rymmas i scenens buffert");

    // MÅLARENS KLIPP — PS-cbuffern b5, samma layout som HLSL:ns
    // `cbuffer SceneClip` (kHlslClipSrc): kantplanen som float4 och antalet i
    // meta.x. Bunden i `bindPipelineState` bredvid maskerna; `bindSceneClip`
    // skriver den bara när ett rit-anrops klipp skiljer från `sceneClip_` —
    // det bufferten bär — så ett anrop utan klipp efter ett utan laddar inte
    // upp något. false när den inte gick att skriva: anropet ritas då inte.
    struct SceneClipCb {
        float planes[kMaxClipPlanes][4];
        float meta[4];
    };
    static_assert(sizeof(SceneClipCb) == 144 && sizeof(SceneClipCb) % 16 == 0,
                  "SceneClipCb måste vara 16-byte-packad som HLSL:ns b5");
    bool bindSceneClip(const MeshDraw& d);

    // Den transienta ringen per buffertslag: en DYNAMIC buffert som skrivs
    // framåt under framen (DISCARD först, NO_OVERWRITE sedan) och dubblas
    // när den inte räcker. En utväxt buffert kan bära delar som redan
    // lämnats ut i framen — den lever i `retired` till nästa frame.
    struct TransientRing {
        std::unique_ptr<D3dGpuBuffer>              buf;
        std::size_t                                offset = 0;
        bool                                       fresh  = true;
        std::vector<std::unique_ptr<D3dGpuBuffer>> retired;
    };
    TransientRing transient_[2];   // [0] vertex, [1] index

    ComPtr<ID3D11VertexShader> lineVs_;
    ComPtr<ID3D11PixelShader>  linePs_;
    ComPtr<ID3D11InputLayout>  lineLayout_;
    ComPtr<ID3D11Buffer>       sceneCbuf_;   // b2: scenens uniformer
    ComPtr<ID3D11Buffer>       sceneClipCbuf_;   // b5: målarens klipp
    SceneClip                  sceneClip_;       // det b5 bär
    // LINE3: samma källa med GLAZE_POS3 och en egen input
    // layout för `LineVertex3`. Misslyckas den loggas det och Line3 ritar
    // inget — Line-programmet står kvar.
    ComPtr<ID3D11VertexShader> line3Vs_;
    ComPtr<ID3D11PixelShader>  line3Ps_;
    ComPtr<ID3D11InputLayout>  line3Layout_;

    // IMAGE-PROGRAMMET (d3d11_image.cpp): världsquads ur
    // `ImageVertex`, perspektivkorrekt sampling, de projektiva vikterna `q`
    // (TEXCOORD1 — en bild på fyra fria hörn),
    // färgjusteringen och opaciteten. Uniformerna i scenens b2 i
    // programmets egen form (`ImageCb`).
    // Skapas i init efter scenens program och rivs före dem; misslyckas det
    // loggas det och `drawMesh` ritar inga bilder.
    bool initImageProgram();
    void shutdownImageProgram();
    void drawImageMesh(const MeshDraw& d);
    ComPtr<ID3D11VertexShader> imageVs_;
    ComPtr<ID3D11PixelShader>  imagePs_;
    ComPtr<ID3D11InputLayout>  imageLayout_;

    // FILL-PROGRAMMET (d3d11_fill.cpp): ytorna ur
    // `FillVertex` — trianglarna och kantremsan som extruderas 1 px utåt i
    // skärmen — och färgen, enfärgad eller en gradient ur rampen. Skapas i
    // init efter scenens program och rivs före dem; misslyckas det loggas
    // det och `drawMesh` ritar inga ytor.
    bool initFillProgram();
    void shutdownFillProgram();
    void drawFillMesh(const MeshDraw& d);
    // Färgens fält i b2 och rampens bindning (t0/s0) — delade av Fill-,
    // Line- och Glyph-programmen.
    void fillPaint(SceneCb& cb, const MeshDraw& d);
    void bindPaintRamp(const MeshDraw& d);
    ComPtr<ID3D11VertexShader> fillVs_;
    ComPtr<ID3D11PixelShader>  fillPs_;
    ComPtr<ID3D11InputLayout>  fillLayout_;

    // GLYPH-PROGRAMMET (d3d11_glyph.cpp): text och
    // markörer ur `GlyphVertex` som billboards, SDF-atlasen och formerna.
    // Skapas i init efter Fill-programmet och rivs före det. `drawGlyphMesh`
    // ritar också GLYPH3 — samma källa med GLAZE_POS3,
    // ankaret i xyz, egen input layout för `GlyphVertex3`.
    bool initGlyphProgram();
    void shutdownGlyphProgram();
    void drawGlyphMesh(const MeshDraw& d);
    ComPtr<ID3D11VertexShader> glyphVs_;
    ComPtr<ID3D11PixelShader>  glyphPs_;
    ComPtr<ID3D11InputLayout>  glyphLayout_;
    ComPtr<ID3D11VertexShader> glyph3Vs_;
    ComPtr<ID3D11PixelShader>  glyph3Ps_;
    ComPtr<ID3D11InputLayout>  glyph3Layout_;

    // EGNA PROGRAM (d3d11_program.cpp). GlazeFrame går i
    // en EGEN cbuffer b3 (DYNAMIC, 240 byte: `MeshDraw::frame` med det bundna
    // målets storlek i `target`), skapad i init; GlazeUser i programmets egen
    // b4. `curProgram_` är det BUNDNA programmets löpnummer — aldrig en
    // adress, för ett rivet programs adress kan återanvändas av nästa — och
    // `customSrvs_` hur många texturplatser ett eget program lämnat bundna:
    // `bindTarget` nollar dem i båda stegen (läs/skriv-hazarden).
    bool initProgramSupport();
    void shutdownProgramSupport();
    void drawCustomMesh(const MeshDraw& d);
    ComPtr<ID3D11Buffer> frameCbuf_;
    std::uint64_t        curProgram_ = 0;
    UINT                 customSrvs_ = 0;

    // SAMPLERN ÄR BATCH-TILLSTÅND: ett filter per batch, bundet i
    // flush — inte en gång per frame — så ett filterbyte mellan två ritningar
    // av samma bild gäller den andra, och scenens program (som binder sin
    // egen sampler per ritning) aldrig kan läcka in i UI-batchen. Text,
    // geometri och compositen tar Nearest uttryckligen.
    ID3D11SamplerState* samplerFor(ImageFilter f) const
    {
        return f == ImageFilter::Linear ? samplerLinear_.Get()
                                        : samplerNearest_.Get();
    }

    void flush();   // ritar batchade vertex med aktuell textur/topologi
    void setState(ID3D11ShaderResourceView* srv,
                  D3D11_PRIMITIVE_TOPOLOGY topo, ImageFilter filter);
    void quad(const Rectf& dst, const Rectf& uv, Color c);
    // Sex vertex för ett SDF-primitiv: dst är quadens täckyta (utökad för
    // AA/blur), prim själva rektangeln parametrarna beskriver.
    void sdfQuad(const Rectf& dst, Color c, float mode, const Rectf& prim,
                 float radius, float param, float offX, float offY);
    bool createBackbuffer(int w, int h);
    bool createOffscreen(int w, int h);   // retained-RT:n (texture+RTV+SRV)
    bool initComposition(HWND hwnd);      // dcomp-trädet (transparent-vägen)
    void updateAlphaMirror();             // retained-alfa → CPU (transparent)

    ComPtr<ID3D11Device>           device_;
    ComPtr<ID3D11DeviceContext>    context_;
    ComPtr<IDXGISwapChain1>        swapchain_;
    ComPtr<ID3D11RenderTargetView> rtv_;

    // Persistent offscreen-RT — frame-innehållet ritas hit och överlever
    // mellan frames (FLIP_DISCARD gör backbuffern odefinierad efter
    // Present; partiell omritning kräver en bevarad yta). Återskapas vid
    // resize. activeRtv_ = det ritningen just nu går till (offscreen före
    // composite, backbuffern efter) — ägs av offRtv_/rtv_.
    ComPtr<ID3D11Texture2D>          offTex_;
    ComPtr<ID3D11RenderTargetView>   offRtv_;
    ComPtr<ID3D11ShaderResourceView> offSrv_;
    ID3D11RenderTargetView*          activeRtv_ = nullptr;
    bool                             retainedValid_ = false;
    // FRAMENS EGET mål — det `bindTarget(nullptr)`
    // återvänder till: retained-RT:n från beginFrame, backbuffern efter
    // composite. `activeRtv_` är ett render-mål medan ett är bundet.
    ID3D11RenderTargetView*          frameRtv_ = nullptr;
    // Det BUNDNA målets storlek — viewporten, b0 och scenens cbuffer räknar
    // px → NDC mot den. Lika med width_/height_ utom i ett render-mål.
    int                              targetW_ = 0;
    int                              targetH_ = 0;
    void setViewport(int w, int h);   // viewport + b0 (UI:ts px → NDC)

    ComPtr<ID3D11VertexShader>     vs_;
    ComPtr<ID3D11PixelShader>      ps_;
    ComPtr<ID3D11InputLayout>      layout_;
    ComPtr<ID3D11Buffer>           vbuf_;
    ComPtr<ID3D11Buffer>           cbuf_;      // viewport-storlek → NDC i VS
    ComPtr<ID3D11Buffer>           clipCbuf_;  // b1: rundade klippmasker → PS
    int                            clipMaskCount_ = 0;   // senast uppladdat
    ComPtr<ID3D11RasterizerState>  raster_;
    ComPtr<ID3D11RasterizerState>  rasterScissor_;   // som raster_ + ScissorEnable
    ComPtr<ID3D11SamplerState>     samplerNearest_;  // text, geometri, composite
    ComPtr<ID3D11SamplerState>     samplerLinear_;   // bilder (förvalet)
    ComPtr<ID3D11Texture2D>        whiteTex_;  // 1×1 R8=255 för ofärgad geometri
    ComPtr<ID3D11ShaderResourceView> whiteSrv_;

    std::vector<Vertex>       batch_;
    ID3D11ShaderResourceView* curSrv_  = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY  curTopo_ = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ImageFilter               curFilter_ = ImageFilter::Nearest;
    int width_  = 0;
    int height_ = 0;
    bool unsyncedPresent_ = false;   // sant under WM_ENTER/EXITSIZEMOVE

    // Transparent-vägen. dcomp-objekten hålls som
    // IUnknown (dcomp.h dras bara in i d3d11_renderer.cpp — importlibben
    // saknas i MinGW, så DCompositionCreateDevice hämtas via LoadLibrary).
    // CPU-spegeln av retained-alfan uppdateras per present, ENDAST för
    // transparenta fönster — opaka betalar ingenting.
    bool                      transparent_ = false;
    ComPtr<IUnknown>          dcompDevice_;
    ComPtr<IUnknown>          dcompTarget_;
    ComPtr<IUnknown>          dcompVisual_;
    ComPtr<ID3D11Texture2D>   alphaStaging_;
    std::vector<std::uint8_t> alphaMirror_;   // en byte (alfa) per pixel
    int                       alphaW_ = 0;
    int                       alphaH_ = 0;

    static constexpr UINT kMaxVerts = 65536;
};

// gfx::Window ovanpå rå Win32: WS_OVERLAPPEDWINDOW vars ÖVRE non-client-yta
// tas bort i WM_NCCALCSIZE (eget chrome — storleksramen på de tre andra
// sidorna står kvar, osynlig, utanför den synliga kanten; OS:et behåller
// resize/Aero Snap/maximerings-animationerna), hit-test via WM_NCHITTEST,
// live-resize genom att rita direkt i WM_SIZE/WM_PAINT (den modala OS-loopen
// står annars still).
class D3dWindow : public Window {
public:
    // Ytans FAKTISKA alfa-status (fasadens fråga): begäran kan ha
    // degraderats vid init.
    bool transparent() const override { return transparent_; }

    // Chromets hörnradie (fysiska px). LAGRAS men används inte:
    // rundade fönster är påslagna på Linux — macOS rundar själv och
    // Windows har inget eget pass. Raden finns så att fasaden är
    // komplett.
    void setChromeRadius(float radius) override { chromeRadius_ = radius; }

    D3dWindow() = default;
    ~D3dWindow() override { shutdown(); }
    D3dWindow(const D3dWindow&)            = delete;
    D3dWindow& operator=(const D3dWindow&) = delete;

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
    float uiScale() const override;       // dpi/96 × gfx::userScale()
    void  userScaleChanged() override;    // se window.h-kontraktet
    // SKÄRMENS mm — mätt i `measureDisplay`, läst här.
    DisplayMetrics displayMetrics() const override { return display_; }
    bool  maximized() const override;
    // FÖNSTRETS GEOMETRI — se d3d11_window.cpp.
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
    void setHitRegions(std::vector<HitRegion> regions) override
    {
        hitRegions_ = std::move(regions);
    }
    void setCaptionHeight(float logicalPx) override
    {
        captionH_ = logicalPx;
    }
    void releaseMouseCapture() override;
    // MUSENS CAPTURE HIT mitt i ett tryck — se
    // d3d11_window.cpp: `SetCapture` när ett av appens fönster håller
    // capture för ett TRYCK och appens tråd äger förgrunden. Och frågan:
    // håller just det här fönstret capturen för ett tryck?
    bool captureMouse() override;
    bool hasMouseCapture() const override;
    // MUSPEKAREN — se d3d11_window.cpp. Formen lagras alltid och
    // visas direkt bara när pekaren står i klientytan eller vi håller
    // capture; resten sköter WM_SETCURSOR.
    void        setCursor(PointerShape shape) override;
    PointerShape cursor() const override { return cursor_; }
    // PROGRAMIKONEN — se d3d11_window.cpp. Ramarna lagras och
    // blir WM_SETICON small/big i fönstrets DPI; WM_DPICHANGED bygger om.
    void setIcon(const WindowIcon& icon) override;
    bool iconApplied() const override { return iconApplied_; }
    void setChromeColors(const ChromeColors& c) override
    {
        chromeColors_ = c;
    }
    void startTimer(std::uint32_t id, std::uint32_t ms) override;
    void stopTimer(std::uint32_t id) override;
    void setTextInputActive(bool) override {}   // WM_CHAR levereras alltid
    bool retainedValid() const override
    {
        return gfxRenderer_.retainedValid();
    }
    bool captureFrame(std::vector<std::uint8_t>& rgba, int& w,
                      int& h) override;
    Renderer& renderer() override { return gfxRenderer_; }

    // OS-fönstret — utskriftsdialogens ägare (d3d11_print.cpp: PrintDlgEx
    // kräver ett giltigt handtag). nullptr före init och efter shutdown.
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK wndProcThunk(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp);
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT hitTest(float x, float y) const;
    Sizef   clientSize() const;
    void    onResize();   // swapchain-resize + omritning (live-resize)

    HWND         hwnd_    = nullptr;
    // AKTIVITETSFÄLTET: `WindowDesc::showInTaskbar = false`
    // på ett OFÄGT fönster ger ett eget DOLT ÄGARFÖNSTER — ett ägt
    // toppnivåfönster har ingen knapp. Skapas i init före fönstret och rivs
    // EFTER det (shutdown och init-felvägarna): OS:et förstör ett ägt fönster
    // med sin ägare, och fönstret ska rivas av vår egen väg och inte så.
    HWND         taskbarOwner_ = nullptr;
    void         destroyTaskbarOwner();
    D3dRenderer  gfxRenderer_;
    Chrome       chrome_  = Chrome::Native;
    bool         transparent_ = false;   // per-pixel-alfa (tvingar None-chrome)
    float        chromeRadius_ = 0.0f;   // fönstrets hörn (fysiska px)
    // STORLEKSGRÄNSERNA i LOGISKA px, 0 = obegränsat.
    // Lagras och räknas om i WM_GETMINMAXINFO — alltså vid FRÅGAN, vilket
    // gör skalbyten gratis här (de tre andra backendarna måste
    // återapplicera).
    int          limMinW_ = 0, limMinH_ = 0, limMaxW_ = 0, limMaxH_ = 0;
    bool         fixedSize() const
    {
        return limMinW_ > 0 && limMinW_ == limMaxW_ &&
               limMinH_ > 0 && limMinH_ == limMaxH_;
    }
    // Maskar WS_THICKFRAME/WS_MAXIMIZEBOX — och för eget chrome står
    // KLIENTYTAN STILL när ramen kommer eller går.
    void         applyResizeStyle();

    // STORLEKSRAMEN UTANFÖR DEN SYNLIGA KANTEN. Ett
    // ändringsbart `Chrome::Custom`-fönster behåller OS:ets vanliga, av DWM
    // osynliga, ram på vänster-, höger- och nederkanten — bara överkantens
    // icke-klientyta tas bort (Windows Terminals recept). Två frågor och inte
    // en, för de svarar på olika saker:
    //
    //   normalFrame()  NORMALLÄGETS ram {L, 0, R, B} i fysiska px — för ett
    //                  Custom-fönster som inte är fast, annars noll. Gäller
    //                  också medan fönstret är maximerat: storleks-
    //                  omräkningarna och WM_GETMINMAXINFO talar normalläget.
    //                  Måtten ur `AdjustWindowRectExForDpi` med
    //                  WS_OVERLAPPEDWINDOW — den stil ett ändringsbart
    //                  fönster har — i fönstrets DPI: EN sanning med
    //                  DefWindowProc:s ram.
    //   frameInsets()  AKTUELL skillnad mellan fönster- och klientrektangeln:
    //                  maximerat Custom-fönster ⇒ den aktuella insetten på alla fyra
    //                  sidor (Windows skjuter ut ramen utanför skärmen),
    //                  annars `normalFrame()`. WM_NCCALCSIZE och setPosition.
    RECT         normalFrame() const;
    RECT         frameInsets() const;
    // KLIENT → FÖNSTER i fysiska px, relativt klientens origo: Native med
    // `AdjustWindowRect`, Custom med `normalFrame()`, None rakt av.
    // De fem omräkningsställena (setSize, restore-flanken, WM_GETMINMAXINFO,
    // userScaleChanged och klientytan i applyResizeStyle) går hit.
    RECT         windowRectFor(int physW, int physH) const;
    // ALLTID ÖVERST — begäran, lagrad för tiden FÖRE
    // CreateWindowExW. Efter det är `WS_EX_TOPMOST` sanningen och
    // `topmost()` läser den i stället för fältet.
    bool         topmost_ = false;
    ChromeColors chromeColors_;   // defaultvärdena tills appen sätter tema
    std::string  title_;
    RenderFn    renderCb_;
    RenderFn    overlayCb_;      // ritas efter composite + chrome
    WindowId    windowId_ = 0;   // stämplas i init (0 = inget fönster)
    bool        dirty_   = false;

    // Hit-test-kartan: glaze trycker ner regionerna
    // + captionens höjd ur widget-trädet; hitTest() slår upp här.
    std::vector<HitRegion> hitRegions_;
    float                  captionH_ = 32.0f;   // logiska px (default =
                                                // TopBar-defaulten)
    bool        noActivate_  = false;   // WS_EX_NOACTIVATE — visa utan fokus
    // FÖRSTA VISNINGEN AKTIVERAR INTE (`WindowDesc::showInactive`) —
    // läst av `showFirst`, aldrig av en senare visning.
    bool        showInactive_ = false;
    // FÖNSTRET HÅLLER CAPTURE FÖR ETT TRYCK — vårt eget `SetCapture` vid
    // knapptrycket, eller ett tryck som lämnats över hit (`captureMouse`).
    // `WM_CAPTURECHANGED` blir `MouseCaptureLost` bara medan flaggan står:
    // vårt eget släpp nollar den FÖRE `ReleaseCapture` (meddelandet kommer
    // synkront under anropet), och OS:ets modala flytt- och storleksloop —
    // som tar och släpper capture utan att ett tryck nått oss — ger inget
    // avbrott. `captureMouse` läser den hos HÅLLAREN: utan flaggan finns
    // inget tryck att ta (capturen är loopens).
    bool        pressCapture_ = false;
    bool        mouseInside_ = false;   // för TrackMouseEvent/MouseLeave —
                                        // och för setCursor: pekaren står i
                                        // klientytan
    PointerShape cursor_      = PointerShape::Arrow;   // den begärda formen
    // PEKARNA: fasadens form → HCURSOR. Systempekarna
    // är DELADE handtag och förstörs aldrig; den stängda handen, som Windows
    // saknar, är fönstrets EGEN — byggd ur src/cursor_bitmaps.h i
    // pekarstorleken för fönstrets DPI (`closedHandSide_`), byggd om när den
    // ändras och riven i shutdown. `Hidden` är nullptr, som `SetCursor`
    // tolkar som ingen pekare.
    HCURSOR      cursorHandle(PointerShape s);
    HCURSOR      closedHand_     = nullptr;
    int          closedHandSide_ = 0;

    // FÖNSTRETS GEOMETRI: normallägets hörn i
    // placeringsrymden (fysiska px, samma rymd som setPosition) — för
    // `Chrome::Custom` KLIENTENS hörn (den osynliga ramen räknas bort),
    // för Native ytterhörnet. Spåras i WM_MOVE/WM_SIZE när
    // fönstret varken är zoomat eller ikoniserat, och sätts i init innan
    // fönstret visats — så en maximerad start bär den begärda rutan.
    // Storleken är `logicalW_`/`logicalH_`, som redan står still under
    // maximeringen.
    int          normalX_ = 0, normalY_ = 0;
    void         trackNormalPosition();
    // STARTLÄGET: `desc.startState`, tillämpat vid FÖRSTA synligheten —
    // i init, eller i den första `setVisible(true)` efter en dold start.
    WindowState  startState_ = WindowState::Normal;
    bool         firstShown_ = false;
    void         showFirst();

    // PROGRAMIKONEN. Ramarna lagras (tvåfas: init tillämpar
    // dem före ShowWindow) och HICON:erna ägs HÄR: WM_SETICON kopierar inte,
    // så den gamla ikonen rivs först när den nya är satt, och de sista
    // efter DestroyWindow. `applyIcon` väljer om ur ramarna i AKTUELL DPI.
    WindowIcon  icon_;
    HICON       iconSmall_   = nullptr;
    HICON       iconBig_     = nullptr;
    bool        iconApplied_ = false;
    void        applyIcon();

    bool        wasMaxOrMin_ = false;   // WindowRestored bara efter max/min
    wchar_t     highSurrogate_ = 0;     // väntande WM_CHAR-surrogathalva
    // En WM_CHAR-kodenhet → ett helt tecken som UTF-8 (true), med
    // surrogatparen och styrteckenfiltret — WM_CHAR och WM_KEYDOWN:s uttag.
    bool        charText(wchar_t ch, std::string& out);

    // Lagrad LOGISK klientstorlek (invarianten): källan när
    // fysisk storlek räknas om vid skalbyte. Sätts i init/setSize och
    // räknas tillbaka ur WM_SIZE vid ANVÄNDAR-resize — aldrig ur vår
    // egen skalresize (pendingScaleResize_-vakten; avrundningen driftar
    // annars) och aldrig vid maximering (restore ger rätt storlek).
    int         logicalW_ = 0, logicalH_ = 0;
    bool        pendingScaleResize_ = false;

    // SKÄRMENS FYSISKA UPPLÖSNING. `measureDisplay` läser
    // EDID ur registret och räknar täthet ur diagonalen (display_util.h) —
    // men BARA när skärmen, dess pixlar eller DPI:n ändrats efter förra
    // mätningen, för den körs vid varje WM_MOVE. `announce` köar
    // `WindowDisplayChanged` när svaret blev ett annat.
    void           measureDisplay(bool announce);
    HMONITOR       displayMon_ = nullptr;
    int            displayW_   = 0, displayH_ = 0;
    UINT           displayDpi_ = 0;
    DisplayMetrics display_;

    // STYRPLATTAN (d3d11_touchpad.h): Direct Manipulation för
    // tvåfingersdraget och nypningen, råa HID-kontakter för vridningen.
    // Kopplas när fönstret VISAS första gången — i init, eller i
    // `setVisible(true)` för ett fönster som startade dolt (ett
    // offscreen-fönster visas aldrig och kopplas aldrig) — och
    // lossas i shutdown FÖRE DestroyWindow; `touchpad_.attached()` säger att
    // det är gjort.
    D3dTouchpad    touchpad_;
};

} // namespace gfx
