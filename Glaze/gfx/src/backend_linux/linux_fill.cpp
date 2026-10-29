// Fill-programmet i GlRenderer — spegel av
// d3d11_fill.cpp (läs dess filhuvud: kantremsan, utåt, speglingen); här bara
// GL-skillnaderna:
//   * Uniformerna är PER PROGRAM: egna locations och egen maskgeneration
//     (`syncClipMasks`), som Line- och Image-programmen.
//   * Fragmentets läge för klippmaskerna kommer ur varyingen `vPixH` =
//     (klipp.xy, klipp.w), PERSPEKTIVKORREKT interpolerad — då är
//     vPixH.xy / vPixH.z fragmentets px också där klipparen skurit en
//     triangel mot närplanet. En noperspective-px hade fått sitt värde ur en
//     punkt bakom kameran där. Aldrig gl_FragCoord (y-flippfällan, se
//     linux_renderer.cpp).
//   * z räknas om till GL:s klipprymd ([−w, w]) i vertexshadern.
//   * Rampen binds på enhet 0 med det LINJÄRA samplerobjektet; UI:ts flush
//     binder sina egna vid nästa ritning.

#include <algorithm>
#include <cstring>

#include "gfx/platform.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

const char kFillVsSrc[] = R"glsl(
uniform mat4 uViewProj;   // chunk-lokalt → pixelklipp
uniform vec4 uParams;     // .xy målets storlek (px)
in vec2 aPos;
in vec2 aPrev;
in vec2 aNext;
in vec4 aCol;
in uvec4 aKs;             // .x FillKind (0 början, 1 slut), .y sidan — heltal
out vec4 vCol;
noperspective out float vEdge;   // 0 på kanten → 1 i remsans ytterkant
out vec2 vWorld;                 // världspunkten (gradienten)
out vec3 vPixH;                  // (klipp.xy, klipp.w) — maskernas px

vec4 toClip(vec2 p) { return uViewProj * vec4(p, 0.0, 1.0); }
vec2 toPx(vec4 c) { return c.xy / max(c.w, 1e-6); }
vec4 toNdc(vec4 c)
{
    return vec4(c.x * 2.0 / uParams.x - c.w, c.w - c.y * 2.0 / uParams.y,
                2.0 * c.z - c.w, c.w);
}
vec4 toNear(vec4 a, vec4 b) { return mix(a, b, a.z / (a.z - b.z)); }

vec2 screenDir(vec4 a, vec4 b)
{
    if (a.z < 0.0 && b.z < 0.0)
        return vec2(0.0);
    if (a.z < 0.0)
        a = toNear(a, b);
    else if (b.z < 0.0)
        b = toNear(b, a);
    vec2  d = toPx(b) - toPx(a);
    float l = length(d);
    return l > 1e-6 ? d / l : vec2(0.0);
}

float mirrorSign()
{
    mat3 h = mat3(uViewProj[0].xyw, uViewProj[1].xyw, uViewProj[3].xyw);
    return determinant(h) < 0.0 ? -1.0 : 1.0;
}

void emit(vec4 c)
{
    gl_Position = toNdc(c);
    vPixH       = vec3(c.xy, c.w);
}

void main()
{
    vCol   = aCol;
    vWorld = aPos;
    vEdge  = 0.0;
    vec4 c = toClip(aPos);
    if (aKs.y == 0u) {
        emit(c);   // på kanten: som den är, rasteriseraren klipper
        return;
    }
    bool start = aKs.x == 0u;
    vec2 other = start ? aNext : aPrev;
    vec4 nb    = toClip(other);
    vec4 adj   = toClip(start ? aPrev : aNext);
    if (c.z < 0.0 && nb.z < 0.0) {
        emit(c);   // hela kanten bakom närplanet: klipps bort
        return;
    }
    vec2 e = start ? screenDir(c, nb) : screenDir(nb, c);
    vec2 g = start ? screenDir(adj, c) : screenDir(c, adj);
    if (c.z < 0.0) {
        float t = c.z / (c.z - nb.z);
        c      = mix(c, nb, t);
        vWorld = mix(aPos, other, t);
        g      = vec2(0.0);
    }
    float s   = mirrorSign();
    vec2  ne  = s * vec2(e.y, -e.x);   // utåt = höger om kanten
    vec2  ng  = s * vec2(g.y, -g.x);
    vec2  off = ne;
    vec2  m   = ne + ng;
    if (dot(g, g) > 0.0 && dot(m, m) > 1e-6) {
        vec2 mn = normalize(m);
        off     = mn / max(dot(mn, ne), 0.25);
    }
    emit(vec4((toPx(c) + off) * c.w, c.z, c.w));
    vEdge = 1.0;
}
)glsl";

// Fragmentshadern — klippmaskernas och färgens delar klistras FÖRE.
const char kFillFsSrc[] = R"glsl(
in vec4 vCol;
noperspective in float vEdge;
in vec2 vWorld;
in vec3 vPixH;
out vec4 frag;

void main()
{
    vec4  col = vCol * paintColor(vWorld);
    float cov = 1.0 - clamp(vEdge, 0.0, 1.0);
    vec2  pix = vPixH.xy / vPixH.z;
    frag = vec4(col.rgb,
                col.a * cov * uPaint1.y * clipCoverage(pix) * sceneClip(pix));
}
)glsl";

const void* byteOffset(std::size_t o)
{
    return reinterpret_cast<const void*>(o);
}

} // namespace

// Färgens uniformer och rampens bindning — delade av Fill-, Line- och
// Glyph-programmen (locations per program).
void GlRenderer::setPaint(const MeshDraw& d, int paint0Loc, int paint1Loc)
{
    const ScenePaint& p = d.uniforms.paint;
    const bool radial   = p.kind == PaintKind::Radial;
    pglUniform4f(paint0Loc, p.x0, p.y0, radial ? p.r0 : p.x1,
                 radial ? p.r1 : p.y1);
    pglUniform4f(paint1Loc, static_cast<float>(p.kind),
                 std::clamp(d.uniforms.opacity, 0.0f, 1.0f), 0.0f, 0.0f);
    // Rampen (eller den vita texturen när färgen är enfärgad — programmet
    // samplar den då inte) med det LINJÄRA samplerobjektet.
    const GlTexture ramp =
        p.kind != PaintKind::Solid && d.image
            ? static_cast<const GlImage*>(d.image)->tex()
            : whiteTex_;
    glBindTexture(GL_TEXTURE_2D, ramp);
    bindSampler(ImageFilter::Linear);
}

bool GlRenderer::initFillProgram()
{
    const StatTimer klocka(stats_.compileNs);   // programbygget
    const char* vsParts[] = {kGlslVersion, kFillVsSrc};
    const char* fsParts[] = {kGlslVersion, kGlslClipSrc, kGlslPaintSrc,
                             kFillFsSrc};
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, vsParts, 2);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, fsParts, 4);
    if (!vs || !fs) {
        if (vs)
            pglDeleteShader(vs);
        if (fs)
            pglDeleteShader(fs);
        log("GL: Fill-programmet kunde inte kompileras — drawMesh ritar inga "
            "ytor");
        return false;
    }
    // Attributen i FillVertex ordning (gfx/scene.h).
    static const char* const kAttribs[] = {"aPos", "aPrev", "aNext", "aCol",
                                           "aKs"};
    fillProgram_ = linkGlProgram(vs, fs, kAttribs, 5);
    if (!fillProgram_) {
        log("GL: Fill-programmet kunde inte länkas — drawMesh ritar inga ytor");
        return false;
    }
    fillViewProjLoc_ = pglGetUniformLocation(fillProgram_, "uViewProj");
    fillParamsLoc_   = pglGetUniformLocation(fillProgram_, "uParams");
    fillPaint0Loc_   = pglGetUniformLocation(fillProgram_, "uPaint0");
    fillPaint1Loc_   = pglGetUniformLocation(fillProgram_, "uPaint1");
    fillClipP0Loc_   = pglGetUniformLocation(fillProgram_, "uClipP0");
    fillClipP1Loc_   = pglGetUniformLocation(fillProgram_, "uClipP1");
    fillClipNLoc_    = pglGetUniformLocation(fillProgram_, "uClipN");
    const int  rampLoc = pglGetUniformLocation(fillProgram_, "uRamp");
    const bool klipp   = findSceneClip(fillProgram_, fillSceneClip_);
    // Samma regel som de andra programmen: en location som saknas betyder att
    // källan och C++-sidan glidit isär — glUniform hade tyst svalt -1.
    if (fillViewProjLoc_ < 0 || fillParamsLoc_ < 0 || fillPaint0Loc_ < 0 ||
        fillPaint1Loc_ < 0 || fillClipP0Loc_ < 0 || fillClipP1Loc_ < 0 ||
        fillClipNLoc_ < 0 || rampLoc < 0 || !klipp) {
        log("GL: Fill-programmets uniformer saknas (uViewProj=%d uParams=%d "
            "uPaint0=%d uPaint1=%d uClipP0=%d uClipP1=%d uClipN=%d uRamp=%d "
            "uSceneClip=%d uSceneClipN=%d) — drawMesh ritar inga ytor",
            fillViewProjLoc_, fillParamsLoc_, fillPaint0Loc_, fillPaint1Loc_,
            fillClipP0Loc_, fillClipP1Loc_, fillClipNLoc_, rampLoc,
            fillSceneClip_.planes, fillSceneClip_.count);
        pglDeleteProgram(fillProgram_);
        fillProgram_ = 0;
        return false;
    }
    pglUseProgram(fillProgram_);
    pglUniform1i(rampLoc, 0);   // enhet 0, som UI:ts tex0

    pglGenVertexArrays(1, &fillVao_);
    pglBindVertexArray(fillVao_);
    for (unsigned i = 0; i < 5; ++i)
        pglEnableVertexAttribArray(i);
    // UI-pipelinen är den bundna (init sker före första framen).
    pglUseProgram(program_);
    pglBindVertexArray(vao_);
    return true;
}

void GlRenderer::shutdownFillProgram()
{
    // Kontexten är aktuell (shutdownRenderer).
    if (fillVao_)
        pglDeleteVertexArrays(1, &fillVao_);
    if (fillProgram_)
        pglDeleteProgram(fillProgram_);
    fillVao_        = 0;
    fillProgram_    = 0;
    fillClipGen_    = 0;
    fillSceneClip_  = SceneClipLocs{};
}

void GlRenderer::drawFillMesh(const MeshDraw& d)
{
    if (!ready() || !meshDrawOk(d, fillProgram_ != 0))
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Fill) {
        pglUseProgram(fillProgram_);
        pglBindVertexArray(fillVao_);
        pipeline_ = Pipeline::Fill;
        ++stats_.programSwitches;
    }
    syncClipMasks(fillClipP0Loc_, fillClipP1Loc_, fillClipNLoc_, fillClipGen_);
    setSceneClip(d, fillSceneClip_);

    // Kameran och färgen — per anrop, räknas inte som uppladdning.
    pglUniformMatrix4fv(fillViewProjLoc_, 1, GL_FALSE, d.uniforms.viewProj);
    pglUniform4f(fillParamsLoc_, static_cast<float>(targetW_),
                 static_cast<float>(targetH_), 0.0f, 0.0f);
    setPaint(d, fillPaint0Loc_, fillPaint1Loc_);

    const auto& vb = static_cast<const GlGpuBuffer&>(*d.vertices.buffer);
    const auto& ib = static_cast<const GlGpuBuffer&>(*d.indices.buffer);
    const std::size_t base   = d.vertices.offset;
    const auto        stride = static_cast<GLsizei>(sizeof(FillVertex));
    pglBindBuffer(GL_ARRAY_BUFFER, vb.id());
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(FillVertex, x)));
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(FillVertex, prevX)));
    pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(FillVertex, nextX)));
    pglVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                           byteOffset(base + offsetof(FillVertex, color)));
    // slaget och sidan som HELTAL (0/1) — `R8G8B8A8_UINT` som i D3D11; ett
    // skalat format räknas om på CPU:n av Mesas d3d12 (linux_scene.cpp).
    pglVertexAttribIPointer(4, 4, GL_UNSIGNED_BYTE, stride,
                            byteOffset(base + offsetof(FillVertex, kind)));
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.id());   // VAO:ns tillstånd
    applyMeshState(d.state);   // renderläget
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount),
                   GL_UNSIGNED_INT, byteOffset(d.indices.offset));
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
