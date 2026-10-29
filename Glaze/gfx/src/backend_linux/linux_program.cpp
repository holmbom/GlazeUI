// Egna program i Linux-backenden (gfx/program.h) — spegel
// av d3d11_program.cpp: appens GLSL kompilerad i körtid med `#version 330
// core`, definitionerna och FÖRSPELET före, och `MeshProgram::Custom` —
// indexerat eller inte, instansierat eller inte, med strömmarnas form per
// rit-anrop. GL-skillnaderna:
//
//   * GLSL 330 saknar layout(binding): blocken binds på NAMN
//     (glUniformBlockBinding — GlazeFrame på punkt 3, GlazeUser på 4) och
//     samplers på namn (GlazeTexture0–3 får enhet 0–3) när programmet skapas.
//   * Platserna är layout(location = N) i källan; programmet länkas utan
//     glBindAttribLocation. Reflektionen (de aktiva attributen) ger platserna
//     och slaget — ett heltalsattribut går med glVertexAttribIPointer
//     (glVertexAttribPointer gör annars om heltalet till flyttal).
//   * GL 3.3 har ingen baseInstance: instansdelen binds med PEKAR-OFFSET och
//     glVertexAttribDivisor 1. gl_InstanceID räknar från delens början.
//   * EN VAO för alla egna program, med attributen påslagna efter layouten —
//     helskärmspasset ritar med alla avslagna, alltså en tom VAO (core-
//     profilen kräver en bunden VAO också utan attribut).
//   * Blocken skrivs om per anrop med glBufferData (orphan), uppladdade via
//     GL_UNIFORM_BUFFER och bundna med glBindBufferBase.

#include <algorithm>
#include <cstring>
#include <iterator>

#include "gl_common.h"
#include "gfx/platform.h"
#include "linux_backend.h"

namespace gfx {
namespace {

// Bindningspunkterna — samma tal som D3D11:s register (b3, b4).
constexpr GLuint kFrameBinding = 3;
constexpr GLuint kUserBinding  = 4;

// FÖRSPELET (gfx/program.h) — efter `#version` och definitionerna, före
// `#line 1` och appens källa. GlazeFrame som `glaze` (std140, samma form som
// FrameUniforms) och glaze_toClip — och därefter färgjusteringen
// (`kGlslAdjustSrc`, gl_common.cpp) i båda stegen.
const char kGlslPrelude[] = R"glsl(
layout(std140) uniform GlazeFrame
{
    mat4 viewProj;
    mat4 invViewProj;
    vec4 eye;
    vec4 forward;
    vec4 right;
    vec4 up;
    vec4 target;
    vec4 scale;
    vec4 pixelSize;
} glaze;
// Kamerans pixelklipp (x/w och y/w i målets px, z/w i [0, 1]) → GL:s
// klipprymd: x och y som D3D:s, z ur [0, w] till [−w, w]. Linjär — w går
// orört vidare till rasteriseringen.
vec4 glaze_toClip(vec4 c)
{
    return vec4(c.x * 2.0 * glaze.target.z - c.w,
                c.w - c.y * 2.0 * glaze.target.w, 2.0 * c.z - c.w, c.w);
}
)glsl";

const void* byteOffset(std::size_t o)
{
    return reinterpret_cast<const void*>(o);
}

// Programmets löpnummer — aldrig återanvänt, till skillnad från en adress.
std::uint64_t nextProgramSerial()
{
    static std::uint64_t n = 0;
    return ++n;
}

// Hur många platser ett attribut av typen tar — en matris en per kolumn.
int locationsOf(GLenum type)
{
    switch (type) {
    case GL_FLOAT_MAT2: case GL_FLOAT_MAT2x3: case GL_FLOAT_MAT2x4: return 2;
    case GL_FLOAT_MAT3: case GL_FLOAT_MAT3x2: case GL_FLOAT_MAT3x4: return 3;
    case GL_FLOAT_MAT4: case GL_FLOAT_MAT4x2: case GL_FLOAT_MAT4x3: return 4;
    default:                                                       return 1;
    }
}

bool integerType(GLenum type)
{
    switch (type) {
    case GL_INT: case GL_INT_VEC2: case GL_INT_VEC3: case GL_INT_VEC4:
    case GL_UNSIGNED_INT: case GL_UNSIGNED_INT_VEC2:
    case GL_UNSIGNED_INT_VEC3: case GL_UNSIGNED_INT_VEC4:
        return true;
    default:
        return false;
    }
}

bool samplerType(GLenum type)
{
    switch (type) {
    case GL_SAMPLER_1D: case GL_SAMPLER_2D: case GL_SAMPLER_3D:
    case GL_SAMPLER_CUBE: case GL_SAMPLER_2D_RECT: case GL_SAMPLER_1D_SHADOW:
    case GL_SAMPLER_2D_SHADOW: case GL_SAMPLER_2D_RECT_SHADOW:
    case GL_SAMPLER_1D_ARRAY: case GL_SAMPLER_2D_ARRAY:
    case GL_SAMPLER_CUBE_SHADOW: case GL_SAMPLER_BUFFER:
    case GL_SAMPLER_2D_MULTISAMPLE: case GL_SAMPLER_2D_MULTISAMPLE_ARRAY:
    case GL_INT_SAMPLER_2D: case GL_INT_SAMPLER_3D: case GL_INT_SAMPLER_CUBE:
    case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D:
    case GL_UNSIGNED_INT_SAMPLER_3D: case GL_UNSIGNED_INT_SAMPLER_CUBE:
    case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
        return true;
    default:
        return false;
    }
}

// VERTEXSTEGETS INDATA: de aktiva attributen, deras platser och slag. Ett
// inbyggt (gl_VertexID, gl_InstanceID) har ingen plats och läser ingen
// ström. Svarar med felet utan programmets namn, tomt när de duger.
std::string attribReflection(unsigned prog, std::uint32_t& inputs,
                             std::uint32_t& integer)
{
    inputs = integer = 0;
    GLint n = 0;
    pglGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &n);
    for (GLint i = 0; i < n; ++i) {
        char    name[256] = {};
        GLsizei len       = 0;
        GLint   size      = 0;
        GLenum  type      = 0;
        pglGetActiveAttrib(prog, static_cast<GLuint>(i), sizeof name, &len,
                           &size, &type, name);
        const GLint loc = pglGetAttribLocation(prog, name);
        if (loc < 0)
            continue;
        const int count = std::max(size, 1) * locationsOf(type);
        for (int k = 0; k < count; ++k) {
            const int at = loc + k;
            if (at >= attrib::kCount)
                return std::string("läser attributet ") + name + " på plats " +
                       std::to_string(at) + " — platserna är 0–" +
                       std::to_string(attrib::kCount - 1);
            inputs |= 1u << at;
            if (integerType(type))
                integer |= 1u << at;
        }
    }
    return {};
}

// SAMPLERNA: varje aktiv sampler heter GlazeTexture<i> med i under
// programmets texturantal — en annan sampler hade fått enhet 0 och tyst
// samplat fel bild. Svarar med felet, och platsernas locations i `loc`.
std::string samplerReflection(unsigned prog, int textures,
                              int (&loc)[kMaxProgramTextures])
{
    for (int& l : loc)
        l = -1;
    GLint n = 0;
    pglGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n; ++i) {
        char    name[256] = {};
        GLsizei len       = 0;
        GLint   size      = 0;
        GLenum  type      = 0;
        pglGetActiveUniform(prog, static_cast<GLuint>(i), sizeof name, &len,
                            &size, &type, name);
        if (!samplerType(type))
            continue;
        int slot = -1;
        for (int s = 0; s < kMaxProgramTextures; ++s)
            if (std::strcmp(name, ("GlazeTexture" + std::to_string(s)).c_str()) ==
                0)
                slot = s;
        if (slot < 0)
            return std::string("har samplern ") + name +
                   " — ett eget programs samplers heter GlazeTexture0–" +
                   std::to_string(kMaxProgramTextures - 1);
        if (slot >= textures)
            return std::string("samplar ") + name +
                   ", men ProgramDesc::textures är " + std::to_string(textures);
        loc[slot] = pglGetUniformLocation(prog, name);
    }
    return {};
}

// LÖSA UNIFORMER OCH ANDRA BLOCK: ett eget programs data går i GlazeUser —
// en uniform utanför ett block (utom samplerna) eller ett block utöver
// GlazeFrame och GlazeUser hade aldrig fått något värde: nollor, tyst.
std::string uniformReflection(unsigned prog)
{
    GLint n = 0;
    pglGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);
    for (GLint i = 0; i < n; ++i) {
        const GLuint idx   = static_cast<GLuint>(i);
        GLint        block = -1;
        pglGetActiveUniformsiv(prog, 1, &idx, GL_UNIFORM_BLOCK_INDEX, &block);
        if (block >= 0)
            continue;
        char    name[256] = {};
        GLsizei len       = 0;
        GLint   size      = 0;
        GLenum  type      = 0;
        pglGetActiveUniform(prog, idx, sizeof name, &len, &size, &type, name);
        if (!samplerType(type))
            return std::string("har uniformen ") + name +
                   " utanför ett block — ett eget programs data går i "
                   "GlazeUser";
    }
    GLint blocks = 0;
    pglGetProgramiv(prog, GL_ACTIVE_UNIFORM_BLOCKS, &blocks);
    const int kända =
        (pglGetUniformBlockIndex(prog, "GlazeFrame") != GL_INVALID_INDEX) +
        (pglGetUniformBlockIndex(prog, "GlazeUser") != GL_INVALID_INDEX);
    if (blocks > kända)
        return "har ett uniform-block utöver GlazeFrame och GlazeUser — det "
               "hade aldrig fått något värde";
    return {};
}

// Programmet: det länkade programmet och reflektionen. Destruktorn raderar
// det genom renderern (fontens livstidsregel — släpp programmet före
// fönstret).
class GlShaderProgram final : public ShaderProgram {
public:
    GlShaderProgram(GlRenderer* r, const ProgramDesc& d)
        : ShaderProgram(d), serial(nextProgramSerial()), renderer_(r)
    {
    }
    ~GlShaderProgram() override
    {
        if (prog && renderer_)
            renderer_->destroyProgram(prog);
    }
    GlShaderProgram(const GlShaderProgram&)            = delete;
    GlShaderProgram& operator=(const GlShaderProgram&) = delete;

    bool               valid() const override { return prog != 0; }
    const std::string& error() const override { return err; }

    // Vertexstegets indata (`ShaderProgram::inputs`) — ur de aktiva attributen.
    void reflected(std::uint32_t inputs, std::uint32_t integer)
    {
        setInputs(inputs, integer);
    }

    const std::uint64_t serial;
    std::string         err;
    unsigned            prog = 0;

private:
    GlRenderer* renderer_;
};

} // namespace

// ------------------------------------------------------------------ livet

bool GlRenderer::initProgramSupport()
{
    // Kontexten är aktuell (init). VAO:n och de två uniform-buffertarna; UI:ts
    // VAO binds tillbaka — UI-pipelinen är den bundna efter init.
    pglGenVertexArrays(1, &customVao_);
    pglGenBuffers(1, &frameUbo_);
    pglGenBuffers(1, &userUbo_);
    pglBindVertexArray(vao_);
    if (!customVao_ || !frameUbo_ || !userUbo_) {
        log("GL: de egna programmens VAO och block kunde inte skapas — drawMesh "
            "ritar inga egna program");
        shutdownProgramSupport();
        return false;
    }
    return true;
}

void GlRenderer::shutdownProgramSupport()
{
    // Kontexten är aktuell (shutdownRenderer).
    if (customVao_)
        pglDeleteVertexArrays(1, &customVao_);
    if (frameUbo_)
        pglDeleteBuffers(1, &frameUbo_);
    if (userUbo_)
        pglDeleteBuffers(1, &userUbo_);
    customVao_     = 0;
    frameUbo_      = 0;
    userUbo_       = 0;
    customEnabled_ = 0;
    curProgram_    = 0;
}

void GlRenderer::destroyProgram(unsigned prog)
{
    if (!prog || !ready())
        return;
    makeCurrent();
    pglDeleteProgram(prog);   // ett bundet program raderas när det lämnas
}

// ---------------------------------------------------------------- program

std::unique_ptr<ShaderProgram> GlRenderer::createProgram(const ProgramDesc& d)
{
    auto        p   = std::make_unique<GlShaderProgram>(this, d);
    std::string err = programDescError(d);
    if (err.empty() && (d.glslVertex.empty() || d.glslFragment.empty()))
        err = programError(d, std::string("saknar GLSL-källa (Linux) — ") +
                                  (d.glslVertex.empty() ? "glslVertex"
                                                        : "glslFragment") +
                                  " är tom, och GL kräver båda stegen");
    if (err.empty() && !makeCurrent())
        err = programError(d, "kunde inte skapas — ingen GL-kontext");

    unsigned prog = 0;
    if (err.empty()) {
        // Kompileringen och länkningen (`RenderStats::compileNs`)
        // — GL har ingen cache, så varje program kostar.
        const StatTimer klocka(stats_.compileNs);
        // #version, definitionerna, förspelet, #line 1 och appens källa —
        // glShaderSource tar delarna i ordning.
        std::string defs;
        for (const auto& [namn, värde] : d.defines)
            defs += "#define " + namn + (värde.empty() ? "" : " " + värde) +
                    "\n";
        // Delarna RÄKNAS (`std::size`): en ny del i förspelet hade annars
        // tappat appens källa i slutet.
        const char* vsParts[] = {kGlslVersion,   defs.c_str(), kGlslPrelude,
                                 kGlslAdjustSrc, kGlslStandSrc, "#line 1\n",
                                 d.glslVertex.c_str()};
        const char* fsParts[] = {kGlslVersion,   defs.c_str(), kGlslPrelude,
                                 kGlslAdjustSrc, kGlslStandSrc, "#line 1\n",
                                 d.glslFragment.c_str()};
        std::string    text;
        const unsigned vs =
            compileGlShader(GL_VERTEX_SHADER, vsParts,
                            static_cast<int>(std::size(vsParts)), &text);
        if (!vs) {
            err = programError(d, "kunde inte kompileras (GLSL, vertexsteget):\n" +
                                      text);
        } else {
            const unsigned fs =
                compileGlShader(GL_FRAGMENT_SHADER, fsParts,
                                static_cast<int>(std::size(fsParts)), &text);
            if (!fs) {
                pglDeleteShader(vs);
                err = programError(
                    d, "kunde inte kompileras (GLSL, fragmentsteget):\n" + text);
            } else {
                prog = linkGlProgram(vs, fs, nullptr, 0, &text);
                if (!prog)
                    err = programError(d, "kunde inte länkas (GLSL):\n" + text);
            }
        }
    }

    std::uint32_t inputs = 0, integer = 0;
    int           texLoc[kMaxProgramTextures] = {};   // samplerReflection
    if (err.empty()) {
        std::string vad = attribReflection(prog, inputs, integer);
        if (vad.empty())
            vad = samplerReflection(prog, d.textures, texLoc);
        if (vad.empty())
            vad = uniformReflection(prog);
        // Blocken på NAMN. Ett block programmet inte använder har tagits bort
        // av länkaren — då finns inget att binda. GlazeUser ska vara exakt
        // ProgramDesc::userBytes stort (std140).
        const GLuint fi = pglGetUniformBlockIndex(prog, "GlazeFrame");
        if (vad.empty() && fi != GL_INVALID_INDEX)
            pglUniformBlockBinding(prog, fi, kFrameBinding);
        const GLuint ui = pglGetUniformBlockIndex(prog, "GlazeUser");
        if (vad.empty() && ui != GL_INVALID_INDEX) {
            GLint size = 0;
            pglGetActiveUniformBlockiv(prog, ui, GL_UNIFORM_BLOCK_DATA_SIZE,
                                       &size);
            if (static_cast<std::uint32_t>(size) != d.userBytes)
                vad = "har GlazeUser på " + std::to_string(size) +
                      " byte i GLSL-källan, men ProgramDesc::userBytes är " +
                      std::to_string(d.userBytes);
            else
                pglUniformBlockBinding(prog, ui, kUserBinding);
        }
        if (!vad.empty())
            err = programError(d, vad);
    }
    if (err.empty()) {
        // Samplerna till sina enheter. glUniform1i kräver programmet bundet —
        // det som var bundet (UI:ts eller scenens) binds tillbaka direkt.
        GLint bound = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &bound);
        pglUseProgram(prog);
        for (int s = 0; s < kMaxProgramTextures; ++s)
            if (texLoc[s] >= 0)
                pglUniform1i(texLoc[s], s);
        pglUseProgram(static_cast<GLuint>(bound));
    }

    if (!err.empty()) {
        if (prog)
            pglDeleteProgram(prog);
        p->err = err;
        log("%s", err.c_str());   // EN gång — här, aldrig per rit-anrop
        return p;
    }
    p->prog = prog;
    p->reflected(inputs, integer);
    ++stats_.programsBuilt;
    return p;
}

// ---------------------------------------------------------------- ritning

void GlRenderer::drawCustomMesh(const MeshDraw& d)
{
    if (!ready() || !meshDrawOk(d, customVao_ != 0))
        return;
    // Programmet kommer alltid ur den här backenden (createProgram) — casten
    // är säker per konstruktion, som drawText:s Font.
    const auto&         p = static_cast<const GlShaderProgram&>(*d.shader);
    const VertexLayout& l = *d.layout;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Custom || curProgram_ != p.serial) {
        pglUseProgram(p.prog);
        pglBindVertexArray(customVao_);
        pipeline_   = Pipeline::Custom;
        curProgram_ = p.serial;
        ++stats_.programSwitches;
    }

    // GlazeFrame: anroparens block med det BUNDNA målets storlek i `target`.
    FrameUniforms f = d.frame;
    f.target[0]     = static_cast<float>(targetW_);
    f.target[1]     = static_cast<float>(targetH_);
    f.target[2]     = targetW_ > 0 ? 1.0f / static_cast<float>(targetW_) : 0.0f;
    f.target[3]     = targetH_ > 0 ? 1.0f / static_cast<float>(targetH_) : 0.0f;
    pglBindBuffer(GL_UNIFORM_BUFFER, frameUbo_);
    pglBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(sizeof f), &f,
                  GL_STREAM_DRAW);
    pglBindBufferBase(GL_UNIFORM_BUFFER, kFrameBinding, frameUbo_);
    // GlazeUser — exakt programmets storlek (meshDrawOk har prövat den).
    if (d.userBytes > 0) {
        pglBindBuffer(GL_UNIFORM_BUFFER, userUbo_);
        pglBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(d.userBytes),
                      d.user, GL_STREAM_DRAW);
        pglBindBufferBase(GL_UNIFORM_BUFFER, kUserBinding, userUbo_);
    }

    // Texturerna på enhet 0… med var sin bilds filter — samplerobjektet
    // skuggar texturens parametrar, så det binds per textur och anrop. Enhet
    // 0 görs aktiv igen, som resten av renderern förutsätter.
    for (int i = 0; i < p.textures(); ++i) {
        const auto& im =
            static_cast<const GlImage&>(*d.textures[static_cast<std::size_t>(i)]);
        pglActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(GL_TEXTURE_2D, im.tex());
        pglBindSampler(static_cast<GLuint>(i),
                       im.filter() == ImageFilter::Linear ? samplerLinear_
                                                          : samplerNearest_);
    }
    pglActiveTexture(GL_TEXTURE0);

    // Strömmarna: attributen på och av efter layouten, pekarna med delens
    // offset — instansdelen också (ingen baseInstance i GL 3.3), med
    // divisorn 1. Heltalen går som heltal (glVertexAttribIPointer).
    std::uint32_t want = 0;
    for (const VertexAttrib& a : l.attribs)
        want |= 1u << a.location;
    for (GLuint loc = 0; loc < attrib::kCount; ++loc) {
        const std::uint32_t bit = 1u << loc;
        if ((want & bit) && !(customEnabled_ & bit))
            pglEnableVertexAttribArray(loc);
        else if (!(want & bit) && (customEnabled_ & bit))
            pglDisableVertexAttribArray(loc);
    }
    customEnabled_ = want;
    for (const VertexAttrib& a : l.attribs) {
        const BufferSlice& s      = a.stream ? d.instances : d.vertices;
        const auto&        buf    = static_cast<const GlGpuBuffer&>(*s.buffer);
        const GLsizei      stride = l.stride[a.stream];
        const void*        ptr    = byteOffset(s.offset + a.offset);
        pglBindBuffer(GL_ARRAY_BUFFER, buf.id());
        switch (a.format) {
        case AttribFormat::Float:
        case AttribFormat::Float2:
        case AttribFormat::Float3:
        case AttribFormat::Float4:
            pglVertexAttribPointer(a.location,
                                   static_cast<GLint>(attribBytes(a.format) / 4),
                                   GL_FLOAT, GL_FALSE, stride, ptr);
            break;
        case AttribFormat::UNorm8x4:
            pglVertexAttribPointer(a.location, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                                   stride, ptr);
            break;
        case AttribFormat::SNorm8x4:
            pglVertexAttribPointer(a.location, 4, GL_BYTE, GL_TRUE, stride, ptr);
            break;
        case AttribFormat::UInt8x4:
            pglVertexAttribIPointer(a.location, 4, GL_UNSIGNED_BYTE, stride, ptr);
            break;
        case AttribFormat::UInt:
            pglVertexAttribIPointer(a.location, 1, GL_UNSIGNED_INT, stride, ptr);
            break;
        }
        pglVertexAttribDivisor(a.location, a.stream);
    }
    applyMeshState(d.state);   // renderläget

    const GLsizei inst  = static_cast<GLsizei>(d.instanceCount);
    std::uint32_t count = 0;
    if (d.indexCount > 0) {
        count         = d.indexCount;
        const auto& i = static_cast<const GlGpuBuffer&>(*d.indices.buffer);
        pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i.id());   // VAO:ns tillstånd
        if (inst > 0)
            pglDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(count),
                                     GL_UNSIGNED_INT,
                                     byteOffset(d.indices.offset), inst);
        else
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count),
                           GL_UNSIGNED_INT, byteOffset(d.indices.offset));
    } else {
        count = d.vertexCount;
        if (inst > 0)
            pglDrawArraysInstanced(GL_TRIANGLES, 0,
                                   static_cast<GLsizei>(count), inst);
        else
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
    }
    ++stats_.drawCalls;
    stats_.vertices +=
        static_cast<std::uint64_t>(count) * std::max<std::uint32_t>(d.instanceCount, 1u);
    stats_.instances += d.instanceCount;
}

} // namespace gfx
