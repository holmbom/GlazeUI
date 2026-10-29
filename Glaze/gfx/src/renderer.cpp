// Backend-neutral del av gfx::Renderer: klipp-stacken, scenens och
// bildernas delade regler och render-målens stack, renderlägets regler
// (`meshDrawOk`), målens form och läsning (`targetDescOk`, `readTargetOk`),
// de egna programmens regler (`customDrawOk`, `programDescError`) och
// brevlådan för GPU-tiden (`takeGpuTimes`).
// Intersektionen görs HÄR så semantiken (nästlade klipp kan aldrig växa,
// balanserade anrop) är identisk i alla backends — backends implementerar
// bara applyClip och bindTarget.
//
// Stacken bär TRE saker per nivå — det
// intersekterade scissor-rektet, nivåns EGEN rect och dess radie. Att
// masken lagras med sin egen geometri är bärande: snittet av två rundade
// rektanglar är inte en rundad rektangel, så en mask härledd ur snittet
// hade varit fel form. Urvalet av de fyra maskplatserna görs per
// applyClip-anrop ur den FULLSTÄNDIGA stacken och är därmed
// ICKE-DESTRUKTIVT: en poppad nivå kan alltid återfå sin mask.

#include <algorithm>
#include <cmath>

#include "gfx/platform.h"
#include "gfx/renderer.h"

namespace gfx {

// Helbildsformen — käll-recten är hela bilden. Ligger här och inte i varje
// backend: den är ren geometri, och backends implementerar bara den
// virtuella formen (se renderer.h).
void Renderer::drawImage(Image& img, const Rectf& dst, float opacity)
{
    drawImage(img,
              Rectf{0.0f, 0.0f, static_cast<float>(img.width()),
                    static_cast<float>(img.height())},
              dst, opacity);
}

void Renderer::pushClip(const Rectf& r)
{
    pushClip(r, 0.0f);
}

void Renderer::pushClip(const Rectf& r, float radius)
{
    Rectf c = r;
    if (!clipStack_.empty()) {
        // Intersektion med aktuellt klipp — tom överlappning ger en
        // nollstor rektangel (klipper allt), aldrig negativa mått.
        // Den RUNDADES rect deltar i snittet precis som alla andra: den
        // rundade formen är en delmängd av sin rektangel, så scissorn
        // förblir korrekt och konservativ.
        const Rectf& top = clipStack_.back().scissor;
        const float x1 = std::max(c.x, top.x);
        const float y1 = std::max(c.y, top.y);
        const float x2 = std::min(c.x + c.w, top.x + top.w);
        const float y2 = std::min(c.y + c.h, top.y + top.h);
        c = Rectf{x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1)};
    }
    clipStack_.push_back(ClipLevel{c, r, radius});
    applyTop();
}

void Renderer::popClip()
{
    if (clipStack_.empty()) {
        log("popClip utan matchande pushClip — ignoreras");
        return;
    }
    clipStack_.pop_back();
    applyTop();
}

bool Renderer::clipBounds(Rectf& out) const
{
    if (clipStack_.empty())
        return false;
    out = clipStack_.back().scissor;
    return true;
}

void Renderer::reapplyClip()
{
    applyTop();
}

void Renderer::applyTop()
{
    if (clipStack_.empty()) {
        applyClip(nullptr, nullptr, 0);
        maskOverflowLogged_ = false;
        return;
    }

    // De YTTERSTA aktiva rundade maskerna, i stackordning. Rektangulära
    // nivåer hoppas över helt — de konsumerar aldrig en maskplats, vilket
    // är det som gör damage-recten och vanliga containrar gratis.
    RoundedClip masks[kMaxRoundedClips];
    std::size_t count    = 0;
    std::size_t rounded  = 0;
    for (const ClipLevel& lvl : clipStack_) {
        if (lvl.radius <= 0.0f)
            continue;
        ++rounded;
        if (count < kMaxRoundedClips)
            masks[count++] = RoundedClip{lvl.mask, lvl.radius};
    }

    if (rounded > kMaxRoundedClips) {
        // EN logg per överskridande-episod: partiell omritning anropar den
        // här vägen en gång per damage-rect och skulle annars spamma.
        if (!maskOverflowLogged_) {
            log("klippstacken har %zu rundade masker — bara de %zu yttersta "
                "maskeras, resten degraderar till sin rektangel",
                rounded, kMaxRoundedClips);
            maskOverflowLogged_ = true;
        }
    } else {
        maskOverflowLogged_ = false;
    }

    applyClip(&clipStack_.back().scissor, count ? masks : nullptr, count);
}

// ------------------------------------------------------ scenens regler
//
// En bit per orsak i `sceneLogged_`: ett felaktigt anrop görs typiskt
// varje frame, och en logg per frame dränker det som är nytt.

namespace {
enum SceneLog : std::uint32_t {
    kLogProgram  = 1u << 0,   // + programmets värde (bit 0–7)
    kLogSlice    = 1u << 8,
    kLogKinds    = 1u << 9,
    kLogIndices  = 1u << 10,
    kLogAlign    = 1u << 11,
    kLogUpdate   = 1u << 12,
    // Bilderna.
    kLogImageDesc    = 1u << 16,
    kLogImageUpdate  = 1u << 17,
    kLogImageMissing = 1u << 18,
    // Ytorna och färgen.
    kLogRampMissing  = 1u << 19,
    // Linjens streck och spetsgräns (Line-programmet).
    kLogStroke       = 1u << 20,
    // Klippets kantplan.
    kLogClip         = 1u << 31,
    // Etiketternas synlighet.
    kLogLabels       = 1u << 22,
    // Render-målen.
    kLogTargetSelf   = 1u << 24,
    // Renderläget och målens form.
    kLogState        = 1u << 13,
    kLogNoDepth      = 1u << 14,
    kLogTargetDesc   = 1u << 25,
    kLogReadInvalid  = 1u << 26,
    kLogReadBound    = 1u << 27,
    // De egna programmen.
    kLogCustomProgram  = 1u << 15,
    kLogCustomLayout   = 1u << 21,
    kLogCustomStreams  = 1u << 23,
    kLogCustomUser     = 1u << 28,
    kLogCustomTextures = 1u << 29,
    kLogCustomFit      = 1u << 30,
};

// Renderläget: varje uppräkning inom sina värden — ett
// tal utanför hade gett ett odefinierat tillstånd i backendens tabeller — och
// biaserna ändliga.
bool stateOk(const RenderState& s)
{
    return static_cast<unsigned>(s.depth) <=
               static_cast<unsigned>(DepthTest::Always) &&
           static_cast<unsigned>(s.blend) <=
               static_cast<unsigned>(Blend::DepthOnly) &&
           static_cast<unsigned>(s.cull) <= static_cast<unsigned>(Cull::Front) &&
           std::isfinite(s.depthBias) && std::isfinite(s.slopeBias);
}

// Shaderns index är ett flyttal (basen läggs i en float-uniform i alla tre
// språken): heltalen är exakta upp till 2²⁴.
constexpr std::uint32_t kMaxLabelIndex = 1u << 24;

// Line-programmets egna uniformer: spetsgränsen är ändlig och minst 1 (SVG),
// och strecken är heldragna eller `makeDash`s form — ett jämnt antal
// kumulativa slut som aldrig minskar, en period över noll och en ändlig
// skala över noll (shadern delar med perioden och skalan).
bool strokeOk(const SceneUniforms& u)
{
    if (!std::isfinite(u.miterLimit) || u.miterLimit < 1.0f)
        return false;
    const SceneDash& d = u.dash;
    if (d.count == 0)
        return true;
    if (d.count < 2 || d.count > kMaxDashes || d.count % 2 != 0 ||
        !std::isfinite(d.offset) || !std::isfinite(d.perWorldUnit) ||
        !(d.perWorldUnit > 0.0f))
        return false;
    float prev = 0.0f;
    for (int i = 0; i < d.count; ++i) {
        if (!std::isfinite(d.ends[i]) || d.ends[i] < prev)
            return false;
        prev = d.ends[i];
    }
    return prev > 0.0f;
}

// Klippet: ett antal planen ryms i (skuggarnas loop läser så många)
// och de aktiva planen ändliga — ett NaN hade gett varje backend sitt eget
// svar på jämförelsen.
bool clipOk(const SceneClip& c)
{
    if (c.count < 0 || c.count > kMaxClipPlanes)
        return false;
    for (int i = 0; i < c.count; ++i) {
        const ClipPlane& p = c.planes[i];
        if (!std::isfinite(p.nx) || !std::isfinite(p.ny) || !std::isfinite(p.d))
            return false;
    }
    return true;
}
} // namespace

bool Renderer::meshDrawOk(const MeshDraw& d, bool programSupported)
{
    // Egna program har egna regler (vertexCount, strömmarna, blocken).
    if (d.program == MeshProgram::Custom)
        return customDrawOk(d, programSupported);
    if (d.indexCount == 0)
        return false;   // inget att rita — inget fel
    const char*   why = nullptr;
    std::uint32_t bit = 0;
    if (!programSupported) {
        why = "programmet finns inte i den här backenden";
        bit = kLogProgram << (static_cast<unsigned>(d.program) & 7u);
    } else if (!d.vertices.valid() || !d.indices.valid()) {
        why = "en buffertdel saknas eller går utanför sin buffert";
        bit = kLogSlice;
    } else if (d.vertices.buffer->kind() != BufferKind::Vertex ||
               d.indices.buffer->kind() != BufferKind::Index) {
        why = "vertex- och indexdelarna är förväxlade";
        bit = kLogKinds;
    } else if (static_cast<std::uint64_t>(d.indexCount) * 4u >
               d.indices.bytes) {
        why = "indexCount går utanför indexdelen";
        bit = kLogIndices;
    } else if (d.vertices.offset % 4u != 0 || d.indices.offset % 4u != 0) {
        why = "en del börjar inte på en hel fyrbyte";
        bit = kLogAlign;
    } else if (d.program == MeshProgram::Image &&
               (!d.image || !d.image->valid())) {
        why = "Image-programmet saknar en giltig bild";
        bit = kLogImageMissing;
    } else if ((d.program == MeshProgram::Fill ||
                d.program == MeshProgram::Line ||
                d.program == MeshProgram::Line3) &&
               d.uniforms.paint.kind != PaintKind::Solid &&
               (!d.image || !d.image->valid())) {
        why = "gradienten saknar sin ramp (MeshDraw::image)";
        bit = kLogRampMissing;
    } else if ((d.program == MeshProgram::Line ||
                d.program == MeshProgram::Line3) &&
               !strokeOk(d.uniforms)) {
        why = "strecken eller spetsgränsen är ogiltiga (SceneUniforms::dash/"
              "miterLimit — bygg strecken med makeDash)";
        bit = kLogStroke;
    } else if (!clipOk(d.uniforms.clip)) {
        why = "klippet är ogiltigt (SceneUniforms::clip — ett antal utanför "
              "0–kMaxClipPlanes eller ett plan som inte är ändligt)";
        bit = kLogClip;
    } else if (d.labels &&
               (!d.labels->valid() || d.labels->format() != ImageFormat::R8 ||
                d.labelBase >= kMaxLabelIndex)) {
        why = "etiketternas synlighet är ingen giltig R8-bild, eller basen är "
              "för stor (MeshDraw::labels)";
        bit = kLogLabels;
    } else if (!stateOk(d.state)) {
        why = "renderläget är ogiltigt (MeshDraw::state — en uppräkning utanför "
              "sina värden eller en bias som inte är ändlig)";
        bit = kLogState;
    }
    return meshVerdict(d, why, bit);
}

bool Renderer::meshVerdict(const MeshDraw& d, const char* why,
                           std::uint32_t bit)
{
    if (!why) {
        // Ett djuptest i ett mål UTAN djup verkar inte (scene.h) — anropet
        // ritas som med Off, men det är nästan alltid ett misstag och sägs
        // därför en gång.
        const RenderTarget* t = boundTarget();
        if (d.state.depth != DepthTest::Off && !(t && t->hasDepth()) &&
            !(sceneLogged_ & kLogNoDepth)) {
            sceneLogged_ |= kLogNoDepth;
            log("drawMesh: ett djuptest i ett mål utan djup verkar inte "
                "(TargetDesc::depth) — ritas utan djup");
        }
        return true;
    }
    if (!(sceneLogged_ & bit)) {
        sceneLogged_ |= bit;
        log("drawMesh: %s (program %u) — anropet ritas inte", why,
            static_cast<unsigned>(d.program));
    }
    return false;
}

bool Renderer::updateOk(const GpuBuffer& b, std::size_t offset,
                        const void* data, std::size_t bytes)
{
    if (b.valid() && data && bytes > 0 && offset % 4u == 0 &&
        bytes % 4u == 0 && offset <= b.size() && bytes <= b.size() - offset)
        return true;
    if (!(sceneLogged_ & kLogUpdate)) {
        sceneLogged_ |= kLogUpdate;
        log("updateBuffer: [%zu, +%zu) i en buffert på %zu byte — utanför, "
            "inte hela fyrbytes eller ogiltig; ingenting skrivs",
            offset, bytes, b.size());
    }
    return false;
}

// ------------------------------------------------------- egna program
//
// Se gfx/program.h. Reglerna är desamma i alla backends, så
// ett anrop som avvisas i en avvisas i alla — och de prövar allt backenden
// sedan litar på: layouten PASSAR programmets indata (varje plats det läser,
// av rätt slag — reflekterat av backenden när programmet kompilerades,
// `ShaderProgram::inputs`), strömmarna som layouten läser rymmer det som
// ritas (hela vertex och hela instanser), GlazeUser har programmets storlek
// och texturerna är programmets.

namespace {

// Layoutens form: platserna unika och under attrib::kCount, formatet och
// strömmen inom sina värden, offset och steg i hela fyrbytes och varje
// attribut inom sitt elements steg (högst kMaxVertexStride).
bool layoutOk(const VertexLayout& l)
{
    std::uint32_t seen = 0;
    for (const VertexAttrib& a : l.attribs) {
        if (a.location >= attrib::kCount || ((seen >> a.location) & 1u) ||
            static_cast<unsigned>(a.format) >
                static_cast<unsigned>(AttribFormat::UInt) ||
            a.stream > 1 || a.offset % 4u != 0)
            return false;
        seen |= 1u << a.location;
        const unsigned stride = l.stride[a.stream];
        if (stride == 0 || stride % 4u != 0 || stride > kMaxVertexStride ||
            a.offset + attribBytes(a.format) > stride)
            return false;
    }
    return true;
}

bool layoutReads(const VertexLayout& l, unsigned stream)
{
    for (const VertexAttrib& a : l.attribs)
        if (a.stream == stream)
            return true;
    return false;
}

// Passar layouten programmet? Varje plats vertexsteget läser finns i
// layouten, och med ett format av samma slag — heltal mot heltal, flyttal
// (också normerade byte) mot flyttal. Platser programmet inte läser får
// layouten ha.
bool layoutFits(const VertexLayout& l, const ShaderProgram& p)
{
    std::uint32_t has = 0, integer = 0;
    for (const VertexAttrib& a : l.attribs) {
        has |= 1u << a.location;
        if (attribInteger(a.format))
            integer |= 1u << a.location;
    }
    return (p.inputs() & ~has) == 0 &&
           ((integer ^ p.integerInputs()) & p.inputs()) == 0;
}

// En ström: delen giltig, en vertexbuffert, på en hel fyrbyte och med plats
// för `count` HELA element om `stride` byte.
bool streamOk(const BufferSlice& s, std::uint32_t count, unsigned stride)
{
    return s.valid() && s.buffer->kind() == BufferKind::Vertex &&
           s.offset % 4u == 0 &&
           static_cast<std::uint64_t>(count) * stride <= s.bytes;
}

// Texturerna: platserna under programmets antal har en giltig bild, och
// platserna efter är tomma — en bild programmet inte samplar är ett misstag.
bool texturesOk(const MeshDraw& d)
{
    const int n = d.shader->textures();
    for (int i = 0; i < kMaxProgramTextures; ++i) {
        const Image* img = d.textures[static_cast<std::size_t>(i)];
        if (i < n ? !(img && img->valid()) : img != nullptr)
            return false;
    }
    return true;
}

// En definition i C-förprocessorns form: bokstäver, siffror och _, inte en
// siffra först (ASCII — alla tre språken).
bool identifier(const std::string& s)
{
    if (s.empty() || (s[0] >= '0' && s[0] <= '9'))
        return false;
    for (const char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

} // namespace

bool Renderer::customDrawOk(const MeshDraw& d, bool programSupported)
{
    const char*   why     = nullptr;
    std::uint32_t bit     = 0;
    const bool    indexed = d.indexCount > 0;
    if (!programSupported) {
        why = "programmet finns inte i den här backenden";
        bit = kLogProgram << (static_cast<unsigned>(d.program) & 7u);
    } else if (!d.shader || !d.shader->valid()) {
        why = "det egna programmet saknas eller är ogiltigt (MeshDraw::shader "
              "— felet står i ShaderProgram::error)";
        bit = kLogCustomProgram;
    } else if (!d.layout || !layoutOk(*d.layout)) {
        why = "layouten saknas eller är ogiltig (MeshDraw::layout — platserna, "
              "formaten, offset och steg)";
        bit = kLogCustomLayout;
    } else if (!layoutFits(*d.layout, *d.shader)) {
        why = "layouten passar inte programmet: en plats som vertexsteget läser "
              "saknas, eller har ett format av fel slag (heltal mot flyttal)";
        bit = kLogCustomFit;
    } else if (!indexed && d.vertexCount == 0) {
        return false;   // inget att rita — inget fel
    } else if (indexed &&
               (!d.indices.valid() ||
                d.indices.buffer->kind() != BufferKind::Index ||
                d.indices.offset % 4u != 0 ||
                static_cast<std::uint64_t>(d.indexCount) * 4u >
                    d.indices.bytes)) {
        why = "indexdelen saknas, är ingen indexbuffert, börjar inte på en hel "
              "fyrbyte eller rymmer inte indexCount";
        bit = kLogCustomStreams;
    } else if (layoutReads(*d.layout, 0) &&
               !streamOk(d.vertices, indexed ? 0u : d.vertexCount,
                         d.layout->stride[0])) {
        why = "vertexströmmen saknas, är ingen vertexbuffert, börjar inte på en "
              "hel fyrbyte eller rymmer inte vertexCount hela vertex";
        bit = kLogCustomStreams;
    } else if (layoutReads(*d.layout, 1) &&
               (d.instanceCount == 0 ||
                !streamOk(d.instances, d.instanceCount,
                          d.layout->stride[1]))) {
        why = "instansströmmen saknas, instanceCount är 0, eller delen rymmer "
              "inte instanceCount hela instanser (MeshDraw::instances)";
        bit = kLogCustomStreams;
    } else if (d.userBytes != d.shader->userBytes() ||
               (d.userBytes > 0 && !d.user)) {
        why = "GlazeUser har inte programmets storlek (MeshDraw::user/userBytes "
              "mot ProgramDesc::userBytes)";
        bit = kLogCustomUser;
    } else if (!texturesOk(d)) {
        why = "texturerna är inte programmets: en plats under "
              "ShaderProgram::textures() saknar en giltig bild, eller en plats "
              "efter har en (MeshDraw::textures)";
        bit = kLogCustomTextures;
    } else if (!stateOk(d.state)) {
        why = "renderläget är ogiltigt (MeshDraw::state — en uppräkning utanför "
              "sina värden eller en bias som inte är ändlig)";
        bit = kLogState;
    }
    // Det bundna målets egen bild kan inte samplas medan den ritas i
    // (imageDrawOk loggar själv).
    if (!why)
        for (int i = 0; i < d.shader->textures(); ++i)
            if (!imageDrawOk(*d.textures[static_cast<std::size_t>(i)]))
                return false;
    return meshVerdict(d, why, bit);
}

std::string Renderer::programDescError(const ProgramDesc& d)
{
    if (d.userBytes % 16u != 0 || d.userBytes > kMaxProgramUserBytes)
        return programError(
            d, "har userBytes " + std::to_string(d.userBytes) +
                   " — GlazeUser-blocket är float4-packat (en multipel av 16) "
                   "och högst " +
                   std::to_string(kMaxProgramUserBytes) + " byte");
    if (d.textures > kMaxProgramTextures)
        return programError(d, "samplar " + std::to_string(d.textures) +
                                   " texturer — högst " +
                                   std::to_string(kMaxProgramTextures));
    for (const auto& [namn, värde] : d.defines) {
        if (!identifier(namn))
            return programError(d, "har definitionen '" + namn +
                                       "', som inte är en identifierare");
        if (värde.find_first_of("\r\n") != std::string::npos)
            return programError(d, "har definitionen " + namn +
                                       " med en radbrytning i värdet");
    }
    return {};
}

std::string Renderer::programError(const ProgramDesc& d, const std::string& vad)
{
    return "programmet '" + d.name + "' " + vad;
}

std::string Renderer::programSourceName(const ProgramDesc& d)
{
    if (d.name.empty())
        return "program";
    std::string ut = d.name;
    for (char& c : ut)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            c = '_';
    return ut;
}

// ------------------------------------------------------- bildernas regler
//
// En rad i bilden är w × pixelstorleken; stride 0 betyder täta rader. En
// stride som inte är en multipel av pixelstorleken går inte att uttrycka i
// GL:s UNPACK_ROW_LENGTH (pixlar, inte byte) — den avvisas därför överallt.

namespace {
bool strideOk(int w, ImageFormat f, std::size_t stride, std::size_t& rowBytes)
{
    const std::size_t bpp   = bytesPerPixel(f);
    const std::size_t tight = static_cast<std::size_t>(w) * bpp;
    rowBytes = stride ? stride : tight;
    return rowBytes >= tight && rowBytes % bpp == 0;
}
} // namespace

bool Renderer::imageDescOk(int w, int h, ImageFormat f, std::size_t stride,
                           std::size_t& rowBytes)
{
    const int max = maxTextureSize();
    if (w > 0 && h > 0 && w <= max && h <= max &&
        strideOk(w, f, stride, rowBytes))
        return true;
    if (!(sceneLogged_ & kLogImageDesc)) {
        sceneLogged_ |= kLogImageDesc;
        log("createImage: %d × %d med stride %zu — tom bild, större än GPU:ns "
            "största textur (%d) eller en stride som inte rymmer en rad i hela "
            "pixlar; bilden blir ogiltig",
            w, h, stride, max);
    }
    return false;
}

std::unique_ptr<Image> Renderer::createImage(int w, int h, ImageFormat f,
                                             const void* px, std::size_t stride)
{
    return createImage(w, h, f, px, stride, false);
}

bool Renderer::imageUpdateOk(const Image& img, int x, int y, int w, int h,
                             const void* px, std::size_t stride,
                             std::size_t& rowBytes)
{
    if (img.valid() && !img.premultiplied() && px && w > 0 && h > 0 &&
        x >= 0 && y >= 0 && w <= img.width() - x && h <= img.height() - y &&
        strideOk(w, img.format(), stride, rowBytes))
        return true;
    if (!(sceneLogged_ & kLogImageUpdate)) {
        sceneLogged_ |= kLogImageUpdate;
        log("updateImage: (%d, %d, %d × %d) med stride %zu i en bild på "
            "%d × %d — utanför, tom, fel stride, ogiltig eller ett render-mål; "
            "ingenting skrivs",
            x, y, w, h, stride, img.width(), img.height());
    }
    return false;
}

float Renderer::sceneImageFlags(const Image& img)
{
    return static_cast<float>((img.premultiplied() ? 1 : 0) |
                              (img.flippedV() ? 2 : 0) |
                              (img.format() == ImageFormat::R8 ? 4 : 0));
}

void Renderer::sceneImageAdjust(const SceneUniforms& u, float (&out)[4])
{
    out[0] = adjustFactor(u.saturation);
    out[1] = adjustFactor(u.contrast);
    out[2] = adjustFactor(u.brightness);
    out[3] = 0.0f;
}

// ------------------------------------------------------------ render-målen
//
// Klippstacken HÖR TILL MÅLET (renderer.h): fönstrets rektanglar är
// fönstrets pixlar, inte målets. pushTarget sparar den yttre stacken i
// målnivån och börjar tomt; popTarget lägger tillbaka den och tillämpar
// den igen — EFTER att det yttre målet bundits, så scissorn räknas mot rätt
// mål (GL räknar nerifrån mot det bundna målets höjd).

void Renderer::pushTarget(RenderTarget& t, Color c)
{
    targetStack_.push_back(TargetLevel{&t, std::move(clipStack_)});
    clipStack_.clear();
    bindTarget(&t);
    applyTop();   // inget klipp i det nya målet
    clear(c);
}

void Renderer::popTarget()
{
    if (targetStack_.empty()) {
        log("popTarget utan matchande pushTarget — ignoreras");
        return;
    }
    if (!clipStack_.empty())
        log("popTarget med %zu klipp kvar i målet — de kastas med målet",
            clipStack_.size());
    std::vector<ClipLevel> outer = std::move(targetStack_.back().outerClips);
    targetStack_.pop_back();
    bindTarget(targetStack_.empty() ? nullptr : targetStack_.back().target);
    clipStack_ = std::move(outer);
    applyTop();
}

void Renderer::beginFrameTargets()
{
    if (targetStack_.empty())
        return;
    // Ett programfel i förra framen: målen poppades aldrig. Det yttersta
    // klippet läggs tillbaka — som om de poppats, och samma läge som en
    // obalanserad pushClip lämnar — och stacken töms, så ingen pekare till
    // ett mål överlever in i en ny frame.
    log("en frame slutade med %zu bundna render-mål — pushTarget/popTarget "
        "är obalanserade",
        targetStack_.size());
    clipStack_ = std::move(targetStack_.front().outerClips);
    targetStack_.clear();
}

bool Renderer::imageDrawOk(const Image& img)
{
    if (targetStack_.empty() || &targetStack_.back().target->image() != &img)
        return true;
    if (!(sceneLogged_ & kLogTargetSelf)) {
        sceneLogged_ |= kLogTargetSelf;
        log("ett render-måls bild ritades i målet självt — den kan inte "
            "samplas medan den ritas i; ritas inte");
    }
    return false;
}

// ------------------------------------------- målens form och läsning (3D)

std::unique_ptr<RenderTarget> Renderer::createRenderTarget(int w, int h)
{
    return createRenderTarget(w, h, TargetDesc{});
}

bool Renderer::targetDescOk(int w, int h, const TargetDesc& d)
{
    const bool sizeOk    = w > 0 && h > 0;
    const bool samplesOk = d.samples == 1 || d.samples == 2 ||
                           d.samples == 4 || d.samples == 8;
    if (sizeOk && samplesOk)
        return true;
    if (!(sceneLogged_ & kLogTargetDesc)) {
        sceneLogged_ |= kLogTargetDesc;
        log("createRenderTarget: %d × %d med %d prov — %s; målet blir ogiltigt",
            w, h, d.samples,
            sizeOk ? "provtagningen är inte 1, 2, 4 eller 8"
                   : "måtten är inte positiva");
    }
    return false;
}

bool Renderer::readTargetOk(RenderTarget& t)
{
    const char*   why = nullptr;
    std::uint32_t bit = 0;
    if (!t.image().valid()) {
        why = "målet är ogiltigt";
        bit = kLogReadInvalid;
    } else if (targetInStack(t)) {
        why = "målet är bundet (eller ligger under ett nästlat mål) — läs det "
              "efter popTarget";
        bit = kLogReadBound;
    }
    if (!why)
        return true;
    if (!(sceneLogged_ & bit)) {
        sceneLogged_ |= bit;
        log("readTarget: %s; ingenting läses", why);
    }
    return false;
}

RenderTarget* Renderer::boundTarget() const
{
    return targetStack_.empty() ? nullptr : targetStack_.back().target;
}

bool Renderer::targetInStack(const RenderTarget& t) const
{
    for (const TargetLevel& lvl : targetStack_)
        if (lvl.target == &t)
            return true;
    return false;
}

// ------------------------------------------------------------- GPU-tiden

// Under brevlådans lås: Metal lämnar svaren ur sin completion handler, i en
// annan tråd än den som hämtar.
void Renderer::takeGpuTimes(std::vector<GpuTime>& out)
{
    std::lock_guard<std::mutex> lås(gpuTimes_->m);
    out.insert(out.end(), gpuTimes_->times.begin(), gpuTimes_->times.end());
    gpuTimes_->times.clear();
}

void Renderer::addGpuTime(GpuTimeBox& box, std::uint64_t serial,
                          std::uint64_t ns)
{
    std::lock_guard<std::mutex> lås(box.m);
    if (box.times.size() >= kMaxGpuTimes)
        box.times.erase(box.times.begin());   // ingen hämtar — de äldsta går
    box.times.push_back(GpuTime{serial, ns});
}

} // namespace gfx
