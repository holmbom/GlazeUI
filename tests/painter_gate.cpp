// painter_gate — RITYTANS LAGER OCH MÅLARE headless.
//
// En RIKTIG `glaze::Canvas` ritas mot en INSPELANDE renderer (NullRenderer
// med buffertar som håller sina byte) och ett riktigt `CanvasResources` och
// `ImageCache` — samma väg som i ett fönster, utan GPU. Grinden läser vad
// som hamnade i rit-anropen och i statistiken (`Renderer::stats()`), aldrig
// lagrets egna strukturer.
//
// VAD GRINDEN PÅSTÅR:
//   K  KÖRNINGARNA: i ett Strict-lager är formernas index målarordningen —
//      två ytor med kontur och två linjer blir Fill, Line, Fill, Line, och
//      konturen och linjerna med samma stil delar ETT anrop; i ByKind blir
//      samma former Fill, Line (alla ytor före alla linjer). Markörerna
//      sist, i ett anrop.
//   L  LAGREN i ordning, ett dolt lager ritas inte, och zoomintervallet.
//   R  RESIDENSEN: efter första ritningen ger kameraändringar (mitt, zoom,
//      bäring, tilt) INGEN uppladdning och ingen ny buffert — också för en
//      chunk som legat utanför bild och panoreras in.
//   T  TILLÄGGET laddar upp bara den nya formens byte (vertex + index), i
//      samma buffert.
//   B  BORTTAGNINGEN laddar upp bara formens indexintervall, och det är
//      degenererat (ingen yta).
//   C  KOMPAKTERINGEN: en åttondel döda ⇒ ingen ombyggnad; över en fjärdedel
//      ⇒ lagret byggs om (nya buffertar) och bara de levande ritas.
//   G  GALLRINGEN: av två linjer långt isär ritas den som syns.
//   I  ERSÄTTNINGEN PÅ PLATS: samma antal punkter ⇒ bara formens intervall
//      laddas upp; fler punkter ⇒ lagret byggs om.
//   N  RUTNÄTET: en statisk mesh — skapas en gång, kameran laddar inte upp.
//   P  MÅLAREN: två linjer i rad är ETT transient anrop, en yta ett till;
//      `onPaint` ritar i världen (kameran gäller) och `onPaintOverlay` i
//      ritytans lokala px — i OVERLAY-LAGRET (`drawOverlay`, inte
//      ritytans egen ritning); inga buffertar skapas.
//   V  TONEN: en inaktiverad rityta ritar allt med opaciteten 1 − iconFade.
//   O  KOLLEKTIONSVOKABULÄREN: index utanför ⇒ ingenting händer.
//   X  TEXTEN byggs mot fönstrets atlas (en testfont): kameran laddar inte
//      upp, en ändrad text bygger om.
//   Y  ETIKETTERNA: krockande etiketter ger Glyph-anropen en
//      synlighetsbild, varje körning sin BAS (det andra lagrets körning
//      börjar efter det förstas etiketter); samma kamera placerar inte om
//      (ingen räkning, ingen uppladdning), en kameraändring gör det och
//      laddar upp högst en byte per etikett; utan kolliderande etiketter
//      ingen bild alls.
//   L3 LINJERNA I RYMDEN: en Line3-form ritas med
//      Line3-programmet (genom 3D-målet — renderaren här ger ett mål som
//      bara bär måtten), och efter första ritningen laddar en kameraändring
//      INGENTING upp och skapar ingen buffert — varken med kartkameran eller
//      med en `Camera3D` — medan målet ritas om (`targetRenders`).
//   TD TEXTDELARNA OCH FYRHÖRNINGEN i målaren:
//      `drawText(delar)` ger byte för byte lagrets vertex för
//      `Shape::text(delar)` (färgen per del finns där), `measureText(delar)`
//      är lagrets etikettlåda (plattans quad med luften 0), och en fluga ger
//      inget Image-anrop medan en trapets ger ett. Sabotage som ska fälla
//      den: målarens `addGlyph`
//      med etikettens `text()` i stället för `textParts` (TD vertex), och
//      `partKeys` utan `imageQuadOk` (TD fluga).
//   LO LAGRENS ORDNING (`Canvas::moveLayer`): LO1 tre lager
//      flyttas och tillbaka med SAMMA vertexbuffertar i den nya ordningen
//      och 0 byte, till samma plats och utanför ändrar ingenting; LO2
//      egenskaperna (ritordningen, synligheten, opaciteten, kollisionen,
//      placeringen, vyerna) följer lagret; LO3 huvudvyn OCH en del-vy ritar
//      den nya ordningen ur samma buffertar; LO4 World-lagren byter ordning i
//      3D-målet och instanserna får ingen ny kopia; LO5 krockande etiketter
//      får baserna i den nya ordningen med högst en byte per etikett; LO6 en
//      pågående etikettoning stannar; LO7 två Terrain-lager byter ordning
//      utan att etiketten vid markhöjd byggs om (terrängens version är
//      ordningsoberoende — drape.cpp). Sabotage som SKA fälla den:
//      flytten som `setLayers` + nytt innehåll (LO1 —
//      nya buffertar), `L.dirty = true` i flytten (LO1, LO4 — ombyggnad),
//      rotationen åt fel håll (LO1, LO3, LO4 — ordningen), `terrainVersion`
//      i vektorns ordning igen (LO7 — ett nytt bygge och byte).
//   GT GOLVET OCH TAKET: GT1 anropets glyphMinPx/MaxPx =
//      gränserna i px (× skalan), mm (× px/mm) och world (× kamerans skala);
//      GT2 olika gränser är olika körningar; GT3 två världsetiketter som inte
//      krockar råa krockar vid golvet och en döljs (texeln 0); GT4 en etikett
//      15 px utanför ytan med golvet 40 px gallras inte.
//   ZV SYNLIGHETEN EFTER ZOOM: ZV1/ZV3 texeln 0 utanför intervallet och 255
//      innanför, också utan kollision (bilden finns); ZV2 en dold etikett
//      tränger inte undan sin granne; ZV4 målaren hoppar över formen; ZV5
//      toningen ger ett mellanvärde över tröskeln.
//   MB MINNESBUDGETEN PER RITYTA, två ritytor i SAMMA bildcache:
//      MB1 A över sin budget vräker sin äldsta och B:s äldre bild står kvar;
//      MB2 kontot är den senaste ritytans; MB3 en riven rityta lämnar sina
//      bilder åt fönstret; MB4 `accountBytes` är summan av kontots poster.
//      Sabotage som SKA fälla dem: uniformen
//      bort (GT1), gränserna utanför `RunKey` (GT2), placeringen med rå
//      storlek (GT3), marginalen utan `reachEm` (GT4), zoomkontrollen bort
//      (ZV1), en dold etikett som tar plats (ZV2), bilden släppt utan
//      kollision (ZV3), målaren utan zoom (ZV4), en enda pool (MB1), kontot
//      vid FÖRSTA ritningen (MB2), döda konton kvar (MB3). MÄTER INTE pixlar,
//      draperingens konto (ingen bänk har två terrängytor) eller meshar.
//   KL MÅLARENS KLIPP (`Painter::save(Rect)`), läst ur anropens
//      `SceneUniforms::clip` och renderarens scissor vid anropet; förväntat värde är
//      rektanglarnas geometri (planen är normerade — värdet är avståndet):
//      KL1 axelparallellt: fyra plan, 40 px in i mitten och −5 px utanför
//      varje kant, samma klipp på ytan och linjen i läget, scissorn är
//      rektangeln, och efter läget inget klipp och ritytans scissor; KL2
//      vridet 30°: planen ger kantavstånden i rektangelns egna koordinater
//      (också i den omskrivna rutans hörn) och scissorn är den omskrivna
//      rektangeln; KL3 nästlat (en axelparallell och en 45° vriden): sex
//      kanter, det minsta av ytornas kantavstånd, och det yttre klippet igen
//      efteråt; KL4 utan bredd, NaN, två klipp utan snitt och ett klipp
//      utanför ritytan ritar ingenting och lämnar stacken i balans; KL5 tre
//      vridna kvadrater (tolv kanter) blir åtta plan som bara släpper in det
//      som ligger i alla tre; KL6 i världen under bäring, tilt och zoom är
//      planen rektangelns bild där `toLocal` lägger hörnen; KL7 `meshDrawOk`
//      avvisar för många plan och ett NaN-plan men läser inte planen efter
//      `count`. Sabotage som SKA fälla dem:
//      `d.uniforms.clip` bort ur `drawTransient` (KL1–KL6), klippet utan
//      flush i `restore` (KL1 — ytan efter läget klipps), normalen utan
//      normering (KL1/KL2 — värdena skalas), `intersectClip` bort (KL3 —
//      bara den inre ytan, KL4 — de två utan snitt ritar), hörnet skuret
//      med STÖRST yta i `cutCorners` (KL5 — två grannhörn skärs, mitten
//      hamnar under 44), `popClip` bort ur
//      `restore` (KL1, KL4 — scissorn står kvar), projektionen med
//      skärmens matris också i världen (KL6), och `clipOk` bort (KL7). MÄTER
//      INTE pixlarna (scene_pixel_gate K, scene3d_pixel_gate KL), föremålen i
//      rymden (NullRenderer bygger inga program) eller klippet i 3D-målet.
//
// VAD GRINDEN INTE MÄTER: pixlar (scene_pixel_gate gör det genom
// den riktiga backenden — lagrens ordning efter en flytt syns bara i
// anropens ordning här, och draperingens omritning efter en flytt bland
// Ground-lagren mäts inte alls), etikettkollisionen, gesterna,
// bilderna i lagren (bildcachens egen grind, scene_pixel_gate) och
// precisionen (scene_pixel_gate A6). Uppladdningarnas FORM (vilka byte) mäts bara som
// antal byte per anrop och som indexbuffertens innehåll. LO5 mäter bara att
// en flytt laddar upp HÖGST en byte per etikett och att Glyph-anropens baser
// följer den nya ordningen — inte att placeringen faktiskt räknas om, att
// synlighetsbilden laddas upp hel (taket fäller inte noll byte), eller vilken
// av de krockande etiketterna som syns efter flytten
// (medvetet lämnat).
//
// SABOTAGEN den fäller: synken som laddar upp
// varje ritning (R), kompakteringen bort (C), gallringen bort (G), ByKind som
// Strict (K), borttagningen utan degenerering (B) och residensen efter
// gallringen i stället för före (R, den panorerade chunken) och Line3-chunkarna
// uppladdade hela varje ritning
// (L3 — 7424 byte på fyra kameraändringar, 5568 i orbiten).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <glaze/glaze.h>

#include "canvas/residency.h"   // intern — föremålens residens (fall LO)
#include "canvas/resources.h"   // intern — fönstrets resurser
#include "image_cache.h"        // intern — fönstrets bildcache

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// ------------------------------------------------------ den inspelande renderaren

// En buffert som HÅLLER sina byte — så grinden kan läsa indexintervallen.
class Buffert : public gfx::GpuBuffer {
public:
    Buffert(gfx::BufferKind k, std::size_t n) : kind_(k), data(n, 0) {}
    gfx::BufferKind kind() const override { return kind_; }
    std::size_t     size() const override { return data.size(); }
    bool            valid() const override { return true; }

    gfx::BufferKind           kind_;
    std::vector<std::uint8_t> data;
};

class Bild : public gfx::Image {
public:
    Bild(int w, int h, gfx::ImageFormat f) : w_(w), h_(h), f_(f)
    {
        // En R8-bild HÅLLER sina texlar — etiketternas synlighet (fall ZV).
        if (f == gfx::ImageFormat::R8)
            px.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    }
    int              width() const override { return w_; }
    int              height() const override { return h_; }
    bool             valid() const override { return true; }
    gfx::ImageFormat format() const override { return f_; }
    bool             premultiplied() const override { return false; }
    bool             flippedV() const override { return false; }

    std::vector<std::uint8_t> px;   // bara R8

private:
    int              w_, h_;
    gfx::ImageFormat f_;
};

// Ett render-mål som bara bär sina mått — ritytans 3D-mål
// ritas då som vilket mål som helst, och rit-anropen i det spelas in.
class Mål : public gfx::RenderTarget {
public:
    Mål(int w, int h, const gfx::TargetDesc& d)
        : bild_(w, h, gfx::ImageFormat::Rgba8), samples_(d.samples),
          djup_(d.depth)
    {
    }
    gfx::Image& image() override { return bild_; }
    int         samples() const override { return samples_; }
    bool        hasDepth() const override { return djup_; }

private:
    Bild bild_;
    int  samples_;
    bool djup_;
};

// EN FONT FÖR ATLASEN: varje glyf utom mellanslaget är en fylld ruta. Det
// räcker för att texten ska byggas — formen på bokstäverna mäts i G3.
class Font : public gfx::NullFont {
public:
    explicit Font(float size) : size_(size) {}
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv() * static_cast<float>(s.size()), height()};
    }
    float height() const override { return size_ * 1.25f; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string&, std::size_t bytes) const override
    {
        return adv() * static_cast<float>(bytes);
    }
    std::size_t fitWidth(const std::string& s, float) const override
    {
        return s.size();
    }
    float ascent() const override { return size_; }
    bool  rasterGlyph(char32_t cp, gfx::GlyphRaster& out) const override
    {
        out         = gfx::GlyphRaster{};
        out.advance = adv();
        if (cp == U' ')
            return true;
        out.width  = static_cast<int>(size_ * 0.5f);
        out.height = static_cast<int>(size_ * 0.7f);
        out.left   = size_ * 0.05f;
        out.top    = -size_ * 0.7f;
        out.coverage.assign(static_cast<std::size_t>(out.width * out.height), 255);
        return true;
    }

private:
    float adv() const { return size_ * 0.6f; }
    float size_;
};

struct Anrop {
    gfx::MeshProgram    program;
    std::uint32_t       indexCount;
    float               pxPerWidthUnit;
    gfx::WidthSpace     space;
    float               opacity;
    const gfx::GpuBuffer* vb;
    const gfx::GpuBuffer* ib;
    std::size_t         ibOffset;
    float               viewProj[16];
    const gfx::Image*   labels;      // etiketternas synlighet
    std::uint32_t       labelBase;
    // Målet anropet ritades i (nullptr = framens eget) — 3D-målet och
    // varje del-vys mål är egna (fall LO).
    const gfx::RenderTarget* mål;
    float glyphMinPx, glyphMaxPx;   // golvet och taket (fall GT)
    // Målarens klipp och renderarens scissor vid anropet (fall KL).
    gfx::SceneClip clip;
    bool           harScissor;
    gfx::Rectf     scissor;
};

class Inspelare : public gfx::NullRenderer {
public:
    std::vector<Anrop>                    anrop;
    std::vector<std::unique_ptr<Buffert>> transienta;   // framens ring

    std::unique_ptr<gfx::GpuBuffer> createBuffer(gfx::BufferKind kind,
                                                 std::size_t bytes,
                                                 const void* data) override
    {
        auto b = std::make_unique<Buffert>(kind, bytes);
        if (data) {
            std::memcpy(b->data.data(), data, bytes);
            stats_.bytesUploaded += bytes;
        }
        ++stats_.buffersCreated;
        return b;
    }
    bool updateBuffer(gfx::GpuBuffer& b, std::size_t offset, const void* data,
                      std::size_t bytes) override
    {
        auto& bb = static_cast<Buffert&>(b);
        if (!updateOk(b, offset, data, bytes))
            return false;
        std::memcpy(bb.data.data() + offset, data, bytes);
        stats_.bytesUploaded += bytes;
        return true;
    }
    gfx::BufferSlice uploadTransient(gfx::BufferKind kind, const void* data,
                                     std::size_t bytes) override
    {
        auto b = std::make_unique<Buffert>(kind, bytes);
        std::memcpy(b->data.data(), data, bytes);
        stats_.bytesUploaded += bytes;
        transienta.push_back(std::move(b));
        return gfx::BufferSlice{transienta.back().get(), 0, bytes};
    }
    void drawMesh(const gfx::MeshDraw& d) override
    {
        if (!meshDrawOk(d, true))
            return;
        Anrop a{d.program,         d.indexCount,      d.uniforms.pxPerWidthUnit,
                d.uniforms.widthSpace, d.uniforms.opacity, d.vertices.buffer,
                d.indices.buffer,  d.indices.offset,  {},
                d.labels,          d.labelBase,       boundTarget(),
                d.uniforms.glyphMinPx, d.uniforms.glyphMaxPx,
                d.uniforms.clip,   false,             {}};
        std::memcpy(a.viewProj, d.uniforms.viewProj, sizeof a.viewProj);
        a.harScissor = clipBounds(a.scissor);
        anrop.push_back(a);
        ++stats_.drawCalls;
    }
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int w, int h, gfx::ImageFormat f,
                                            const void* px, std::size_t,
                                            bool) override
    {
        if (px)
            stats_.bytesUploaded +=
                static_cast<std::uint64_t>(w) * h * gfx::bytesPerPixel(f);
        return std::make_unique<Bild>(w, h, f);
    }
    bool updateImage(gfx::Image& img, int x, int y, int w, int h, const void* px,
                     std::size_t stride) override
    {
        stats_.bytesUploaded +=
            static_cast<std::uint64_t>(w) * h * gfx::bytesPerPixel(img.format());
        // En R8-bild ur `createImage` håller texlarna (etiketternas synlighet).
        if (auto* b = dynamic_cast<Bild*>(&img);
            b && !b->px.empty() && px && x >= 0 && y >= 0 &&
            x + w <= b->width() && y + h <= b->height()) {
            const std::size_t rad = stride ? stride : static_cast<std::size_t>(w);
            for (int r = 0; r < h; ++r)
                std::memcpy(b->px.data() +
                                static_cast<std::size_t>(y + r) * b->width() + x,
                            static_cast<const std::uint8_t*>(px) + r * rad,
                            static_cast<std::size_t>(w));
        }
        return true;
    }
    std::unique_ptr<gfx::Font> loadFont(gfx::FontFamily, float size,
                                        gfx::FontStyle) override
    {
        return std::make_unique<Font>(size);
    }
    using gfx::NullRenderer::createRenderTarget;
    std::unique_ptr<gfx::RenderTarget> createRenderTarget(
        int w, int h, const gfx::TargetDesc& d) override
    {
        return std::make_unique<Mål>(w, h, d);
    }
};

// ------------------------------------------------------------- ritytan

// Ritytan med overlay-lagrets krokar nåbara — fönstret anropar dem i en riktig
// present (middle_gate mäter den vandringen); här anropas de direkt.
struct Rityta : glaze::Canvas {
    using glaze::Canvas::drawOverlay;
    using glaze::Canvas::hasOverlay;
};

struct Bänk {
    Inspelare              r;
    Font                   font{13.0f};
    glaze::ImageCache      bilder{r};
    glaze::CanvasResources res{r};
    Rityta                 yta;
    glaze::Ctx             c{};

    Bänk()
    {
        c.r           = &r;
        c.font        = &font;
        c.headingFont = nullptr;
        c.scale       = 1.0f;
        c.area        = gfx::Rectf{0.0f, 0.0f, 800.0f, 600.0f};
        c.theme       = &glaze::Theme::slateDefault().light;
        c.ui          = glaze::UI::Slate;
        c.style       = &glaze::defaultStyle(glaze::UI::Slate);
        c.images      = &bilder;
        c.pxPerMm     = 4.0f;
        c.canvas      = &res;
        c.box         = gfx::Rectf{10.0f, 20.0f, 400.0f, 300.0f};
    }

    // EN FRAME, som fönstret gör den: framens början (trim) och ritningen.
    void frame()
    {
        bilder.trim(std::size_t{256} << 20, std::size_t{16} << 20);
        res.trim();
        r.transienta.clear();
        r.anrop.clear();
        yta.render(c);
    }
    std::vector<gfx::MeshProgram> program() const
    {
        std::vector<gfx::MeshProgram> p;
        for (const Anrop& a : r.anrop)
            p.push_back(a.program);
        return p;
    }
};

std::string namn(const std::vector<gfx::MeshProgram>& p)
{
    std::string s;
    for (gfx::MeshProgram m : p) {
        if (!s.empty())
            s += " ";
        switch (m) {
        case gfx::MeshProgram::Line:   s += "Line"; break;
        case gfx::MeshProgram::Fill:   s += "Fill"; break;
        case gfx::MeshProgram::Glyph:  s += "Glyph"; break;
        case gfx::MeshProgram::Image:  s += "Image"; break;
        case gfx::MeshProgram::Line3:  s += "Line3"; break;
        case gfx::MeshProgram::Glyph3: s += "Glyph3"; break;
        default:                       s += "?"; break;
        }
    }
    return s.empty() ? "(inga)" : s;
}

using P = gfx::MeshProgram;

glaze::Shape yta(double x, double y, gfx::Color fyll)
{
    return glaze::Shape::rect({x, y, 40, 30}, fyll,
                              glaze::Stroke{.paint = gfx::Color{0, 0, 0, 255},
                                            .width = 2.0f});
}

// En linje med TRE punkter: två segment och en skarv — 12 vertex och 18
// index (gfx/geom2d.h: fyra vertex per segment och per rund skarv, sex index
// per bit). Grindens förväntade värde för en forms storlek.
glaze::Shape linje(double x, double y)
{
    return glaze::Shape::polyline({{x, y}, {x + 30, y}, {x + 30, y + 20}},
                                  glaze::Stroke{.width = 2.0f});
}
constexpr std::size_t kLinjeVertexByte = 12 * sizeof(gfx::LineVertex);
constexpr std::size_t kLinjeIndex      = 18;
constexpr std::size_t kLinjeByte = kLinjeVertexByte + kLinjeIndex * 4;

glaze::Shape markör(double x, double y)
{
    return glaze::Shape::marker({x, y}, glaze::MarkerStyle{.size = 8.0f});
}

// Den inspelade matrisen (kolumnordning) på en punkt → målets px.
bool projicera(const float m[16], double x, double y, double& px, double& py)
{
    const double X = m[0] * x + m[4] * y + m[12];
    const double Y = m[1] * x + m[5] * y + m[13];
    const double W = m[3] * x + m[7] * y + m[15];
    if (!(W > 0.0))
        return false;
    px = X / W;
    py = Y / W;
    return true;
}

// ------------------------------------------------------------------- fallen

void fallK()
{
    Bänk b;
    b.yta.addShape(yta(10, 10, gfx::Color{255, 0, 0, 255}));
    b.yta.addShape(yta(60, 10, gfx::Color{0, 0, 255, 255}));
    b.yta.addShape(linje(10, 60));
    b.yta.addShape(linje(60, 60));
    b.yta.addShape(markör(20, 100));
    b.yta.addShape(markör(40, 100));
    b.frame();
    const auto strikt = b.program();
    check(strikt == std::vector<P>{P::Fill, P::Line, P::Fill, P::Line, P::Glyph},
          "K Strict: indexen är målarordningen, konturen och linjerna med "
          "samma stil delar anrop", namn(strikt));

    b.yta.setLayerDrawOrder(0, glaze::DrawOrder::ByKind);
    b.frame();
    const auto slag = b.program();
    check(slag == std::vector<P>{P::Fill, P::Line, P::Glyph},
          "K ByKind: alla ytor, sedan alla linjer, sedan markörerna", namn(slag));
}

void fallL()
{
    // Formerna i ytans mitt (de lokala px (200, 150)) — synliga vid varje
    // zoom, så att bara zoomintervallet avgör.
    Bänk b;
    b.yta.addShape(yta(180, 135, gfx::Color{255, 0, 0, 255}));
    const int över = b.yta.addLayer("Över");
    check(över == 1, "L addLayer ger nästa index", std::to_string(över));
    b.yta.addShape(över, markör(200, 150));
    b.frame();
    check(b.program() == std::vector<P>{P::Fill, P::Line, P::Glyph},
          "L lagren i ordning: lager 0 före lager 1", namn(b.program()));
    b.yta.setLayerVisible(över, false);
    b.frame();
    check(b.program() == std::vector<P>{P::Fill, P::Line},
          "L ett dolt lager ritas inte", namn(b.program()));
    b.yta.setLayerVisible(över, true);
    b.yta.setLayerZoomRange(över, 5.0, 0.0);
    b.frame();
    check(b.program() == std::vector<P>{P::Fill, P::Line},
          "L utanför zoomintervallet ritas lagret inte", namn(b.program()));
    b.yta.setZoom(6.0);
    b.frame();
    check(b.program() == std::vector<P>{P::Fill, P::Line, P::Glyph},
          "L innanför zoomintervallet ritas det", namn(b.program()));
}

void fallR()
{
    Bänk b;
    b.yta.setLayerDrawOrder(0, glaze::DrawOrder::ByKind);
    b.yta.addShape(yta(10, 10, gfx::Color{255, 0, 0, 255}));
    b.yta.addShape(linje(10, 60));
    b.yta.addShape(linje(100000, 0));   // långt utanför bild — egen ruta
    b.yta.addShape(markör(20, 100));
    b.yta.setGridSize(10.0);
    b.frame();
    check(b.r.stats().buffersCreated > 0, "R första ritningen laddar upp",
          std::to_string(b.r.stats().buffersCreated) + " buffertar");

    b.r.resetStats();
    b.yta.setCenter(30.0, 40.0);
    b.frame();
    b.yta.setZoom(2.0);
    b.frame();
    b.yta.setBearing(30.0f);
    b.frame();
    b.yta.setPitch(40.0f);
    b.frame();
    b.yta.setPitch(0.0f);
    b.yta.setBearing(0.0f);
    b.yta.setZoom(1.0);
    b.yta.setCenter(100015.0, 10.0);   // den som låg utanför bild
    b.frame();
    const gfx::RenderStats s = b.r.stats();
    check(s.bytesUploaded == 0 && s.buffersCreated == 0 && s.drawCalls > 0,
          "R kameraändringar laddar inte upp och skapar ingen buffert — inte "
          "heller när en chunk utanför bild panoreras in",
          std::to_string(s.bytesUploaded) + " byte, " +
              std::to_string(s.buffersCreated) + " buffertar, " +
              std::to_string(s.drawCalls) + " anrop");
    bool fjärranRitad = false;
    for (const Anrop& a : b.r.anrop)
        if (a.program == P::Line && a.indexCount == kLinjeIndex)
            fjärranRitad = true;
    check(fjärranRitad, "R den panorerade linjen ritas ur sin buffert");
}

// L3 — LINJERNA I RYMDEN: residenta som de plana.
bool ritadeLine3(const Bänk& b)
{
    for (const Anrop& a : b.r.anrop)
        if (a.program == P::Line3 && a.indexCount > 0)
            return true;
    return false;
}

void fallL3()
{
    Bänk b;
    b.yta.addShape(glaze::Shape::polyline3({{10, 10, 0}, {60, 10, 5}, {60, 40, 10}},
                                           glaze::Stroke{.width = 2.0f}));
    b.yta.addShape(glaze::Shape::line3({200, 150, 0}, {260, 150, 30},
                                       glaze::Stroke{.width = glaze::world(0.5f)}));
    b.frame();
    const std::uint64_t mål0 = b.res.targetRenders();
    check(ritadeLine3(b) && b.r.stats().buffersCreated > 0 && mål0 == 1,
          "L3 första ritningen laddar upp och ritar Line3 — genom 3D-målet",
          namn(b.program()) + ", " + std::to_string(mål0) + " målritningar");

    // KARTKAMERAN: mitt, zoom, bäring och tilt.
    b.r.resetStats();
    b.yta.setCenter(30.0, 40.0);
    b.frame();
    b.yta.setZoom(2.0);
    b.frame();
    b.yta.setBearing(30.0f);
    b.frame();
    b.yta.setPitch(40.0f);
    b.frame();
    const gfx::RenderStats s = b.r.stats();
    check(s.bytesUploaded == 0 && s.buffersCreated == 0 && ritadeLine3(b) &&
              b.res.targetRenders() == mål0 + 4,
          "L3 fyra kameraändringar: ingen uppladdning och ingen buffert, och "
          "målet ritas om varje gång",
          std::to_string(s.bytesUploaded) + " byte, " +
              std::to_string(s.buffersCreated) + " buffertar, " +
              std::to_string(b.res.targetRenders() - mål0) + " målritningar");

    // 3D-KAMERAN: en orbit i tre steg.
    b.yta.setCamera(glaze::Camera3D{.target = {35, 25, 5}, .distance = 120,
                                    .azimuth = 0, .elevation = 30});
    b.frame();
    b.r.resetStats();
    for (int i = 1; i <= 3; ++i) {
        b.yta.setCamera(glaze::Camera3D{.target    = {35, 25, 5},
                                        .distance  = 120.0 + 10.0 * i,
                                        .azimuth   = 20.0f * i,
                                        .elevation = 30.0f + 5.0f * i});
        b.frame();
    }
    const gfx::RenderStats s3 = b.r.stats();
    check(b.yta.is3D() && s3.bytesUploaded == 0 && s3.buffersCreated == 0 &&
              ritadeLine3(b),
          "L3 en orbit med Camera3D: ingen uppladdning och ingen buffert",
          std::to_string(s3.bytesUploaded) + " byte, " +
              std::to_string(s3.buffersCreated) + " buffertar");
}

void fallTB()
{
    // Fyra linjer först, så att EN borttagen (en femtedel) ligger under
    // kompakteringens fjärdedel.
    Bänk b;
    for (int i = 0; i < 4; ++i)
        b.yta.addShape(linje(10, 10 + 25.0 * i));
    b.frame();
    b.r.resetStats();
    b.yta.addShape(linje(10, 120));
    b.frame();
    check(b.r.stats().bytesUploaded == kLinjeByte &&
              b.r.stats().buffersCreated == 0,
          "T tillägget laddar upp bara den nya formen, i samma buffert",
          std::to_string(b.r.stats().bytesUploaded) + " byte (facit " +
              std::to_string(kLinjeByte) + "), " +
              std::to_string(b.r.stats().buffersCreated) + " nya buffertar");
    check(b.r.anrop.size() == 1 && b.r.anrop[0].indexCount == 5 * kLinjeIndex,
          "T de fem linjerna är ETT anrop",
          std::to_string(b.r.anrop.size()) + " anrop");

    b.r.resetStats();
    b.yta.removeShape(4);
    b.frame();
    check(b.r.stats().bytesUploaded == kLinjeIndex * 4 &&
              b.r.stats().buffersCreated == 0,
          "B borttagningen laddar upp bara formens index",
          std::to_string(b.r.stats().bytesUploaded) + " byte");
    // Formens intervall i indexbufferten: degenererat (alla lika) — och den
    // före den orörd (inte alla lika).
    bool degenererad = false;
    if (!b.r.anrop.empty()) {
        const auto* ib = static_cast<const Buffert*>(b.r.anrop[0].ib);
        std::vector<std::uint32_t> idx(5 * kLinjeIndex);
        std::memcpy(idx.data(), ib->data.data(), idx.size() * 4);
        const std::size_t borta = 4 * kLinjeIndex, kvar = 3 * kLinjeIndex;
        degenererad = true;
        for (std::size_t i = borta + 1; i < borta + kLinjeIndex; ++i)
            degenererad = degenererad && idx[i] == idx[borta];
        bool levande = false;
        for (std::size_t i = kvar + 1; i < kvar + kLinjeIndex; ++i)
            levande = levande || idx[i] != idx[kvar];
        degenererad = degenererad && levande;
    }
    check(degenererad && b.yta.shapeCount() == 4,
          "B den borttagna formens index är degenererade, grannen orörd");
}

void fallC()
{
    Bänk b;
    for (int i = 0; i < 8; ++i)
        b.yta.addShape(linje(10, 10 + 25.0 * i));
    b.frame();
    b.r.resetStats();
    b.yta.removeShape(0);   // 1/8 död
    b.frame();
    check(b.r.stats().buffersCreated == 0 &&
              b.r.stats().bytesUploaded == kLinjeIndex * 4,
          "C en åttondel död: ingen ombyggnad, bara indexintervallet",
          std::to_string(b.r.stats().buffersCreated) + " buffertar");
    b.r.resetStats();
    b.yta.removeShape(0);
    b.yta.removeShape(0);   // 3/8 döda — över en fjärdedel
    b.frame();
    std::uint32_t ritade = 0;
    for (const Anrop& a : b.r.anrop)
        ritade += a.indexCount;
    check(b.r.stats().buffersCreated >= 2 && ritade == 5 * kLinjeIndex,
          "C över en fjärdedel döda: lagret byggs om och bara de levande ritas",
          std::to_string(b.r.stats().buffersCreated) + " buffertar, " +
              std::to_string(ritade) + " index (facit " +
              std::to_string(5 * kLinjeIndex) + ")");
}

void fallG()
{
    Bänk b;
    b.yta.setLayerDrawOrder(0, glaze::DrawOrder::ByKind);
    b.yta.addShape(linje(10, 10));
    b.yta.addShape(linje(100000, 10));
    b.yta.setCenter(25.0, 20.0);
    b.frame();
    check(b.r.anrop.size() == 1,
          "G av två linjer långt isär ritas bara den som syns",
          std::to_string(b.r.anrop.size()) + " anrop");
}

void fallI()
{
    Bänk b;
    b.yta.addShape(linje(10, 10));
    b.yta.addShape(linje(10, 50));
    b.frame();
    b.r.resetStats();
    b.yta.setShape(0, linje(12, 14));   // samma antal punkter
    b.frame();
    check(b.r.stats().bytesUploaded == kLinjeByte &&
              b.r.stats().buffersCreated == 0,
          "I samma antal punkter skrivs på plats — bara formens intervall",
          std::to_string(b.r.stats().bytesUploaded) + " byte");
    b.r.resetStats();
    b.yta.setShape(0, glaze::Shape::polyline({{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                             glaze::Stroke{.width = 2.0f}));
    b.frame();
    check(b.r.stats().buffersCreated >= 2,
          "I fler punkter bygger om lagret",
          std::to_string(b.r.stats().buffersCreated) + " buffertar");
    check(b.r.anrop.size() == 1, "I ordningen och sammanslagningen står kvar",
          std::to_string(b.r.anrop.size()) + " anrop");
}

void fallN()
{
    Bänk b;
    b.yta.setGridSize(10.0);
    b.frame();
    check(b.r.anrop.size() == 1 && b.r.anrop[0].program == P::Line &&
              b.r.stats().buffersCreated == 2,
          "N rutnätet är ett Line-anrop ur en statisk mesh",
          namn(b.program()) + ", " +
              std::to_string(b.r.stats().buffersCreated) + " buffertar");
    const gfx::GpuBuffer* vb = b.r.anrop.empty() ? nullptr : b.r.anrop[0].vb;
    b.r.resetStats();
    b.yta.setCenter(1234.5, -77.0);
    b.yta.setZoom(3.0);
    b.frame();
    check(b.r.stats().bytesUploaded == 0 && b.r.anrop.size() == 1 &&
              b.r.anrop[0].vb == vb,
          "N kameran ritar samma mesh utan uppladdning",
          std::to_string(b.r.stats().bytesUploaded) + " byte");
}

void fallP()
{
    Bänk b;
    b.yta.setCenter(100.0, 100.0);   // en kamera: världen ≠ de lokala px
    b.yta.onPaint = [](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.drawPolyline({{0, 0}, {20, 0}}, {.width = 2.0f});
        e.painter.drawPolyline({{0, 10}, {20, 10}}, {.width = 2.0f});
        e.painter.fillRect({0, 20, 10, 10}, gfx::Color{255, 0, 0, 255});
    };
    b.yta.onPaintOverlay = [](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.drawLine({0, 0}, {10, 0}, {.width = 2.0f});
    };
    b.frame();
    check(b.program() == std::vector<P>{P::Line, P::Fill},
          "P två linjer i rad är ett anrop, ytan ett — och HUD:en INTE i "
          "ritytans egen ritning",
          namn(b.program()));
    check(b.yta.hasOverlay(), "P en onPaintOverlay ger ritytan ett overlay-lager");
    // Overlay-lagret, som fönstret ritar det vid presenten.
    b.yta.drawOverlay(b.c);
    check(b.program() == std::vector<P>{P::Line, P::Fill, P::Line},
          "P HUD:en ritas i overlay-lagret, ett anrop", namn(b.program()));
    check(b.r.stats().buffersCreated == 0,
          "P målaren skapar inga buffertar — den transienta ringen",
          std::to_string(b.r.stats().buffersCreated));
    if (b.r.anrop.size() == 3) {
        // Världen: (0, 0) hamnar i ytans mitt minus kamerans mitt (zoom 1).
        double wx = 0.0, wy = 0.0, sx = 0.0, sy = 0.0;
        const bool w = projicera(b.r.anrop[0].viewProj, 0.0, 0.0, wx, wy);
        const bool s = projicera(b.r.anrop[2].viewProj, 0.0, 0.0, sx, sy);
        const double mx = b.c.box.x + b.c.box.w / 2.0 - 100.0;
        const double my = b.c.box.y + b.c.box.h / 2.0 - 100.0;
        check(w && std::abs(wx - mx) < 0.01 && std::abs(wy - my) < 0.01,
              "P onPaint ritar i världen — kameran gäller",
              std::to_string(wx) + ", " + std::to_string(wy));
        check(s && std::abs(sx - b.c.box.x) < 0.01 &&
                  std::abs(sy - b.c.box.y) < 0.01,
              "P onPaintOverlay ritar i de lokala px — (0, 0) i ytans hörn",
              std::to_string(sx) + ", " + std::to_string(sy));
    }
}

void fallV()
{
    Bänk b;
    b.yta.addShape(yta(10, 10, gfx::Color{255, 0, 0, 255}));
    b.yta.setGridSize(10.0);
    b.yta.onPaint = [](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.drawLine({0, 0}, {10, 0});
    };
    b.yta.disable();
    b.frame();
    const float ton = 1.0f - b.c.style->iconFade;
    bool alla = !b.r.anrop.empty();
    for (const Anrop& a : b.r.anrop)
        alla = alla && std::abs(a.opacity - ton) < 1e-6f;
    check(alla, "V inaktiverad: rutnät, lager och målare ritas med 1 − iconFade",
          std::to_string(b.r.anrop.size()) + " anrop");
}

void fallO()
{
    Bänk b;
    b.yta.addShape(linje(10, 10));
    check(b.yta.addShape(7, linje(0, 0)) == -1 && b.yta.layerCount() == 1,
          "O addShape i ett lager som inte finns ger −1");
    b.yta.setShape(0, 5, linje(0, 0));
    b.yta.removeShape(0, 5);
    b.yta.removeLayer(3);
    check(b.yta.shapeCount(0) == 1 && b.yta.layerCount() == 1,
          "O setShape/removeShape/removeLayer utanför ändrar ingenting");
    const int a = b.yta.addLayer("A");
    b.yta.addShape(a, linje(0, 0));
    b.yta.setLayer(a, "B");
    check(b.yta.shapeCount(a) == 0 && b.yta.layerName(a) == "B",
          "O setLayer ersätter lagret helt (nytt namn, inga former)");
    b.yta.setLayers({"x", "y", "z"});
    check(b.yta.layerCount() == 3 && b.yta.shapeCount(0) == 0 &&
              b.yta.layerName(2) == "z",
          "O setLayers fyller om alla lager");
    b.yta.addShape(linje(1, 2));
    const glaze::Shape* s = b.yta.shape(0, 0);
    check(s && s->kind() == glaze::Shape::Kind::Lines && !b.yta.shape(0, 1),
          "O shape(lager, index) läser formen, nullptr utanför");
}

void fallY()
{
    Bänk b;
    // Två etiketter i lager 0 och en i lager 1 — tre etiketter i två
    // körningar, alla över varandra.
    b.yta.addShape(glaze::Shape::text({200, 150}, "Storgatan"));
    b.yta.addShape(glaze::Shape::text({205, 152}, "Kyrkvägen"));
    const int över = b.yta.addLayer("över");
    b.yta.addShape(över, glaze::Shape::text({210, 154}, "Torget"));
    b.frame();
    std::vector<const Anrop*> glyf;
    for (const Anrop& a : b.r.anrop)
        if (a.program == P::Glyph)
            glyf.push_back(&a);
    check(glyf.size() == 2 && glyf[0]->labels && glyf[0]->labels == glyf[1]->labels &&
              glyf[0]->labelBase == 0 && glyf[1]->labelBase == 2,
          "Y en synlighetsbild för ritytan; det andra lagrets körning börjar "
          "efter det förstas två etiketter",
          std::to_string(glyf.size()) + " anrop, baser " +
              (glyf.size() == 2 ? std::to_string(glyf[0]->labelBase) + "/" +
                                      std::to_string(glyf[1]->labelBase)
                                : std::string("—")));
    const std::uint64_t p1 = b.res.labelPlacements();
    b.r.resetStats();
    b.frame();
    check(b.res.labelPlacements() == p1 && b.r.stats().bytesUploaded == 0,
          "Y samma kamera: ingen ny placering, ingen uppladdning",
          std::to_string(b.res.labelPlacements() - p1));
    b.r.resetStats();
    b.yta.setCenter(190.0, 140.0);
    b.yta.setBearing(30.0f);
    b.frame();
    check(b.res.labelPlacements() == p1 + 1 && b.r.stats().bytesUploaded <= 3,
          "Y en kameraändring placerar om och laddar upp högst en byte per "
          "etikett",
          std::to_string(b.r.stats().bytesUploaded) + " byte");

    Bänk fri;
    fri.yta.addShape(glaze::Shape::text({200, 150}, "Ensam",
                                        glaze::TextStyle{.collide = false}));
    fri.frame();
    bool utan = !fri.r.anrop.empty();
    for (const Anrop& a : fri.r.anrop)
        utan = utan && a.labels == nullptr;
    check(utan, "Y utan kolliderande etiketter: ingen synlighetsbild");
}

void fallX()
{
    Bänk b;
    b.yta.addShape(glaze::Shape::text({50, 50}, "Storgatan",
                                      glaze::TextStyle{.size = glaze::mm(2.5f)}));
    b.frame();
    check(b.program() == std::vector<P>{P::Glyph} && b.res.glyphBuilds() == 1,
          "X texten byggs mot fönstrets atlas och ritas",
          namn(b.program()) + ", " + std::to_string(b.res.glyphBuilds()) + " bygg");
    check(!b.r.anrop.empty() && std::abs(b.r.anrop[0].pxPerWidthUnit - 4.0f) < 1e-6f,
          "X storleken i mm: anropets enhet är skärmens px per mm",
          b.r.anrop.empty() ? "" : std::to_string(b.r.anrop[0].pxPerWidthUnit));
    b.r.resetStats();
    b.yta.setCenter(10.0, 10.0);
    b.yta.setBearing(45.0f);
    b.frame();
    check(b.r.stats().bytesUploaded == 0 && b.res.glyphBuilds() == 1,
          "X kameran bygger inte om texten och laddar inte upp",
          std::to_string(b.r.stats().bytesUploaded) + " byte");
    b.yta.setShape(0, glaze::Shape::text({50, 50}, "Kyrkvägen",
                                         glaze::TextStyle{.size = glaze::mm(2.5f)}));
    b.frame();
    check(b.res.glyphBuilds() == 2, "X en ändrad text byggs om",
          std::to_string(b.res.glyphBuilds()));
}

// Glyf-anropets vertex, ur den buffert anropet band (lagrets statiska eller
// målarens transienta — båda håller sina byte här).
std::vector<gfx::GlyphVertex> glyfVertex(const Bänk& b)
{
    for (const Anrop& a : b.r.anrop)
        if (a.program == P::Glyph && a.vb) {
            const auto& buf = static_cast<const Buffert&>(*a.vb);
            std::vector<gfx::GlyphVertex> v(buf.data.size() / sizeof(gfx::GlyphVertex));
            std::memcpy(v.data(), buf.data.data(), v.size() * sizeof(gfx::GlyphVertex));
            return v;
        }
    return {};
}

// TD — TEXTDELARNA och BILDEN PÅ FYRA HÖRN i målaren:
// `drawText(delar)` ger exakt lagrets vertex för `Shape::text(delar)` — samma
// färger per del, samma fet —, `measureText(delar)` är lagrets etikettlåda
// (plattans quad, luften 0), och en fyrhörning som inte är strikt konvex ger
// inget Image-anrop medan en giltig ger ett.
void fallTD()
{
    const std::vector<glaze::TextSpan> delar = {
        glaze::TextSpan{.text = "Stor"},
        {.text = "gatan", .color = gfx::Color{200, 0, 0, 255}, .bold = true}};
    const glaze::TextStyle stil{.size       = 20.0f,
                                .background = gfx::Color{255, 255, 0, 255},
                                .collide    = false};

    Bänk lager;
    lager.yta.addShape(glaze::Shape::text({100, 80}, delar, stil));
    lager.frame();
    const std::vector<gfx::GlyphVertex> vl = glyfVertex(lager);

    Bänk       målare;
    gfx::Sizef mått{};
    målare.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        e.painter.drawText({100, 80}, delar, stil);
        mått = e.painter.measureText(delar, stil);
    };
    målare.frame();
    const std::vector<gfx::GlyphVertex> vm = glyfVertex(målare);

    const bool samma = !vl.empty() && vl.size() == vm.size() &&
                       std::memcmp(vl.data(), vm.data(),
                                   vl.size() * sizeof(gfx::GlyphVertex)) == 0;
    bool röd = false;
    for (const gfx::GlyphVertex& v : vl)
        röd = röd || (v.color.r == 200 && v.color.g == 0);
    check(samma && röd,
          "TD drawText(delar) ger exakt lagrets vertex för Shape::text(delar) "
          "— och den andra delens färg finns där",
          std::to_string(vl.size()) + " / " + std::to_string(vm.size()) + " vertex");

    // Plattans quad (luften 0) är etikettens låda i em; storleken 20 px och
    // skalan 1 gör den till logiska px.
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const gfx::GlyphVertex& v : vl)
        if ((v.flags & gfx::glyphflag::kShapeMask) ==
            static_cast<std::uint32_t>(gfx::GlyphShape::Plate)) {
            x0 = std::min(x0, v.ox);
            x1 = std::max(x1, v.ox);
            y0 = std::min(y0, v.oy);
            y1 = std::max(y1, v.oy);
        }
    check(x1 > x0 && std::abs(mått.w - (x1 - x0) * 20.0f) < 1e-3f &&
              std::abs(mått.h - (y1 - y0) * 20.0f) < 1e-3f,
          "TD measureText(delar) = lagrets etikettlåda",
          std::to_string(mått.w) + " × " + std::to_string(mått.h) + " mot " +
              std::to_string((x1 - x0) * 20.0f) + " × " +
              std::to_string((y1 - y0) * 20.0f));

    // Fyrhörningen: en fluga ritas inte, en trapets ritas (ett Image-anrop).
    Bänk          bild;
    glaze::Bitmap pixlar(4, 4);
    bool          trapets = false;
    bild.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        if (trapets)
            e.painter.drawImage({0, 0}, {40, 0}, {30, 20}, {10, 20}, pixlar);
        else
            e.painter.drawImage({0, 0}, {40, 20}, {40, 0}, {0, 20}, pixlar);
    };
    const auto bildAnrop = [&] {
        int n = 0;
        for (const Anrop& a : bild.r.anrop)
            n += a.program == P::Image ? 1 : 0;
        return n;
    };
    bild.frame();
    const int fluga = bildAnrop();
    trapets = true;
    bild.frame();
    check(fluga == 0 && bildAnrop() == 1,
          "TD en fyrhörning som inte är strikt konvex ger inget Image-anrop, en "
          "trapets ett",
          std::to_string(fluga) + " / " + std::to_string(bildAnrop()));
}

// LO — LAGRENS ORDNING (`Canvas::moveLayer`). Förväntat värde är
// buffertarna FÖRE flytten: samma pekare i den nya ordningen, och inga nya
// buffertar eller byte — det enda som får laddas upp är etiketternas
// synlighet (en byte per etikett).

using Buffertar = std::vector<const gfx::GpuBuffer*>;

// Anropens vertexbuffertar i ritordning — alla, eller de i målet `mål` som
// finns i `bland`.
Buffertar buffertar(const Bänk& b)
{
    Buffertar ut;
    for (const Anrop& a : b.r.anrop)
        ut.push_back(a.vb);
    return ut;
}

Buffertar buffertar(const Bänk& b, const Buffertar& bland,
                    const gfx::RenderTarget* mål)
{
    Buffertar ut;
    for (const Anrop& a : b.r.anrop)
        if (a.mål == mål && std::find(bland.begin(), bland.end(), a.vb) != bland.end())
            ut.push_back(a.vb);
    return ut;
}

// Programmet `p`:s anrop — deras buffertar, i ritordning.
Buffertar buffertar(const Bänk& b, P p)
{
    Buffertar ut;
    for (const Anrop& a : b.r.anrop)
        if (a.program == p)
            ut.push_back(a.vb);
    return ut;
}

// Målen anropen ritades i, i den ordning de först användes.
std::vector<const gfx::RenderTarget*> målen(const Bänk& b)
{
    std::vector<const gfx::RenderTarget*> ut;
    for (const Anrop& a : b.r.anrop)
        if (std::find(ut.begin(), ut.end(), a.mål) == ut.end())
            ut.push_back(a.mål);
    return ut;
}

std::string uppladdat(const Bänk& b)
{
    return std::to_string(b.r.stats().bytesUploaded) + " byte, " +
           std::to_string(b.r.stats().buffersCreated) + " buffertar";
}

bool inget(const Bänk& b)
{
    return b.r.stats().bytesUploaded == 0 && b.r.stats().buffersCreated == 0;
}

void fallLO()
{
    // ---- LO1. Tre lager — ytan (Fill, Line), linjen, texten — flyttas och
    // tillbaka. Texten kolliderar inte, så ingen synlighetsbild finns.
    {
        Bänk b;
        b.yta.setLayers({"Ytor", "Linjer", "Text"});
        b.yta.addShape(0, yta(10, 10, gfx::Color{255, 0, 0, 255}));
        b.yta.addShape(1, linje(10, 60));
        b.yta.addShape(2, glaze::Shape::text({200, 150}, "Storgatan",
                                             glaze::TextStyle{.collide = false}));
        b.frame();
        const auto      före = b.program();
        const Buffertar vb   = buffertar(b);
        check(före == std::vector<P>{P::Fill, P::Line, P::Line, P::Glyph},
              "LO1 lagren i ordning: ytan (Fill, Line), linjen, texten",
              namn(före));
        if (vb.size() != 4)
            return;

        b.r.resetStats();
        const bool flyttad = b.yta.moveLayer(2, 0);
        b.frame();
        check(flyttad &&
                  b.program() == std::vector<P>{P::Glyph, P::Fill, P::Line, P::Line} &&
                  buffertar(b) == Buffertar{vb[3], vb[0], vb[1], vb[2]} && inget(b) &&
                  b.yta.layerName(0) == "Text" && b.yta.layerName(1) == "Ytor",
              "LO1 moveLayer(2, 0): texten underst med SAMMA buffertar — inget "
              "byggs om och inget laddas upp",
              namn(b.program()) + ", " + uppladdat(b));

        b.r.resetStats();
        const bool tillbaka = b.yta.moveLayer(0, 3);
        b.frame();
        check(tillbaka && b.program() == före && buffertar(b) == vb && inget(b) &&
                  b.yta.layerName(2) == "Text",
              "LO1 moveLayer(0, 3): tillbaka i den ursprungliga ordningen, 0 byte",
              namn(b.program()) + ", " + uppladdat(b));

        const bool kvar = b.yta.moveLayer(1, 2) && b.yta.moveLayer(1, 1);
        b.frame();
        check(kvar && b.yta.layerName(1) == "Linjer" && buffertar(b) == vb,
              "LO1 moveLayer(1, 2) och (1, 1) — till samma plats: true, ordningen "
              "står kvar");
        const bool avslag = !b.yta.moveLayer(3, 0) && !b.yta.moveLayer(0, 4) &&
                            !b.yta.moveLayer(-1, 0) && !b.yta.moveLayer(0, -1);
        check(avslag && b.yta.layerName(0) == "Ytor" &&
                  b.yta.layerName(1) == "Linjer" && b.yta.layerName(2) == "Text",
              "LO1 utanför (lager 3, plats 4, negativa) — false och ingenting "
              "ändras");
    }

    // ---- LO2. LAGRETS EGENSKAPER följer lagret: ritordningen, synligheten,
    // opaciteten, kollisionen, placeringen, vyerna och formerna — och ett
    // dolt lager förblir dolt (och oladdat).
    {
        Bänk b;
        b.yta.setLayers({"A", "B", "C"});
        b.yta.addShape(0, yta(10, 10, gfx::Color{255, 0, 0, 255}));
        b.yta.addShape(0, yta(60, 10, gfx::Color{0, 0, 255, 255}));
        b.yta.setLayerDrawOrder(0, glaze::DrawOrder::ByKind);
        b.yta.addShape(1, linje(10, 60));
        b.yta.setLayerVisible(1, false);
        b.yta.addShape(2, markör(20, 100));
        b.yta.setLayerOpacity(2, 0.5f);
        b.yta.setLayerCollision(2, false);
        b.yta.setLayerPlacement(2, glaze::LayerPlacement::Ground);
        b.frame();
        check(b.program() == std::vector<P>{P::Fill, P::Line, P::Glyph},
              "LO2 (fixturen) ByKind-lagret, det dolda lagret och markören",
              namn(b.program()));
        b.r.resetStats();
        b.yta.moveLayer(0, 3);   // B, C, A
        b.frame();
        float opMarkör = -1.0f;
        for (const Anrop& a : b.r.anrop)
            if (a.program == P::Glyph)
                opMarkör = a.opacity;
        check(b.yta.layerName(2) == "A" &&
                  b.yta.layerDrawOrder(2) == glaze::DrawOrder::ByKind &&
                  b.yta.shapeCount(2) == 2 && b.yta.layerName(0) == "B" &&
                  !b.yta.layerVisible(0) &&
                  b.yta.layerDrawOrder(0) == glaze::DrawOrder::Strict &&
                  b.yta.layerName(1) == "C" && b.yta.layerOpacity(1) == 0.5f &&
                  !b.yta.layerCollision(1) &&
                  b.yta.layerPlacement(1) == glaze::LayerPlacement::Ground &&
                  b.yta.layerPlacement(2) == glaze::LayerPlacement::Auto &&
                  b.yta.layerViews(1) == std::vector<int>{0},
              "LO2 egenskaperna följer lagret till dess nya plats");
        check(b.program() == std::vector<P>{P::Glyph, P::Fill, P::Line} &&
                  opMarkör == 0.5f && inget(b),
              "LO2 det dolda lagret ritas inte, markören (opacitet 0,5) under "
              "ByKind-lagret — 0 byte",
              namn(b.program()) + ", " + uppladdat(b));
    }

    // ---- LO3. TVÅ VYER (huvudvyn och en del-vy, `setLayerViews`) i
    // 3D-målet: båda ritar lagren i den nya ordningen, ur samma buffertar.
    // Markdjupet (ett Fill-anrop efter marken) hör inte till lagren och
    // sållas bort genom buffertarna.
    {
        Bänk b;
        b.yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
        b.yta.setLayers({"Röd", "Blå", "Linje"});
        b.yta.addShape(0, glaze::Shape::rect({-40, -30, 30, 30},
                                             gfx::Color{255, 0, 0, 255}));
        b.yta.addShape(1, glaze::Shape::rect({-20, -20, 30, 30},
                                             gfx::Color{0, 0, 255, 255}));
        b.yta.addShape(2, linje(-30, 0));
        const int vy = b.yta.addView(glaze::View{
            .width  = 160,
            .height = 120,
            .camera = glaze::Camera3D{.target = {0, 0, 0}, .distance = 400,
                                      .elevation = 90}});
        for (int i = 0; i < 3; ++i)
            b.yta.setLayerViews(i, {0, vy});
        b.frame();
        const auto mål = målen(b);
        Buffertar  lagren;
        for (const Anrop& a : b.r.anrop)
            if (!mål.empty() && a.mål == mål.front() && lagren.size() < 3)
                lagren.push_back(a.vb);
        const bool fixtur = vy == 1 && mål.size() == 2 && lagren.size() == 3 &&
                            buffertar(b, lagren, mål[1]) == lagren;
        check(fixtur,
              "LO3 (fixturen) två mål — huvudvyns och del-vyns — med lagrens tre "
              "anrop i ordning i båda",
              std::to_string(mål.size()) + " mål");
        if (!fixtur)
            return;
        b.r.resetStats();
        b.yta.moveLayer(2, 0);
        b.frame();
        const auto      efter = målen(b);
        const Buffertar ny{lagren[2], lagren[0], lagren[1]};
        check(efter.size() == 2 && buffertar(b, lagren, efter[0]) == ny &&
                  buffertar(b, lagren, efter[1]) == ny && inget(b) &&
                  b.yta.layerViews(0) == std::vector<int>{0, 1},
              "LO3 båda vyerna ritar linjen underst, ur samma buffertar, och "
              "vyerna följer lagret — 0 byte",
              uppladdat(b));
    }

    // ---- LO4. WORLD-LAGREN OCH FÖREMÅLEN (3D-målet): två World-lager med en
    // linje i rymden var byter ordning i ritningen, och instanserna i ett
    // tredje förblir residenta — inga nya kopior, buffertar eller byte.
    // (Meshernas program kompileras inte av renderaren här, så instanserna
    // ritas inte — residensen gör det ändå.)
    {
        Bänk b;
        b.yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 30.0f});
        b.yta.setLayers({"Mark", "Stolpar", "Ledning", "Träd"});
        b.yta.addShape(0, glaze::Shape::rect({-40, -30, 80, 60},
                                             gfx::Color{200, 220, 200, 255}));
        b.yta.addShape(1, glaze::Shape::line3({-30, 0, 0}, {-30, 0, 20},
                                              glaze::Stroke{.width = 2.0f}));
        b.yta.addShape(2, glaze::Shape::line3({30, 0, 0}, {30, 0, 20},
                                              glaze::Stroke{.width = 3.0f}));
        b.yta.addShape(3, glaze::Shape::instances(
                              glaze::Shape::box(glaze::Box3{{0, 0, 0}, {1, 1, 1}}),
                              {glaze::Instance{.at = {0, 10, 0}},
                               glaze::Instance{.at = {5, 10, 0}}}));
        b.frame();
        // Den andra framen MÄTS, och 3D-målet ritas om bara när ritytans
        // `damageStamp()` ändrats (target3d.cpp): utan
        // invalideringen hade den kompositerat det cachade målet utan ett
        // enda ritanrop — fixturen hade sett 0 Line3.
        b.yta.invalidate();
        b.frame();
        const Buffertar     linjer = buffertar(b, P::Line3);
        const std::uint64_t kopior = b.res.objects().copiesMade();
        const std::size_t   mängder = b.res.objects().instanceSetCount();
        check(linjer.size() == 2 && kopior > 0 && mängder == 1,
              "LO4 (fixturen) två Line3-anrop och instanserna residenta",
              std::to_string(linjer.size()) + " Line3, " + std::to_string(kopior) +
                  " kopior, " + std::to_string(mängder) + " mängder · " +
                  namn(b.program()));   // anropen i detaljen: ett tomt mål syns
        if (linjer.size() != 2)
            return;
        b.r.resetStats();
        b.yta.moveLayer(2, 1);   // Mark, Ledning, Stolpar, Träd
        b.frame();
        check(buffertar(b, P::Line3) == Buffertar{linjer[1], linjer[0]} &&
                  b.res.objects().copiesMade() == kopior &&
                  b.res.objects().instanceSetCount() == mängder && inget(b) &&
                  b.yta.layerPlacement(1) == glaze::LayerPlacement::Auto,
              "LO4 World-lagren i den nya ordningen; instanserna utan ny kopia — "
              "0 byte",
              uppladdat(b));
    }

    // ---- LO5. ETIKETTERNA: två krockande etiketter i två lager. Flytten
    // placerar om dem (indexen betyder något annat) och laddar upp högst en
    // byte per etikett — och Glyph-anropens bas följer den nya ordningen.
    {
        Bänk b;
        b.yta.setLayers({"Undre", "Övre"});
        b.yta.addShape(0, glaze::Shape::text({200, 150}, "Storgatan"));
        b.yta.addShape(1, glaze::Shape::text({205, 152}, "Torget"));
        b.frame();
        const Buffertar glyf = buffertar(b, P::Glyph);
        bool            baser = glyf.size() == 2;
        for (const Anrop& a : b.r.anrop)
            if (a.program == P::Glyph)
                baser = baser && a.labels && a.labelBase == (a.vb == glyf[0] ? 0u : 1u);
        check(baser, "LO5 (fixturen) två etikettkörningar med baserna 0 och 1");
        if (!baser)
            return;
        b.r.resetStats();
        b.yta.moveLayer(1, 0);
        b.frame();
        bool nyaBaser = buffertar(b, P::Glyph) == Buffertar{glyf[1], glyf[0]};
        for (const Anrop& a : b.r.anrop)
            if (a.program == P::Glyph)
                nyaBaser = nyaBaser && a.labelBase == (a.vb == glyf[1] ? 0u : 1u);
        check(nyaBaser && b.r.stats().buffersCreated == 0 &&
                  b.r.stats().bytesUploaded <= 2,
              "LO5 efter flytten: Torget först med bas 0, inga buffertar och högst "
              "en byte per etikett",
              uppladdat(b));
    }

    // ---- LO6. En PÅGÅENDE etikettoning stannar vid flytten: indexen betyder
    // något annat, så synligheten får målet direkt och ritytan slutar be om
    // frames (regel 1).
    {
        Bänk b;
        b.yta.setLabelFade(true);
        b.yta.setLayers({"Vänster", "Höger"});
        b.yta.addShape(0, glaze::Shape::text({180, 150}, "HHHHHH",
                                             glaze::TextStyle{.size = 22.0f}));
        b.yta.addShape(1, glaze::Shape::text({220, 150}, "HHHHHH",
                                             glaze::TextStyle{.size = 22.0f}));
        b.frame();   // sida vid sida krockar de — det övre lagret vinner
        b.yta.setBearing(90.0f);   // ankarna över varandra — den dolda visas
        b.frame();
        const bool tonar = b.yta.isAnimating();
        b.r.resetStats();
        b.yta.moveLayer(1, 0);
        b.frame();
        check(tonar && !b.yta.isAnimating() && b.r.stats().buffersCreated == 0 &&
                  b.r.stats().bytesUploaded <= 2,
              "LO6 en pågående toning stannar vid flytten — ingen buffert, högst en "
              "byte per etikett",
              std::string(tonar ? "tonade" : "tonade INTE") + ", " + uppladdat(b));
    }

    // ---- LO7. TVÅ TERRAIN-LAGER byter ordning: höjden är den
    // översta ytan oavsett ordning, så terrängens version står kvar och
    // etiketten vid markhöjd (ett Glyph3-bygge) byggs inte om.
    {
        Bänk b;
        b.yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 30.0f});
        b.yta.setLayers({"Etiketter", "Mark A", "Mark B"});
        b.yta.addShape(0, glaze::Shape::text({0, 0}, "Toppen",
                                             glaze::TextStyle{.collide = false}));
        b.yta.addShape(1, glaze::Shape::box(glaze::Box3{{-50, -50, 0}, {50, 50, 2}}));
        b.yta.addShape(2, glaze::Shape::box(glaze::Box3{{-40, -40, 0}, {40, 40, 5}}));
        b.yta.setLayerPlacement(1, glaze::LayerPlacement::Terrain);
        b.yta.setLayerPlacement(2, glaze::LayerPlacement::Terrain);
        b.frame();
        b.yta.invalidate();   // den mätta framen ritar 3D-målet igen (som LO4)
        b.frame();
        const std::uint64_t byggen   = b.res.glyphBuilds();
        const bool          påTerräng = !buffertar(b, P::Glyph3).empty();
        check(påTerräng, "LO7 (fixturen) etiketten byggs vid markhöjd (Glyph3)");
        b.r.resetStats();
        const bool flyttad = b.yta.moveLayer(2, 1);
        b.frame();
        check(flyttad && påTerräng && b.res.glyphBuilds() == byggen && inget(b) &&
                  !buffertar(b, P::Glyph3).empty(),
              "LO7 Terrain-lagren byter ordning: etiketten byggs inte om — 0 byte",
              std::to_string(b.res.glyphBuilds() - byggen) + " nya bygg, " +
                  uppladdat(b));
    }
}

} // namespace

// ------------------------------------------------- (GT, ZV, MB)

std::vector<const Anrop*> glyfAnrop(const Bänk& b)
{
    std::vector<const Anrop*> ut;
    for (const Anrop& a : b.r.anrop)
        if (a.program == P::Glyph)
            ut.push_back(&a);
    return ut;
}

// Etikettens synlighet i anropets bild (rad för rad, `labelBase + label`) —
// −1 utan bild.
int texel(const Anrop* a, std::uint32_t label)
{
    const auto* bild = a ? dynamic_cast<const Bild*>(a->labels) : nullptr;
    if (!bild)
        return -1;
    const std::size_t t = static_cast<std::size_t>(a->labelBase) + label;
    return t < bild->px.size() ? bild->px[t] : -1;
}

// GT — GOLVET OCH TAKET: anropets uniformer och det placeringen och
// gallringen räknar med.
void fallGT()
{
    // GT1 — uniformerna = gränserna i fysiska px: px × skalan 1, mm × 4 px/mm,
    // world × kamerans 2 px per enhet (zoom 2). Tre olika par: tre anrop.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.setZoom(2.0);
        const auto text = [&](double x, glaze::Length mn, glaze::Length mx) {
            b.yta.addShape(glaze::Shape::text(
                {x, 150}, "A",
                glaze::TextStyle{.collide = false, .minSize = mn, .maxSize = mx}));
        };
        text(150, glaze::px(10), glaze::px(30));
        text(200, glaze::mm(2), glaze::mm(5));
        text(250, glaze::world(3), glaze::world(6));
        b.frame();
        const auto  g     = glyfAnrop(b);
        const float f[][2] = {{10, 30}, {8, 20}, {6, 12}};
        bool        ok    = g.size() == 3;
        std::string fick;
        for (std::size_t i = 0; i < g.size(); ++i) {
            fick += std::to_string(g[i]->glyphMinPx) + "/" +
                    std::to_string(g[i]->glyphMaxPx) + " ";
            ok = ok && i < 3 && std::abs(g[i]->glyphMinPx - f[i][0]) < 1e-4f &&
                 std::abs(g[i]->glyphMaxPx - f[i][1]) < 1e-4f;
        }
        check(ok, "GT1 golvet och taket i px, mm och world blir anropets fysiska "
                  "px — ett anrop per par",
              fick);
    }
    // GT2 — samma gränser delar körning, olika gör det inte.
    {
        Bänk b;
        const auto stil = [](float tak) {
            return glaze::TextStyle{.collide = false, .minSize = glaze::px(10),
                                    .maxSize = glaze::px(tak)};
        };
        b.yta.addShape(glaze::Shape::text({100, 100}, "A", stil(30)));
        b.yta.addShape(glaze::Shape::text({150, 100}, "B", stil(30)));
        b.frame();
        const std::size_t samma = glyfAnrop(b).size();
        b.yta.setShape(1, glaze::Shape::text({150, 100}, "B", stil(40)));
        b.frame();
        check(samma == 1 && glyfAnrop(b).size() == 2,
              "GT2 samma gränser: ett Glyph-anrop; ett annat tak: två",
              std::to_string(samma) + " / " + std::to_string(glyfAnrop(b).size()));
    }
    // GT3 — två världsetiketter 4 px isär (em 2 px vid zoom 1) krockar inte
    // råa, men vid golvet 20 px gör de det: en av dem döljs.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        const auto par = [&](glaze::Length golv) {
            b.yta.setShapes(
                {glaze::Shape::text({200, 150}, "AAAA",
                                    glaze::TextStyle{.size    = glaze::world(2),
                                                     .minSize = golv}),
                 glaze::Shape::text({200, 154}, "AAAA",
                                    glaze::TextStyle{.size    = glaze::world(2),
                                                     .minSize = golv})});
            b.frame();
            const auto g = glyfAnrop(b);
            return std::pair{texel(g.empty() ? nullptr : g[0], 0),
                             texel(g.empty() ? nullptr : g[0], 1)};
        };
        const auto råa  = par(glaze::Length{});
        const auto golv = par(glaze::px(20));
        check(råa.first == 255 && råa.second == 255 &&
                  ((golv.first == 0) != (golv.second == 0)),
              "GT3 råa krockar de inte; vid golvet krockar de och en döljs",
              std::to_string(råa.first) + "/" + std::to_string(råa.second) + " → " +
                  std::to_string(golv.first) + "/" + std::to_string(golv.second));
    }
    // GT4 — en etikett 15 px utanför ytans vänsterkant, rå 2 px och golvet
    // 40 px: den når in i ytan och ritas (gallringens marginal i em).
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.addShape(glaze::Shape::text(
            {-15.0, 150.0}, "AAAA",
            glaze::TextStyle{.size    = glaze::world(2),
                             .collide = false,
                             .minSize = glaze::px(40)}));
        b.frame();
        check(glyfAnrop(b).size() == 1,
              "GT4 en etikett utanför ytan som golvet gjort stor gallras inte",
              std::to_string(glyfAnrop(b).size()) + " anrop");
    }
}

// ZV — SYNLIGHETEN EFTER ZOOM genom synlighetsbilden och målaren.
void fallZV()
{
    // ZV1 och ZV3 — en etikett UTAN kollision men med intervallet [2, ∞) får
    // en synlighetsbild: texeln 0 vid zoom 1 och 255 vid zoom 3.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.addShape(glaze::Shape::text(
            {200, 150}, "Skolan", glaze::TextStyle{.collide = false, .minZoom = 2.0}));
        b.frame();
        auto      g    = glyfAnrop(b);
        const int dold = texel(g.empty() ? nullptr : g[0], 0);
        b.yta.setZoom(3.0);
        b.frame();
        g                = glyfAnrop(b);
        const int synlig = texel(g.empty() ? nullptr : g[0], 0);
        check(dold == 0 && synlig == 255,
              "ZV1/ZV3 utanför intervallet är texeln 0, innanför 255 — också utan "
              "kollision",
              std::to_string(dold) + " / " + std::to_string(synlig));
    }
    // ZV2 — den dolda etiketten (prioritet 10, intervallet [2, ∞)) tränger
    // inte undan sin granne vid zoom 1.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.addShape(glaze::Shape::text(
            {200, 150}, "Torget", glaze::TextStyle{.priority = 10, .minZoom = 2.0}));
        b.yta.addShape(glaze::Shape::text({200, 152}, "Posten"));
        b.frame();
        const auto g  = glyfAnrop(b);
        const int  t0 = texel(g.empty() ? nullptr : g[0], 0);
        const int  t1 = texel(g.empty() ? nullptr : g[0], 1);
        check(t0 == 0 && t1 == 255,
              "ZV2 den dolda etiketten tar ingen plats — grannen syns",
              std::to_string(t0) + "/" + std::to_string(t1));
    }
    // ZV4 — målaren ritar inte texten utanför intervallet, men innanför.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.onPaint = [](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.drawText({200, 150}, "Skolan",
                               glaze::TextStyle{.minZoom = 2.0});
        };
        b.frame();
        const std::size_t utanför = glyfAnrop(b).size();
        b.yta.setZoom(3.0);
        b.frame();
        check(utanför == 0 && glyfAnrop(b).size() == 1,
              "ZV4 målaren hoppar över texten utanför intervallet",
              std::to_string(utanför) + " / " + std::to_string(glyfAnrop(b).size()));
    }
    // ZV5 — med toning ger tröskelpassagen ett mellanvärde i första framen.
    {
        Bänk b;
        b.yta.setCenter(200.0, 150.0);
        b.yta.setZoom(3.0);
        b.yta.setLabelFade(true);
        b.yta.addShape(glaze::Shape::text(
            {200, 150}, "Skolan", glaze::TextStyle{.collide = false, .minZoom = 2.0}));
        b.frame();
        b.yta.setZoom(1.0);
        b.frame();
        const auto g = glyfAnrop(b);
        const int  t = texel(g.empty() ? nullptr : g[0], 0);
        check(t > 0 && t < 255, "ZV5 med toning tonar etiketten ut över tröskeln",
              std::to_string(t));
    }
}

// MB — MINNESBUDGETEN PER RITYTA: två ritytor i samma bildcache.
// En bild på 16 × 16 är 1024 byte.
void fallMB()
{
    const auto bildform = [](const glaze::Bitmap& bm) {
        return glaze::Shape::image({100, 100, 50, 50}, bm);
    };
    const auto ram = [](Bänk& b, glaze::Canvas* a, glaze::Canvas* c2) {
        b.bilder.trim(std::size_t{256} << 20, std::size_t{16} << 20);
        b.res.trim();
        b.r.transienta.clear();
        b.r.anrop.clear();
        if (a)
            a->render(b.c);
        if (c2)
            c2->render(b.c);
    };
    // MB1 — A med två bilders budget vräker sin äldsta; B:s (äldre) bild rörs
    // inte.
    {
        Bänk          b;
        Rityta        B;
        glaze::Bitmap a1(16, 16), a2(16, 16), a3(16, 16), b1(16, 16);
        b.yta.setMemoryBudget(2048);
        b.yta.addShape(0, bildform(a1));
        b.yta.addShape(b.yta.addLayer("2"), bildform(a2));
        b.yta.addShape(b.yta.addLayer("3"), bildform(a3));
        B.addShape(bildform(b1));
        const auto visa = [&](int lager) {
            for (int i = 0; i < 3; ++i)
                b.yta.setLayerVisible(i, i == lager);
        };
        visa(0);
        ram(b, &b.yta, &B);
        visa(1);
        ram(b, &b.yta, nullptr);
        visa(2);
        ram(b, &b.yta, nullptr);
        ram(b, nullptr, nullptr);   // trim: A = 3072 > 2048
        const std::size_t kvar = b.bilder.canvasBytes();
        b.r.resetStats();
        ram(b, nullptr, &B);
        const std::uint64_t bUpp = b.r.stats().bytesUploaded;
        check(kvar == 3 * 1024u && bUpp < 1024,
              "MB1 A över sin budget vräker sin äldsta — B:s bild står kvar",
              std::to_string(kvar) + " byte kvar, B laddade upp " +
                  std::to_string(bUpp));
    }
    // MB2 — en bild ritad av A och sedan av B räknas på B: A ligger inte
    // över sin budget (en bild), och ingenting vräks.
    {
        Bänk          b;
        Rityta        B;
        glaze::Bitmap x(16, 16), y(16, 16);
        b.yta.setMemoryBudget(1024);
        b.yta.addShape(0, bildform(x));
        b.yta.addShape(b.yta.addLayer("y"), bildform(y));
        B.addShape(bildform(x));
        ram(b, &b.yta, nullptr);   // A: x och y
        ram(b, nullptr, &B);       // B: x — nu B:s
        ram(b, nullptr, nullptr);
        check(b.bilder.canvasCount() == 2,
              "MB2 kontot är den SENASTE ritytans — ingenting vräks",
              std::to_string(b.bilder.canvasCount()) + " bilder");
    }
    // MB3 — en riven rityta lämnar sina bilder åt fönstrets konto (förvalet).
    {
        Bänk          b;
        glaze::Bitmap x(16, 16), y(16, 16);
        {
            Rityta A;
            A.setMemoryBudget(1024);
            A.addShape(0, bildform(x));
            A.addShape(A.addLayer("y"), bildform(y));
            ram(b, &A, nullptr);
        }
        ram(b, nullptr, nullptr);
        ram(b, nullptr, nullptr);
        check(b.bilder.canvasCount() == 2 && b.bilder.accountBytes(0) == 2048,
              "MB3 en riven ritytas bilder går till fönstret och vräks inte",
              std::to_string(b.bilder.canvasCount()) + " bilder, " +
                  std::to_string(b.bilder.accountBytes(0)) + " byte på fönstret");
    }
    // MB4 — kontots byte är summan av dess poster, och en bild flyttar konto
    // när en annan ritar den.
    {
        Inspelare         r;
        glaze::ImageCache c{r};
        glaze::Bitmap     p(16, 16), q(16, 16);
        const auto        liv = std::make_shared<int>(0);
        c.trim(std::size_t{256} << 20, std::size_t{16} << 20);
        const std::uint64_t förra = c.beginAccount(77, liv, 0);
        c.get(p);
        c.get(q);
        c.endAccount(förra);
        const std::size_t på77 = c.accountBytes(77);
        c.trim(std::size_t{256} << 20, std::size_t{16} << 20);
        c.get(p);
        check(förra == 0 && på77 == 2048 && c.accountBytes(77) == 1024 &&
                  c.accountBytes(0) == 1024 && c.canvasBytes() == 2048,
              "MB4 kontots byte = summan av dess poster; ritad utanför flyttar den",
              std::to_string(på77) + " → " + std::to_string(c.accountBytes(77)) +
                  " / " + std::to_string(c.accountBytes(0)));
    }
}

// ------------------------------------------------------------------ KL
//
// MÅLARENS KLIPP. Förväntat värde är rektanglarnas egen geometri: planen är
// normerade, så ett plans värde i en punkt är punktens avstånd till kanten,
// och för en punkt INNE i ett konvext snitt är det minsta planvärdet
// avståndet till snittets rand — det minsta av ytornas egna kantavstånd.
// Utanför prövas bara punkter bortom en kants mitt, där kantens eget värde
// är det minsta.

// Det minsta planvärdet i punkten (målets px); +∞ utan plan.
double planVärde(const gfx::SceneClip& k, double x, double y)
{
    double m = std::numeric_limits<double>::infinity();
    for (int i = 0; i < k.count; ++i)
        m = std::min(m, static_cast<double>(k.planes[i].nx) * x +
                            static_cast<double>(k.planes[i].ny) * y +
                            static_cast<double>(k.planes[i].d));
    return m;
}

// Kantavståndet i rektangeln [−hw, hw] × [−hh, hh] (punkten i rektangelns
// egna koordinater; positivt inne).
double rutAvstånd(double x, double y, double hw, double hh)
{
    return std::min(std::min(x + hw, hw - x), std::min(y + hh, hh - y));
}

constexpr double kRadPerGrad = 3.14159265358979323846 / 180.0;

// Målarens `rotate(grader)` på en punkt: (cos·x − sin·y, sin·x + cos·y).
glaze::Point vrid(glaze::Point q, double grader)
{
    const double r = grader * kRadPerGrad;
    return {std::cos(r) * q.x - std::sin(r) * q.y,
            std::sin(r) * q.x + std::cos(r) * q.y};
}

std::string tal(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

// Kantavståndet i en konvex fyrhörning (hörnen i ordning, åt vilket håll som
// helst) — det minsta av kanternas tecknade avstånd, positivt inne.
double polyAvstånd(const glaze::Point (&p)[4], double x, double y)
{
    double a = 0.0;
    for (int i = 0; i < 4; ++i)
        a += p[i].x * p[(i + 1) % 4].y - p[(i + 1) % 4].x * p[i].y;
    const double s = a < 0.0 ? -1.0 : 1.0;
    double       m = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; ++i) {
        const glaze::Point& u = p[i];
        const glaze::Point& v = p[(i + 1) % 4];
        const double        l = std::hypot(v.x - u.x, v.y - u.y);
        m = std::min(m, s * ((v.x - u.x) * (y - u.y) - (v.y - u.y) * (x - u.x)) / l);
    }
    return m;
}

bool nära(double a, double b, double tol = 1e-2)
{
    return std::abs(a - b) <= tol;
}

bool sammaRekt(const gfx::Rectf& r, double x, double y, double w, double h)
{
    return nära(r.x, x) && nära(r.y, y) && nära(r.w, w) && nära(r.h, h);
}

std::string rekt(const Anrop& a)
{
    if (!a.harScissor)
        return "ingen scissor";
    return tal(a.scissor.x) + ", " + tal(a.scissor.y) + ", " + tal(a.scissor.w) +
           " × " + tal(a.scissor.h);
}

void fallKL()
{
    const gfx::Color kRöd{255, 0, 0, 255};

    // KL1 ETT AXELPARALLELLT KLIPP i de lokala px: ytan och linjen i läget
    // bär samma fyra plan, scissorn är rektangeln, och efter läget ritas
    // utan klipp med ritytans egen scissor.
    {
        Bänk b;
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.screenSpace();
            {
                auto k = e.painter.save(glaze::Rect{100, 50, 120, 80});
                e.painter.fillRect({0, 0, 400, 300}, kRöd);
                e.painter.drawLine({0, 90}, {400, 90}, {.width = 2.0f});
            }
            e.painter.fillRect({0, 0, 10, 10}, kRöd);
        };
        b.frame();
        const double ox = b.c.box.x, oy = b.c.box.y;   // skalan är 1
        check(b.program() == std::vector<P>{P::Fill, P::Line, P::Fill},
              "KL1 klippet tömmer satsen: yta och linje i läget, ytan efter",
              namn(b.program()));
        if (b.r.anrop.size() == 3) {
            const gfx::SceneClip& k = b.r.anrop[0].clip;
            const double mitt  = planVärde(k, ox + 160, oy + 90);
            const double vä    = planVärde(k, ox + 95, oy + 90);
            const double hö    = planVärde(k, ox + 225, oy + 90);
            const double upp   = planVärde(k, ox + 160, oy + 45);
            const double ned   = planVärde(k, ox + 160, oy + 135);
            check(k.count == 4 && nära(mitt, 40) && nära(vä, -5) && nära(hö, -5) &&
                      nära(upp, -5) && nära(ned, -5),
                  "KL1 fyra plan: 40 px in i mitten, −5 px utanför varje kant",
                  std::to_string(k.count) + " plan: " + tal(mitt) + " / " +
                      tal(vä) + " " + tal(hö) + " " + tal(upp) + " " + tal(ned));
            check(b.r.anrop[1].clip == k,
                  "KL1 linjen i samma läge har samma klipp");
            check(b.r.anrop[0].harScissor &&
                      sammaRekt(b.r.anrop[0].scissor, ox + 100, oy + 50, 120, 80),
                  "KL1 scissorn är klippets rektangel", rekt(b.r.anrop[0]));
            check(b.r.anrop[2].clip.count == 0 && b.r.anrop[2].harScissor &&
                      sammaRekt(b.r.anrop[2].scissor, ox, oy, b.c.box.w, b.c.box.h),
                  "KL1 efter läget: inget klipp, ritytans egen scissor tillbaka",
                  std::to_string(b.r.anrop[2].clip.count) + " plan, " +
                      rekt(b.r.anrop[2]));
        }
    }

    // KL2 VRIDET 30°: fyrhörningens plan ger rektangelns egna kantavstånd i
    // de vridna koordinaterna — också i det omskrivna rutans hörn, som ligger
    // utanför — och scissorn är den omskrivna rektangeln.
    {
        Bänk b;
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.screenSpace();
            e.painter.translate(160, 110);
            e.painter.rotate(30);
            auto k = e.painter.save(glaze::Rect{-60, -40, 120, 80});
            e.painter.fillRect({-300, -300, 600, 600}, kRöd);
        };
        b.frame();
        const double cx = b.c.box.x + 160, cy = b.c.box.y + 110;
        if (b.r.anrop.size() != 1) {
            check(false, "KL2 ett anrop", namn(b.program()));
        } else {
            const gfx::SceneClip& k = b.r.anrop[0].clip;
            const glaze::Point    q[] = {{0, 0},   {0, -45},  {65, 0},  {-65, 0},
                                         {0, 45},  {50, 30},  {-70, -50},
                                         {70, 50}, {-55, 35}};
            bool        alla = k.count == 4;
            std::string fick = std::to_string(k.count) + " plan:";
            for (const glaze::Point& p : q) {
                const glaze::Point s = vrid(p, 30.0);
                const double       v = planVärde(k, cx + s.x, cy + s.y);
                alla = alla && nära(v, rutAvstånd(p.x, p.y, 60, 40));
                fick += " " + tal(v);
            }
            check(alla, "KL2 vriden: planen ger rektangelns kantavstånd i dess "
                        "egna koordinater (också i den omskrivna rutans hörn)",
                  fick);
            const double v30 = 30.0 * kRadPerGrad;
            const double hx  = 60 * std::cos(v30) + 40 * std::sin(v30);
            const double hy  = 60 * std::sin(v30) + 40 * std::cos(v30);
            check(b.r.anrop[0].harScissor &&
                      sammaRekt(b.r.anrop[0].scissor, cx - hx, cy - hy, 2 * hx, 2 * hy),
                  "KL2 scissorn är den omskrivna rektangeln", rekt(b.r.anrop[0]));
        }
    }

    // KL3 NÄSTLAT: en axelparallell rektangel och en 45° vriden kvadrat runt
    // samma mitt — snittet är kvadraten med topp och botten avskurna (sex
    // kanter), och efter det inre läget gäller det yttre igen.
    {
        Bänk b;
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.screenSpace();
            auto a = e.painter.save(glaze::Rect{100, 50, 120, 80});
            {
                auto s = e.painter.save();
                e.painter.translate(160, 90);
                e.painter.rotate(45);
                auto k = e.painter.save(glaze::Rect{-40, -40, 80, 80});
                e.painter.fillRect({-300, -300, 600, 600}, kRöd);
            }
            e.painter.fillRect({0, 0, 400, 300}, kRöd);
        };
        b.frame();
        const double cx = b.c.box.x + 160, cy = b.c.box.y + 90;
        if (b.r.anrop.size() != 2) {
            check(false, "KL3 två anrop", namn(b.program()));
        } else {
            const gfx::SceneClip& k = b.r.anrop[0].clip;
            // Förväntat värde: det minsta av rektangelns och kvadratens kantavstånd.
            const auto väntat = [&](double x, double y) {
                const glaze::Point q = vrid({x - cx, y - cy}, -45.0);
                return std::min(rutAvstånd(x - cx, y - cy, 60, 40),
                                rutAvstånd(q.x, q.y, 40, 40));
            };
            const glaze::Point p[] = {{cx, cy}, {cx, cy - 35}, {cx, cy - 45},
                                      {cx - 59.5, cy}, {cx + 20, cy + 25}};
            bool        alla = k.count == 6;
            std::string fick = std::to_string(k.count) + " plan:";
            for (const glaze::Point& q : p) {
                const double v = planVärde(k, q.x, q.y);
                alla = alla && nära(v, väntat(q.x, q.y));
                fick += " " + tal(v) + " (" + tal(väntat(q.x, q.y)) + ")";
            }
            check(alla, "KL3 nästlat: snittet — sex kanter, det minsta av de "
                        "två ytornas kantavstånd",
                  fick);
            const double r = 40.0 * std::sqrt(2.0);
            check(b.r.anrop[0].harScissor &&
                      sammaRekt(b.r.anrop[0].scissor, cx - r, cy - 40, 2 * r, 80),
                  "KL3 scissorn är snittets omskrivna rektangel",
                  rekt(b.r.anrop[0]));
            const gfx::SceneClip& yttre = b.r.anrop[1].clip;
            check(yttre.count == 4 && nära(planVärde(yttre, cx, cy), 40) &&
                      nära(planVärde(yttre, cx - 65, cy), -5),
                  "KL3 efter det inre läget gäller det yttre klippet igen",
                  std::to_string(yttre.count) + " plan");
        }
    }

    // KL4 TOMT: utan bredd, med ett tal som inte är ändligt, två klipp utan
    // snitt och ett klipp utanför ritytan ritar ingenting — och stacken är i
    // balans efteråt (det sista anropet har ritytans egen scissor).
    {
        Bänk b;
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.screenSpace();
            {
                auto k = e.painter.save(glaze::Rect{50, 50, 0, 40});
                e.painter.fillRect({0, 0, 400, 300}, kRöd);
            }
            {
                auto k = e.painter.save(glaze::Rect{50, 50, std::nan(""), 40});
                e.painter.fillRect({0, 0, 400, 300}, kRöd);
            }
            {
                auto a = e.painter.save(glaze::Rect{0, 0, 50, 50});
                auto k = e.painter.save(glaze::Rect{100, 100, 50, 50});
                e.painter.fillRect({0, 0, 400, 300}, kRöd);
                e.painter.drawLine({0, 0}, {400, 300}, {.width = 2.0f});
            }
            {
                auto k = e.painter.save(glaze::Rect{500, 400, 50, 50});
                e.painter.fillRect({0, 0, 800, 600}, kRöd);
            }
            e.painter.fillRect({0, 0, 10, 10}, kRöd);
        };
        b.frame();
        check(b.r.anrop.size() == 1 && b.r.anrop[0].clip.count == 0 &&
                  b.r.anrop[0].harScissor &&
                  sammaRekt(b.r.anrop[0].scissor, b.c.box.x, b.c.box.y,
                            b.c.box.w, b.c.box.h),
              "KL4 ett tomt klipp ritar ingenting, och stacken är i balans",
              namn(b.program()));
    }

    // KL5 FLER ÄN ÅTTA KANTER: tre kvadrater vridna 0°, 30° och 60° runt
    // samma mitt har ett tolvhörnigt snitt — det blir åtta plan, och varje
    // punkt planen släpper in ligger i alla tre kvadraterna (klippet krymper,
    // det växer aldrig). Mitten ligger kvar långt inne.
    {
        Bänk b;
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            e.painter.screenSpace();
            e.painter.translate(200, 150);
            auto k0 = e.painter.save(glaze::Rect{-50, -50, 100, 100});
            e.painter.rotate(30);
            auto k1 = e.painter.save(glaze::Rect{-50, -50, 100, 100});
            e.painter.rotate(30);
            auto k2 = e.painter.save(glaze::Rect{-50, -50, 100, 100});
            e.painter.fillRect({-300, -300, 600, 600}, kRöd);
        };
        b.frame();
        const double cx = b.c.box.x + 200, cy = b.c.box.y + 150;
        if (b.r.anrop.size() != 1) {
            check(false, "KL5 ett anrop", namn(b.program()));
        } else {
            const gfx::SceneClip& k = b.r.anrop[0].clip;
            bool krymper = true;
            int  inne    = 0;
            for (double y = -60; y <= 60; y += 1.5)
                for (double x = -60; x <= 60; x += 1.5) {
                    if (planVärde(k, cx + x, cy + y) < 0.0)
                        continue;
                    ++inne;
                    for (double g : {0.0, 30.0, 60.0}) {
                        const glaze::Point q = vrid({x, y}, -g);
                        krymper = krymper && rutAvstånd(q.x, q.y, 50, 50) >= -1e-3;
                    }
                }
            check(k.count == gfx::kMaxClipPlanes && krymper && inne > 0 &&
                      planVärde(k, cx, cy) > 44.0,
                  "KL5 tolv kanter blir åtta: planen släpper bara in det som "
                  "ligger i alla tre, och mitten ligger kvar långt inne",
                  std::to_string(k.count) + " plan, " + std::to_string(inne) +
                      " punkter inne, mitten " + tal(planVärde(k, cx, cy)));
        }
    }

    // KL6 I VÄRLDEN under bäring 30°, tilt 40° och zoom 2: planen är
    // fyrhörningen där ritytans `toLocal` lägger rektangelns hörn —
    // mittpunkten och 4 px utanför varje kants mitt.
    {
        Bänk b;
        b.yta.setCenter(500.0, 500.0);
        b.yta.setZoom(2.0);
        b.yta.setBearing(30.0f);
        b.yta.setPitch(40.0f);
        b.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
            auto k = e.painter.save(glaze::Rect{490, 495, 20, 10});
            e.painter.fillRect({400, 400, 200, 200}, kRöd);
        };
        b.frame();
        const glaze::Point hörn[4] = {{490, 495}, {510, 495}, {510, 505}, {490, 505}};
        glaze::Point       px[4];
        bool               synlig = true;
        for (int i = 0; i < 4; ++i) {
            const std::optional<glaze::Point> l = b.yta.toLocal(hörn[i]);
            synlig = synlig && l.has_value();
            if (l)
                px[i] = {b.c.box.x + l->x, b.c.box.y + l->y};
        }
        if (!synlig || b.r.anrop.size() != 1) {
            check(false, "KL6 rektangelns hörn på ytan och ett anrop",
                  namn(b.program()));
        } else {
            const gfx::SceneClip& k = b.r.anrop[0].clip;
            const double mx = (px[0].x + px[1].x + px[2].x + px[3].x) / 4.0;
            const double my = (px[0].y + px[1].y + px[2].y + px[3].y) / 4.0;
            bool         alla = k.count == 4 &&
                        nära(planVärde(k, mx, my), polyAvstånd(px, mx, my));
            std::string fick = std::to_string(k.count) + " plan, mitten " +
                               tal(planVärde(k, mx, my)) + " (" +
                               tal(polyAvstånd(px, mx, my)) + ")";
            for (int i = 0; i < 4; ++i) {
                // 4 px ut från kantens mitt, vinkelrätt — bort från mitten.
                const glaze::Point& u = px[i];
                const glaze::Point& v = px[(i + 1) % 4];
                const double        ex = (u.x + v.x) / 2.0, ey = (u.y + v.y) / 2.0;
                const double        l  = std::hypot(v.x - u.x, v.y - u.y);
                double              nx = -(v.y - u.y) / l, ny = (v.x - u.x) / l;
                if (nx * (mx - ex) + ny * (my - ey) > 0.0) {
                    nx = -nx;
                    ny = -ny;
                }
                const double värde = planVärde(k, ex + 4.0 * nx, ey + 4.0 * ny);
                alla = alla && nära(värde, -4.0, 0.05);
                fick += " " + tal(värde);
            }
            check(alla, "KL6 i världen under bäring och tilt: planen är "
                        "rektangelns bild på ytan",
                  fick);
        }
    }

    // KL7 FASADENS REGEL: ett klipp med fler plan än kMaxClipPlanes eller ett
    // aktivt plan som inte är ändligt avvisas av `meshDrawOk` — ett plan
    // efter `count` läses inte.
    {
        Inspelare             r;
        const gfx::LineVertex v[4]{};
        const std::uint32_t   idx[6] = {0, 1, 2, 0, 2, 3};
        gfx::MeshDraw         d;
        d.program    = gfx::MeshProgram::Line;
        d.vertices   = r.uploadTransient(gfx::BufferKind::Vertex, v, sizeof v);
        d.indices    = r.uploadTransient(gfx::BufferKind::Index, idx, sizeof idx);
        d.indexCount = 6;
        d.uniforms.clip.count = gfx::kMaxClipPlanes + 1;
        r.drawMesh(d);
        const std::size_t förMånga = r.anrop.size();
        d.uniforms.clip.count           = 1;
        d.uniforms.clip.planes[0]       = gfx::ClipPlane{std::nanf(""), 0.0f, 0.0f, 0.0f};
        r.drawMesh(d);
        const std::size_t nan = r.anrop.size();
        d.uniforms.clip.planes[0] = gfx::ClipPlane{1.0f, 0.0f, -5.0f, 0.0f};
        d.uniforms.clip.planes[1] = gfx::ClipPlane{std::nanf(""), 0.0f, 0.0f, 0.0f};
        r.drawMesh(d);
        check(förMånga == 0 && nan == 0 && r.anrop.size() == 1,
              "KL7 meshDrawOk avvisar för många plan och ett NaN-plan, och "
              "läser inte planen efter count",
              std::to_string(förMånga) + " / " + std::to_string(nan) + " / " +
                  std::to_string(r.anrop.size()));
    }
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== painter_gate — ritytans lager och målare (headless) ===\n");

    fallK();
    fallL();
    fallR();
    fallL3();
    fallTB();
    fallC();
    fallG();
    fallI();
    fallN();
    fallP();
    fallV();
    fallO();
    fallX();
    fallY();
    fallTD();
    fallLO();
    fallGT();
    fallZV();
    fallMB();
    fallKL();

    std::printf(g_fel ? "=== painter_gate: %d FEL av %d ===\n"
                      : "=== painter_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
