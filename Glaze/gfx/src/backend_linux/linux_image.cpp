// Bilder i Linux-backenden: libpng (systemkomponent) avkodar PNG → rak
// RGBA8 (`gfx::decodeImage`, trådsäker) → `createImage` → GL-textur; ritas
// i shader-mode 5 (RGBA rakt av, samma väg som compositen) i den vanliga
// batch-pipelinen. Plus fasadens gfx::encodePng (glaze-render) — libpng åt
// andra hållet. Spegel av d3d11_image.cpp. Rad 0 överst, rak alfa — matchar
// fasadkontraktet exakt, ingen swizzle. BARA PNG: libpng är den enda
// avkodaren här.
//
// AVKODNINGEN GÅR GENOM libpng:s FULLA API.
// Det FÖRENKLADE API:t (`png_image_*`) behandlar 16-bitars data som LINJÄRT
// ljus med förmultiplicerad alfa och räknar om till sRGB vid nedskalningen —
// `PNG_IMAGE_FLAG_16BIT_sRGB` rättade grått men inte RGBA med alfa (0 blev
// 180 vid alfa 90), och WIC och Image I/O gör ingen sådan omräkning. Därför:
// rundad 16 → 8-skalning som vår egen kodek, palett och grått utvidgat till
// RGB, tRNS till alfa, en fyllnadsalfa 255 och interlace-passen — och INGEN
// gammakorrigering. Kodaren använder förenklade API:t: 8-bitars RGBA in
// är exakt PNG_FORMAT_RGBA.

#include <png.h>

#include <algorithm>
#include <climits>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../image_util.h"
#include "gfx/platform.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

// Läsningens källa: bytes i minnet, lästa genom libpng:s återanrop.
struct PngKälla {
    const std::uint8_t* data = nullptr;
    std::size_t         size = 0;
    std::size_t         pos  = 0;
};

void läsPngBytes(png_structp png, png_bytep ut, png_size_t n)
{
    auto* k = static_cast<PngKälla*>(png_get_io_ptr(png));
    if (!k || n > k->size - k->pos)
        png_error(png, "PNG-datan tog slut");   // longjmp, återvänder aldrig
    std::memcpy(ut, k->data + k->pos, n);
    k->pos += n;
}

// libpng:s fel: meddelandet till anroparens buffert, därefter longjmp till
// vakten — en felfunktion får aldrig återvända. Bufferten ligger i
// ANROPARENS ram (`avkodaPng`), inte i ramen som anropade setjmp, så dess
// innehåll är bestämt efter hoppet.
constexpr std::size_t kFelLängd = 160;

void pngFel(png_structp png, png_const_charp msg)
{
    if (auto* buf = static_cast<char*>(png_get_error_ptr(png)))
        std::snprintf(buf, kFelLängd, "%s", msg ? msg : "");
    png_longjmp(png, 1);
}

// Varningarna (t.ex. en sRGB-profil libpng känner som felaktig) tystas —
// bilden avkodas ändå, och förenklade API:t tystade dem likadant.
void pngVarning(png_structp, png_const_charp) {}

// Läsningens två strukturer, rivna i ALLA utgångar.
struct PngLäsning {
    png_structp png  = nullptr;
    png_infop   info = nullptr;
    ~PngLäsning()
    {
        if (png)
            png_destroy_read_struct(&png, &info, nullptr);
    }
};

// Pixlarna och radpekarna — i ANROPARENS ram, så att vaktens ram bara bär
// triviala objekt (nedan).
struct PngUt {
    std::vector<std::uint8_t> pixlar;
    std::vector<png_bytep>    rader;
    png_uint_32               w = 0, h = 0;
};

// VAKTEN: setjmp + varje libpng-anrop som kan hoppa. Ramen bär BARA pekare
// och tal — en longjmp förbi en icke-trivial destruktor är odefinierad
// ([csetjmp.syn]), och lokala som ändras efter setjmp är obestämda efter
// hoppet. Vektorerna bor i `ut` (anroparens ram); de växer här men förstörs
// där. false = libpng gav upp (meddelandet står i felbufferten).
bool läsMedVakt(png_structp png, png_infop info, PngUt* ut)
{
    if (setjmp(png_jmpbuf(png)))
        return false;

    png_read_info(png, info);
    png_uint_32 w = 0, h = 0;
    int         djup = 0, färg = 0;
    png_get_IHDR(png, info, &w, &h, &djup, &färg, nullptr, nullptr, nullptr);
    // Pixlarnas byte måste rymmas i size_t (32-bitars bygge) — idiomet med
    // en tur och retur genom typen, som inte blir en alltid-falsk jämförelse
    // där size_t är 64 bitar.
    const std::uint64_t byte = static_cast<std::uint64_t>(w) * h * 4;
    if (w == 0 || h == 0 || w > INT_MAX / 4 || h > INT_MAX ||
        byte != static_cast<std::size_t>(byte))
        png_error(png, "orimliga mått");

    // TRANSFORMERNA → RGBA8 rak alfa (libpng ordnar dem själv). 16 bitar
    // SKALAS med rundning — (v·255 + 32767) / 65535, vår kodeks regel
    // (Glaze/assets/src/png.cpp) — och trunkeras bara om libpng-bygget
    // saknar skalningen.
    if (djup == 16) {
#ifdef PNG_READ_SCALE_16_TO_8_SUPPORTED
        png_set_scale_16(png);
#else
        png_set_strip_16(png);
#endif
    }
    if (färg == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (färg == PNG_COLOR_TYPE_GRAY && djup < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    const bool trns = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
    if (trns)
        png_set_tRNS_to_alpha(png);
    if (färg == PNG_COLOR_TYPE_GRAY || färg == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    if ((färg & PNG_COLOR_MASK_ALPHA) == 0 && !trns)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_set_interlace_handling(png);   // Adam7: png_read_image tar alla pass
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != static_cast<std::size_t>(w) * 4)
        png_error(png, "transformerna gav inte RGBA8");

    ut->pixlar.resize(static_cast<std::size_t>(w) * h * 4);
    ut->rader.resize(h);
    for (png_uint_32 y = 0; y < h; ++y)
        ut->rader[y] = ut->pixlar.data() + static_cast<std::size_t>(y) * w * 4;
    png_read_image(png, ut->rader.data());
    png_read_end(png, nullptr);
    ut->w = w;
    ut->h = h;
    return true;
}

// PNG-bytes → rak RGBA8, rad 0 överst. Inget globalt tillstånd — varje
// avkodning har sina egna strukturer, så den är trådsäker som
// `gfx::decodeImage` lovar. `fel` = libpng:s meddelande vid avslag.
bool avkodaPng(const void* data, std::size_t size,
               std::vector<std::uint8_t>& out, int& w, int& h, std::string& fel)
{
    char       felbuf[kFelLängd] = {};
    PngLäsning läs;
    läs.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, felbuf, pngFel,
                                     pngVarning);
    if (!läs.png) {
        fel = "png_create_read_struct misslyckades";
        return false;
    }
    läs.info = png_create_info_struct(läs.png);
    if (!läs.info) {
        fel = "png_create_info_struct misslyckades";
        return false;
    }
    PngKälla källa{static_cast<const std::uint8_t*>(data), size, 0};
    png_set_read_fn(läs.png, &källa, läsPngBytes);
    PngUt ut;
    if (!läsMedVakt(läs.png, läs.info, &ut)) {
        fel = felbuf;
        return false;
    }
    out = std::move(ut.pixlar);
    w   = static_cast<int>(ut.w);
    h   = static_cast<int>(ut.h);
    return true;
}

} // namespace

// ---------------------------------------------------------------- GlImage

GlImage::~GlImage()
{
    if (tex_ && renderer_)
        renderer_->destroyTexture(tex_);
}

// ---------------------------------------------------------------- fabriker

// Fildata → pixlar, utan renderer (image.h: trådsäker). BARA PNG — libpng
// tar inget annat, men signaturen prövas ändå först, med samma logg som
// D3D11 och Metal.
bool decodeImage(const void* data, std::size_t size,
                 std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (!hasPngSignature(data, size)) {
        log("decodeImage: %zu byte är inte PNG — bara PNG avkodas (gfx/image.h)",
            size);
        return false;
    }
    std::string fel;
    if (!avkodaPng(data, size, rgba, w, h, fel)) {
        log("decodeImage: kunde inte avkoda %zu byte (%s)", size, fel.c_str());
        return false;
    }
    return true;
}

std::unique_ptr<Image> GlRenderer::loadImage(const std::string& path)
{
    // Returnerar alltid ett objekt — valid() avgör (som loadFont). Filen
    // läses hel och går SAMMA avkodning som bytes ur minnet.
    std::ifstream fil(path, std::ios::binary);
    if (!fil.is_open()) {
        log("loadImage: kunde inte läsa %s", path.c_str());
        return std::make_unique<GlImage>();
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(fil)),
                                          std::istreambuf_iterator<char>());
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    std::string               fel;
    if (!avkodaPng(bytes.data(), bytes.size(), px, w, h, fel)) {
        log("loadImage: kunde inte läsa %s (%s)", path.c_str(), fel.c_str());
        return std::make_unique<GlImage>();
    }
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

std::unique_ptr<Image> GlRenderer::loadImage(const void* data,
                                             std::size_t size)
{
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    if (!decodeImage(data, size, px, w, h))   // loggar själv
        return std::make_unique<GlImage>();
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

// ------------------------------------------------------ bilder ur pixlar

std::unique_ptr<Image> GlRenderer::createImage(int w, int h, ImageFormat f,
                                               const void* px,
                                               std::size_t stride,
                                               bool mipmaps)
{
    std::size_t rowBytes = 0;
    if (!ready() || !imageDescOk(w, h, f, stride, rowBytes))
        return std::make_unique<GlImage>();
    // Utan pixlar: NOLLOR — glTexImage2D med nullptr lämnar innehållet
    // odefinierat, och fasaden lovar en nollställd bild.
    std::vector<std::uint8_t> zeros;
    const void*               src = px;
    if (!src) {
        zeros.assign(rowBytes * static_cast<std::size_t>(h), 0);
        src = zeros.data();
    }
    const GlTexture t = createTexture(f, w, h, src, rowBytes, mipmaps);
    if (!t)
        return std::make_unique<GlImage>();   // createTexture loggade
    if (mipmaps)
        generateMips(t);   // kedjan ur nivå 0
    if (px)
        stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                                bytesPerPixel(f);
    return std::make_unique<GlImage>(this, t, w, h, f, false, false, mipmaps);
}

bool GlRenderer::updateImage(Image& img, int x, int y, int w, int h,
                             const void* px, std::size_t stride)
{
    std::size_t rowBytes = 0;
    if (!imageUpdateOk(img, x, y, w, h, px, stride, rowBytes) ||
        !makeCurrent())
        return false;
    // Det som BATCHATS läser texturen först vid flushen — flusha först, så
    // det redan ritade behåller det gamla innehållet (renderer.h). Utanför
    // en frame är batchen tom och flushen gör ingenting.
    flush();
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    auto& im = static_cast<GlImage&>(img);
    updateTexture(im.tex(), img.format(), x, y, w, h, px, rowBytes);
    if (im.mipmapped())
        generateMips(im.tex());   // kedjan ur den nya nivå 0
    stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                            bytesPerPixel(img.format());
    return true;
}

void GlRenderer::drawImage(Image& img, const Rectf& src, const Rectf& dst,
                           float opacity)
{
    // Bilden kommer alltid ur samma backend — casten är säker per
    // konstruktion (som drawText:s Font).
    auto& im = static_cast<GlImage&>(img);
    if (!im.valid() || im.width() <= 0 || im.height() <= 0 ||
        !imageDrawOk(img))
        return;

    // Käll-rect → normaliserad UV. En bild ur pixlar laddas med rad 0 =
    // bildens överkant, så v-axeln är rak; ett render-mål (FBO) har raderna
    // nerifrån (`flippedV`) och speglas här — samma flipp som compositens.
    const float iw = static_cast<float>(im.width());
    const float ih = static_cast<float>(im.height());
    Rectf uv{src.x / iw, src.y / ih, src.w / iw, src.h / ih};
    if (im.flippedV()) {
        uv.y = 1.0f - uv.y;
        uv.h = -uv.h;
    }
    // Mode 5 = RGBA rakt av, 6 = ett render-måls premultiplicerade
    // innehåll, 7 = R8 som gråskala — och bildens EGET filter.
    const float mode = im.format() == ImageFormat::R8 ? 7.0f
                       : im.premultiplied()           ? 6.0f
                                                      : 5.0f;
    imageQuad(im.tex(), dst, uv, opacity, mode, im.filter());
}

// ======================================================== Image-programmet
//
// Scenens bilder — spegel av d3d11_image.cpp:s program,
// också de projektiva vikterna `q` (attribut 2)
// och färgjusteringen (`uAdjust` genom glaze_colorAdjust, `kGlslAdjustSrc` —
// samma del som de egna programmens förspel).
// GL-skillnaderna: uniformerna är PER PROGRAM (egna locations och egen
// maskgeneration), z räknas om till GL:s [−w, w], och fragmentets läge för
// klippmaskerna kommer ur ett PERSPEKTIVKORREKT (x, y, w) som delas i
// fragmentshadern — aldrig ur gl_FragCoord (y-flippfällan, se
// linux_renderer.cpp) och inte heller ur ett noperspective-läge räknat i
// vertexshadern: en platta under tilt kan ha hörn BAKOM kameran, och deras
// division hade varit skräp som klippningen därefter interpolerat.

namespace {

const char kImageVsSrc[] = R"glsl(
uniform mat4 uViewProj;   // chunk-lokalt → pixelklipp
uniform vec4 uParams;     // .xy målets storlek (px), .z opaciteten,
                          // .w flaggorna: 1 premultiplicerad, 2 speglad v, 4 R8
in vec2 aPos;
in vec2 aUv;
in float aQ;              // hörnets projektiva vikt (1 för en rektangel)
out vec3 vUvq;            // (u·q, v·q, q), perspektivkorrekt (förvalet)
out vec3 vClip;           // (x, y, w) i pixelklipp — maskernas läge

void main()
{
    vec4 c = uViewProj * vec4(aPos, 0.0, 1.0);
    // Pixelklipp → GL:s klipprymd: x och y som D3D:s, z ur [0, w] till
    // [−w, w]. Linjär i (x, y, z, w) — w går orört till rasteriseringen.
    gl_Position = vec4(c.x * 2.0 / uParams.x - c.w,
                       c.w - c.y * 2.0 / uParams.y, 2.0 * c.z - c.w, c.w);
    int flags = int(uParams.w + 0.5);
    // Speglingen FÖRE multiplikationen med q — v' är bildens koordinat.
    float v = (flags & 2) != 0 ? 1.0 - aUv.y : aUv.y;
    vUvq  = vec3(aUv.x * aQ, v * aQ, aQ);
    vClip = vec3(c.x, c.y, c.w);
}
)glsl";

// Fragmentshadern — klippmaskernas del (kGlslClipSrc) och färgjusteringen
// (kGlslAdjustSrc) klistras FÖRE.
const char kImageFsSrc[] = R"glsl(
uniform sampler2D uTex;
uniform vec4 uParams;
uniform vec4 uAdjust;   // färgjusteringen: mättnad, kontrast, ljushet, 0
in vec3 vUvq;
in vec3 vClip;
out vec4 frag;

void main()
{
    int  flags = int(uParams.w + 0.5);
    vec4 t     = texture(uTex, vUvq.xy / vUvq.z);
    if ((flags & 4) != 0)
        t = vec4(t.rrr, 1.0);                       // R8: gråskala
    if ((flags & 1) != 0)
        t.rgb = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);   // render-mål
    // Färgjusteringen på den RAKA färgen — förvalen lämnar den orörd.
    t.rgb = glaze_colorAdjust(t.rgb, uAdjust.xyz);
    // Rak alfa som UI:t; opaciteten, klippmaskerna och målarens klipp rör
    // bara alfan.
    vec2 pix = vClip.xy / vClip.z;
    t.a *= clamp(uParams.z, 0.0, 1.0) * clipCoverage(pix) * sceneClip(pix);
    frag = t;
}
)glsl";

const void* byteOffset(std::size_t o)
{
    return reinterpret_cast<const void*>(o);
}

} // namespace

bool GlRenderer::initImageProgram()
{
    const StatTimer klocka(stats_.compileNs);   // programbygget
    const char* vsParts[] = {kGlslVersion, kImageVsSrc};
    const char* fsParts[] = {kGlslVersion, kGlslClipSrc, kGlslAdjustSrc,
                             kImageFsSrc};
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, vsParts, 2);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, fsParts, 4);
    if (!vs || !fs) {
        if (vs)
            pglDeleteShader(vs);
        if (fs)
            pglDeleteShader(fs);
        log("GL: Image-programmet kunde inte kompileras — drawMesh ritar "
            "inga bilder");
        return false;
    }
    static const char* const kAttribs[] = {"aPos", "aUv", "aQ"};
    imageProgram_ = linkGlProgram(vs, fs, kAttribs, 3);
    if (!imageProgram_) {
        log("GL: Image-programmet kunde inte länkas — drawMesh ritar inga "
            "bilder");
        return false;
    }
    imageViewProjLoc_ = pglGetUniformLocation(imageProgram_, "uViewProj");
    imageParamsLoc_   = pglGetUniformLocation(imageProgram_, "uParams");
    imageAdjustLoc_   = pglGetUniformLocation(imageProgram_, "uAdjust");
    imageClipP0Loc_   = pglGetUniformLocation(imageProgram_, "uClipP0");
    imageClipP1Loc_   = pglGetUniformLocation(imageProgram_, "uClipP1");
    imageClipNLoc_    = pglGetUniformLocation(imageProgram_, "uClipN");
    const int  texLoc = pglGetUniformLocation(imageProgram_, "uTex");
    const bool klipp  = findSceneClip(imageProgram_, imageSceneClip_);
    // Samma regel som de andra programmen: en location som saknas betyder
    // att källan och C++-sidan glidit isär — glUniform hade tyst svalt -1.
    if (imageViewProjLoc_ < 0 || imageParamsLoc_ < 0 || imageAdjustLoc_ < 0 ||
        imageClipP0Loc_ < 0 || imageClipP1Loc_ < 0 || imageClipNLoc_ < 0 ||
        texLoc < 0 || !klipp) {
        log("GL: Image-programmets uniformer saknas (uViewProj=%d uParams=%d "
            "uAdjust=%d uClipP0=%d uClipP1=%d uClipN=%d uTex=%d uSceneClip=%d "
            "uSceneClipN=%d) — drawMesh ritar inga bilder",
            imageViewProjLoc_, imageParamsLoc_, imageAdjustLoc_,
            imageClipP0Loc_, imageClipP1Loc_, imageClipNLoc_, texLoc,
            imageSceneClip_.planes, imageSceneClip_.count);
        pglDeleteProgram(imageProgram_);
        imageProgram_ = 0;
        return false;
    }
    pglUseProgram(imageProgram_);
    pglUniform1i(texLoc, 0);   // enhet 0, som UI:ts tex0

    // Programmets VAO: attributen påslagna en gång; pekarna per rit-anrop.
    pglGenVertexArrays(1, &imageVao_);
    pglBindVertexArray(imageVao_);
    pglEnableVertexAttribArray(0);
    pglEnableVertexAttribArray(1);
    pglEnableVertexAttribArray(2);   // aQ — de projektiva vikterna
    // UI-pipelinen är den bundna (init sker före första framen).
    pglUseProgram(program_);
    pglBindVertexArray(vao_);
    return true;
}

void GlRenderer::shutdownImageProgram()
{
    // Kontexten är aktuell (shutdownRenderer).
    if (imageVao_)
        pglDeleteVertexArrays(1, &imageVao_);
    if (imageProgram_)
        pglDeleteProgram(imageProgram_);
    imageVao_        = 0;
    imageProgram_    = 0;
    imageClipGen_    = 0;
    imageSceneClip_  = SceneClipLocs{};
}

void GlRenderer::drawImageMesh(const MeshDraw& d)
{
    if (!ready() || !meshDrawOk(d, imageProgram_ != 0) ||
        !imageDrawOk(*d.image))
        return;
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    const auto& im = static_cast<const GlImage&>(*d.image);

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Image) {
        pglUseProgram(imageProgram_);
        pglBindVertexArray(imageVao_);
        pipeline_ = Pipeline::Image;
        ++stats_.programSwitches;
    }
    syncClipMasks(imageClipP0Loc_, imageClipP1Loc_, imageClipNLoc_,
                  imageClipGen_);
    setSceneClip(d, imageSceneClip_);

    // Kameran, opaciteten, bildens flaggor och färgjusteringen — per anrop,
    // räknas inte som uppladdning (RenderStats).
    pglUniformMatrix4fv(imageViewProjLoc_, 1, GL_FALSE, d.uniforms.viewProj);
    pglUniform4f(imageParamsLoc_, static_cast<float>(targetW_),
                 static_cast<float>(targetH_),
                 std::clamp(d.uniforms.opacity, 0.0f, 1.0f),
                 sceneImageFlags(im));   // samma tal som HLSL:ns och MSL:ns
    float adjust[4];
    sceneImageAdjust(d.uniforms, adjust);   // likaså
    pglUniform4fv(imageAdjustLoc_, 1, adjust);

    // Bilden och DESS filter på enhet 0 — UI:ts flush binder sina egna vid
    // nästa ritning (samplerobjektet skuggar texturens parametrar).
    glBindTexture(GL_TEXTURE_2D, im.tex());
    bindSampler(im.filter());

    const auto& vb = static_cast<const GlGpuBuffer&>(*d.vertices.buffer);
    const auto& ib = static_cast<const GlGpuBuffer&>(*d.indices.buffer);
    const std::size_t base   = d.vertices.offset;
    const auto        stride = static_cast<GLsizei>(sizeof(ImageVertex));
    pglBindBuffer(GL_ARRAY_BUFFER, vb.id());
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(ImageVertex, x)));
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(ImageVertex, u)));
    pglVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(ImageVertex, q)));
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.id());   // VAO:ns tillstånd
    applyMeshState(d.state);   // renderläget
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount),
                   GL_UNSIGNED_INT, byteOffset(d.indices.offset));
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

// ---------------------------------------------------------------- encode

// PNG-encode (glaze-render): encode-spegeln av loadImage. Fasadens
// kontrakt (rad 0 överst, rak alfa, täta rader) är exakt PNG_FORMAT_RGBA —
// row_stride w × 4, ingen flipp, ingen swizzle.
bool encodePng(const std::string& path, const std::vector<std::uint8_t>& rgba,
               int w, int h)
{
    if (w <= 0 || h <= 0 ||
        rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        log("encodePng: ogiltiga mått/otillräcklig pixeldata (%dx%d)", w, h);
        return false;
    }
    png_image im{};
    im.version = PNG_IMAGE_VERSION;
    im.width   = static_cast<png_uint_32>(w);
    im.height  = static_cast<png_uint_32>(h);
    im.format  = PNG_FORMAT_RGBA;
    const bool ok = png_image_write_to_file(&im, path.c_str(), 0,
                                            rgba.data(), w * 4,
                                            nullptr) != 0;
    if (!ok)
        log("encodePng: kunde inte skriva %s", path.c_str());
    return ok;
}

} // namespace gfx
