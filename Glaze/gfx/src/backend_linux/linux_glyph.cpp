// Glyph-programmet i GlRenderer — och dess
// Glyph3-variant (källan med GLAZE_POS3, ankaret i xyz) —
// spegel av d3d11_glyph.cpp (läs dess filhuvud: billboards, fältet,
// formerna och den liggande texten); här bara
// GL-skillnaderna:
//   * Uniformerna är per program; färgens del (kGlslPaintSrc) ger uPaint0/
//     uPaint1 och sampler-enheten — som här bär ATLASSIDAN, inte en ramp.
//   * Flaggorna och etikettens index kommer som HELTAL (glVertexAttribIPointer
//     — `R32_UINT` som i D3D11). Som skalade flyttal räknade Mesas
//     d3d12-drivrutin (WSLg) om varje ritad vertex på CPU:n.
//   * Fragmentets läge för maskerna är `vPix` (noperspective): quadens fyra
//     hörn delar ankarets w, och ett ankare bakom närplanet gallras helt —
//     ingen triangel klipps mot närplanet.
//   * ETIKETTERNAS SYNLIGHET ligger på ENHET 1 (`uLabels`) och läses
//     med texelFetch i vertexshadern; indexet är ett heltalsattribut och
//     basen ligger i uParams.w (ett flyttal, exakt under 2²⁴), −1 när ingen
//     är bunden.
//   * GOLVET OCH TAKET för en em ligger i uPaint1.x/.w, som
//     d3d11:s gPaint1 — fysiska px, 0 = ingen gräns.

#include <algorithm>
#include <cstring>

#include "gfx/platform.h"
#include "gfx/sdf.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

const char kGlyphVsSrc[] = R"glsl(
uniform mat4 uViewProj;   // chunk-lokalt → pixelklipp
uniform vec4 uParams;     // .xy målets storlek (px), .z px per storleksenhet,
                          // .w etiketternas bas (−1 = ingen synlighet bunden)
uniform sampler2D uLabels;   // enhet 1: en R8-texel per etikett
// Färgdelens uniformer (kGlslPaintSrc, fragmentshadern) — bildmarkörernas
// proportioner (.w) och flaggor (.z) läses också här, och uPaint1.x/.w är
// storlekens golv och tak i px.
uniform vec4 uPaint0;
uniform vec4 uPaint1;
// GLYPH3: samma källa med GLAZE_POS3 — ankaret i xyz.
#ifdef GLAZE_POS3
in vec3  aAnchor;
#else
in vec2  aAnchor;
#endif
in vec2  aOff;            // hörnet från ankaret, em (y nedåt)
in vec2  aUv;             // atlas-UV, eller formens läge (em)
in vec4  aCol;
in vec4  aHalo;
in float aSize;           // em i enheten
in float aAngle;          // radianer, moturs
in float aHaloW;          // glorians/konturens bredd i enheten
in uint  aFlags;
in uint  aLabel;          // etikettens index i bygget
out vec2 vUv;
flat out vec4 vCol;
flat out vec4 vHalo;
flat out vec4 vShape;     // .xy formens halva mått (em), .z storleken i px,
                          // .w glorians bredd i em
flat out int  vFlags;
flat out float vVis;      // etikettens synlighet 0–1
noperspective out vec2 vPix;

// Synligheten: 1 utan bunden synlighet, annars texeln på basen + indexet,
// rad för rad.
float visibility()
{
    if (uParams.w < 0.0)
        return 1.0;
    int w = textureSize(uLabels, 0).x;
    int i = int(uParams.w + 0.5) + int(aLabel);
    return texelFetch(uLabels, ivec2(i % w, i / w), 0).r;
}

vec4 toNdc(vec4 c)
{
    return vec4(c.x * 2.0 / uParams.x - c.w, c.w - c.y * 2.0 / uParams.y,
                2.0 * c.z - c.w, c.w);
}

// Världsriktningen v som skärmens riktning vid ankaret (pixelklippet c) —
// d3d11_glyph.cpp:s glyphScreenDir.
vec2 screenDir(vec4 c, vec3 v)
{
    vec4 d = uViewProj * vec4(v, 0.0);
    return (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
}

// LIGGANDE TEXT (glyphflag::kGroundAligned) —
// d3d11_glyph.cpp:s glyphGround: hörnet i världsplanet vid ankaret, och
// pixelklippet ut (vPix räknas av anroparen ur det).
#ifdef GLAZE_POS3
vec4 groundCorner(vec3 anchor, vec4 c, vec2 off, float sizePx, out float sizeEff)
#else
vec4 groundCorner(vec2 anchor, vec4 c, vec2 off, float sizePx, out float sizeEff)
#endif
{
    vec3 ew = vec3(cos(aAngle), sin(aAngle), 0.0);
    vec3 dw = vec3(-ew.y, ew.x, 0.0);
    vec2 es = screenDir(c, ew);
    vec2 ds = screenDir(c, dw);
    if (es.x * ds.y - es.y * ds.x < 0.0) {
        dw = -dw;
        ds = -ds;
    }
    vec3 fw = vec3((uViewProj * vec4(1.0, 0.0, 0.0, 0.0)).w,
                   (uViewProj * vec4(0.0, 1.0, 0.0, 0.0)).w,
                   (uViewProj * vec4(0.0, 0.0, 1.0, 0.0)).w);
    if (dot(fw, fw) < 1e-20)
        fw = vec3((uViewProj * vec4(1.0, 0.0, 0.0, 0.0)).z,
                  (uViewProj * vec4(0.0, 1.0, 0.0, 0.0)).z,
                  (uViewProj * vec4(0.0, 0.0, 1.0, 0.0)).z);
    if (dot(fw, fw) < 1e-20)
        fw = vec3(0.0, 0.0, 1.0);
    vec3 tv = cross(fw, ew);
    if (dot(tv, tv) < 1e-20)
        tv = cross(fw, dw);
    float k = length(screenDir(c, normalize(tv)));
    float s = sizePx / max(k, 1e-12);   // världsenheter per em
    sizeEff = sqrt(length(es) * length(ds)) * s;
    vec3 w  = (off.x * ew + off.y * dw) * s;
#ifdef GLAZE_POS3
    return uViewProj * vec4(anchor + w, 1.0);
#else
    return uViewProj * vec4(anchor + w.xy, 0.0, 1.0);
#endif
}

void main()
{
    vCol   = aCol;
    vHalo  = aHalo;
    vFlags = int(aFlags);
    vUv    = aUv;
    vShape = vec4(0.0);
    vPix   = vec2(0.0);
    vVis   = visibility();
#ifdef GLAZE_POS3
    vec4 c = uViewProj * vec4(aAnchor, 1.0);
#else
    vec4 c = uViewProj * vec4(aAnchor, 0.0, 1.0);
#endif
    if (c.z < 0.0 || c.w <= 1e-6 || vVis <= 0.0) {
        // Bakom närplanet, eller dold av kollisionen.
        gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
        return;
    }
    float sizePx = aSize * uParams.z;
    // Golvet, sedan taket — taket vinner när de korsas.
    if (uPaint1.x > 0.0) sizePx = max(sizePx, uPaint1.x);
    if (uPaint1.w > 0.0) sizePx = min(sizePx, uPaint1.w);
    vec2  e;
    if ((vFlags & 4) != 0) {
        vec4 d = uViewProj * vec4(cos(aAngle), sin(aAngle), 0.0, 0.0);
        vec2 s = (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
        float l = length(s);
        e = l > 1e-12 ? s / l : vec2(1.0, 0.0);
    } else {
        e = vec2(cos(aAngle), -sin(aAngle));
    }
    vec2 down = vec2(-e.y, e.x);
    vec2 off  = aOff;
    if ((vFlags & 3) != 0) {
        vShape.xy = abs(aUv);
        float g   = 1.0 / max(sizePx, 1e-6);
        vec2  sg  = sign(aUv);
        off += sg * g;
        vUv += sg * g;
    }
    if ((vFlags & 32) != 0) {
        // En BILDMARKÖR: bildens proportioner kring punkten, och en bild med
        // raderna nerifrån (ett GL-mål) vänds.
        float aspect = uPaint0.w;   // bildens bredd / höjd
        off *= aspect >= 1.0 ? vec2(1.0, 1.0 / aspect) : vec2(aspect, 1.0);
        if ((int(uPaint1.z + 0.5) & 2) != 0)
            vUv.y = 1.0 - vUv.y;
    }
    vShape.z = sizePx;
    vShape.w = aSize > 0.0 ? aHaloW / aSize : 0.0;
    if ((vFlags & 64) != 0) {
        // LIGGANDE: hörnet i världsplanet. Hörnen har var sitt w,
        // och klippmaskernas läge är hörnets egen projektion (ett hörn bakom
        // ögat har ingen — sällsynt för en etikett, skriven gräns).
        float eff;
        vec4  q     = groundCorner(aAnchor, c, off, sizePx, eff);
        vShape.z    = eff;
        gl_Position = toNdc(q);
        vPix        = q.w > 1e-6 ? q.xy / q.w : vec2(0.0);
        return;
    }
    // STÅENDE: hörnets djup med planet genom ankaret (kGlslStandSrc).
    vec2 o2 = (off.x * e + off.y * down) * sizePx;
    vec2 p  = c.xy / c.w + o2;
    gl_Position =
        toNdc(vec4(p * c.w, glaze_standingZ(uViewProj, c, o2), c.w));
    vPix        = p;
}
)glsl";

// Fragmentshadern — klippmaskernas och färgens delar klistras FÖRE.
const char kGlyphFsSrc[] = R"glsl(
in vec2 vUv;
flat in vec4 vCol;
flat in vec4 vHalo;
flat in vec4 vShape;
flat in int  vFlags;
flat in float vVis;
noperspective in vec2 vPix;
out vec4 frag;

void main()
{
    int   shape  = vFlags & 3;
    float sizePx = vShape.z;
    vec3  rgb;
    float a;
    if ((vFlags & 32) != 0) {
        // BILDMARKÖREN: R8 som gråskala, ett måls premultiplicerade innehåll
        // tillbaka till rak alfa (uPaint1.z: flaggorna 4 och 1).
        vec4 t = texture(uRamp, vUv);
        int  f = int(uPaint1.z + 0.5);
        if ((f & 4) != 0)
            t = vec4(t.rrr, 1.0);
        if ((f & 1) != 0)
            t.rgb = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
        rgb = t.rgb;
        a   = t.a;
    } else if (shape == 0) {
        float d = (texture(uRamp, vUv).r - 0.5) * 2.0 * uPaint0.y;
        if ((vFlags & 8) != 0)
            d += uPaint0.z;                  // syntetisk fet
        bool halo = (vFlags & 16) != 0;
        if (halo)
            d += vShape.w * uPaint0.x;       // glorians bredd, em → texlar
        vec4 col = halo ? vHalo : vCol;
        rgb = col.rgb;
        a   = col.a * clamp(d * sizePx / uPaint0.x + 0.5, 0.0, 1.0);
    } else {
        float dOut  = shape == 2 ? vShape.x - length(vUv)
                                 : min(vShape.x - abs(vUv.x),
                                       vShape.y - abs(vUv.y));
        float outer = clamp(dOut * sizePx + 0.5, 0.0, 1.0);
        float inner = clamp((dOut - vShape.w) * sizePx + 0.5, 0.0, 1.0);
        // Under PLATTAN ligger konturen bara i bandet.
        float fa = vCol.a * inner;
        float ha = vHalo.a * (shape == 1 ? outer - inner : outer * (1.0 - fa));
        a   = fa + ha;
        rgb = a > 0.0 ? (vCol.rgb * fa + vHalo.rgb * ha) / a : vCol.rgb;
    }
    frag = vec4(rgb, a * vVis * uPaint1.y * clipCoverage(vPix) *
                         sceneClip(vPix));
}
)glsl";

const void* byteOffset(std::size_t o)
{
    return reinterpret_cast<const void*>(o);
}

// Attributpekarna för vertexformatet V (GlyphVertex eller GlyphVertex3 —
// samma fält, ankaret med `anchorDims` komponenter) från vertexdelens offset.
template <class V>
void glyphAttribs(std::size_t base, GLint anchorDims)
{
    const auto stride = static_cast<GLsizei>(sizeof(V));
    pglVertexAttribPointer(0, anchorDims, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, ax)));
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, ox)));
    pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, u)));
    pglVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                           byteOffset(base + offsetof(V, color)));
    pglVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                           byteOffset(base + offsetof(V, halo)));
    pglVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, size)));
    pglVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, angle)));
    pglVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride,
                           byteOffset(base + offsetof(V, haloWidth)));
    pglVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, stride,
                            byteOffset(base + offsetof(V, flags)));
    pglVertexAttribIPointer(9, 1, GL_UNSIGNED_INT, stride,
                            byteOffset(base + offsetof(V, label)));
}

} // namespace

// Glyph-programmet — eller med `pos3` dess Glyph3-variant (definitionen
// efter `#version`) — länkat, med locations och VAO.
bool GlRenderer::buildGlyphProgram(bool pos3, GlyphProgram& p)
{
    const StatTimer klocka(stats_.compileNs);   // programbygget
    const char* name   = pos3 ? "Glyph3" : "Glyph";
    const char* define = pos3 ? "#define GLAZE_POS3 1\n" : "";
    const char* vsParts[] = {kGlslVersion, define, kGlslStandSrc, kGlyphVsSrc};
    const char* fsParts[] = {kGlslVersion, define, kGlslClipSrc, kGlslPaintSrc,
                             kGlyphFsSrc};
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, vsParts, 4);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, fsParts, 5);
    if (!vs || !fs) {
        if (vs)
            pglDeleteShader(vs);
        if (fs)
            pglDeleteShader(fs);
        log("GL: %s-programmet kunde inte kompileras — drawMesh ritar "
            "ingen text",
            name);
        return false;
    }
    // Attributen i GlyphVertex ordning (gfx/scene.h) — samma namn i Glyph3.
    static const char* const kAttribs[] = {"aAnchor", "aOff",   "aUv",
                                           "aCol",    "aHalo",  "aSize",
                                           "aAngle",  "aHaloW", "aFlags",
                                           "aLabel"};
    p.program = linkGlProgram(vs, fs, kAttribs, 10);
    if (!p.program) {
        log("GL: %s-programmet kunde inte länkas — drawMesh ritar ingen text",
            name);
        return false;
    }
    p.viewProj       = pglGetUniformLocation(p.program, "uViewProj");
    p.params         = pglGetUniformLocation(p.program, "uParams");
    p.paint0         = pglGetUniformLocation(p.program, "uPaint0");
    p.paint1         = pglGetUniformLocation(p.program, "uPaint1");
    p.clipP0         = pglGetUniformLocation(p.program, "uClipP0");
    p.clipP1         = pglGetUniformLocation(p.program, "uClipP1");
    p.clipN          = pglGetUniformLocation(p.program, "uClipN");
    const int  texLoc = pglGetUniformLocation(p.program, "uRamp");
    const int  lblLoc = pglGetUniformLocation(p.program, "uLabels");
    const bool klipp  = findSceneClip(p.program, p.sceneClip);
    if (p.viewProj < 0 || p.params < 0 || p.paint0 < 0 || p.paint1 < 0 ||
        p.clipP0 < 0 || p.clipP1 < 0 || p.clipN < 0 || texLoc < 0 ||
        lblLoc < 0 || !klipp) {
        log("GL: %s-programmets uniformer saknas (uViewProj=%d uParams=%d "
            "uPaint0=%d uPaint1=%d uClipP0=%d uClipP1=%d uClipN=%d uRamp=%d "
            "uLabels=%d uSceneClip=%d uSceneClipN=%d) — drawMesh ritar ingen "
            "text",
            name, p.viewProj, p.params, p.paint0, p.paint1, p.clipP0, p.clipP1,
            p.clipN, texLoc, lblLoc, p.sceneClip.planes, p.sceneClip.count);
        pglDeleteProgram(p.program);
        p = GlyphProgram{};
        return false;
    }
    pglUseProgram(p.program);
    pglUniform1i(texLoc, 0);   // atlassidan på enhet 0, som UI:ts tex0
    pglUniform1i(lblLoc, 1);   // etiketternas synlighet på enhet 1

    pglGenVertexArrays(1, &p.vao);
    pglBindVertexArray(p.vao);
    for (unsigned i = 0; i < 10; ++i)
        pglEnableVertexAttribArray(i);
    // UI-pipelinen är den bundna (init sker före första framen).
    pglUseProgram(program_);
    pglBindVertexArray(vao_);
    return true;
}

void GlRenderer::releaseGlyphProgram(GlyphProgram& p)
{
    if (p.vao)
        pglDeleteVertexArrays(1, &p.vao);
    if (p.program)
        pglDeleteProgram(p.program);
    p = GlyphProgram{};
}

bool GlRenderer::initGlyphProgram()
{
    if (!buildGlyphProgram(false, glyph_))
        return false;
    // Glyph3 får misslyckas för sig (loggat) — Glyph står kvar.
    buildGlyphProgram(true, glyph3_);
    return true;
}

void GlRenderer::shutdownGlyphProgram()
{
    // Kontexten är aktuell (shutdownRenderer).
    releaseGlyphProgram(glyph3_);
    releaseGlyphProgram(glyph_);
}

void GlRenderer::drawGlyphMesh(const MeshDraw& d)
{
    // Glyph och Glyph3 — samma källa, var sitt program.
    const bool    pos3 = d.program == MeshProgram::Glyph3;
    GlyphProgram& gp   = pos3 ? glyph3_ : glyph_;
    if (!ready() || !meshDrawOk(d, gp.program != 0))
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    const Pipeline want = pos3 ? Pipeline::Glyph3 : Pipeline::Glyph;
    if (pipeline_ != want) {
        pglUseProgram(gp.program);
        pglBindVertexArray(gp.vao);
        pipeline_ = want;
        ++stats_.programSwitches;
    }
    syncClipMasks(gp.clipP0, gp.clipP1, gp.clipN, gp.clipGen);
    setSceneClip(d, gp.sceneClip);

    // Kameran, storlekens enhet, fältets mått och opaciteten — per anrop,
    // räknas inte som uppladdning.
    pglUniformMatrix4fv(gp.viewProj, 1, GL_FALSE, d.uniforms.viewProj);
    // .w: etiketternas bas (exakt under 2²⁴), −1 = ingen synlighet bunden.
    pglUniform4f(gp.params, static_cast<float>(targetW_),
                 static_cast<float>(targetH_), d.uniforms.pxPerWidthUnit,
                 d.labels ? static_cast<float>(d.labelBase) : -1.0f);
    // .w och .z: bildmarkörernas proportioner och flaggor (Image-programmets
    // tal) — en atlassida läses aldrig så, bara quadar med kImage gör det.
    const float aspect = d.image && d.image->height() > 0
                             ? static_cast<float>(d.image->width()) /
                                   static_cast<float>(d.image->height())
                             : 1.0f;
    pglUniform4f(gp.paint0, kSdfRefSize, static_cast<float>(kSdfRadius),
                 kSyntheticBoldEm * kSdfRefSize, aspect);
    // .x och .w: storlekens golv och tak.
    pglUniform4f(gp.paint1, d.uniforms.glyphMinPx,
                 std::clamp(d.uniforms.opacity, 0.0f, 1.0f),
                 d.image ? sceneImageFlags(*d.image) : 0.0f,
                 d.uniforms.glyphMaxPx);
    // Atlassidan (vit när chunken bara bär former) med det LINJÄRA
    // samplerobjektet — ett avståndsfält ska interpoleras.
    glBindTexture(GL_TEXTURE_2D,
                  d.image ? static_cast<const GlImage*>(d.image)->tex()
                          : whiteTex_);
    bindSampler(ImageFilter::Linear);
    // Etiketternas synlighet på enhet 1 (vit när ingen är bunden — shadern
    // läser den då inte). texelFetch bryr sig inte om samplern; enhet 0 är
    // den aktiva igen efteråt, som resten av renderern förutsätter.
    pglActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  d.labels ? static_cast<const GlImage*>(d.labels)->tex()
                           : whiteTex_);
    pglActiveTexture(GL_TEXTURE0);

    const auto& vb = static_cast<const GlGpuBuffer&>(*d.vertices.buffer);
    const auto& ib = static_cast<const GlGpuBuffer&>(*d.indices.buffer);
    pglBindBuffer(GL_ARRAY_BUFFER, vb.id());
    if (pos3)
        glyphAttribs<GlyphVertex3>(d.vertices.offset, 3);
    else
        glyphAttribs<GlyphVertex>(d.vertices.offset, 2);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.id());   // VAO:ns tillstånd
    applyMeshState(d.state);   // renderläget
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount),
                   GL_UNSIGNED_INT, byteOffset(d.indices.offset));
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
