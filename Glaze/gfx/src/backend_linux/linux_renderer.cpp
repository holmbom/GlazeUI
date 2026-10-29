// GlRenderer — gfx::Renderer ovanpå EGL + OpenGL (3.3 core). Direktport av
// d3d11_renderer.cpp: EN pipeline (2D = ortho-quads), SDF-pixelshader med
// mode 0–5, batcher som flushar vid textur-/topologibyte/clear/composite,
// retained-FBO + composite, glScissor-klipp. GLSL kompileras i runtime med
// glCompileShader — samma princip som D3DCompile, inget byggsteg.
//
// GL:s y-uppochner hanteras på TRE ställen:
//  1) glScissor räknar nerifrån  → sy = fbH − (y + h)
//  2) glReadPixels läser nerifrån → raderna flippas i captureRetained
//  3) FBO-texturens v-axel        → composite-quadens UV:er flippas
// Vertexshadern gör samma px→NDC-flip som HLSL:n, så all ritkod använder
// top-left-origo precis som övriga backends.
//
// SDF-koordinaten är ett FJÄRDE fall och löses INTE med en flipp: den
// interpoleras ur ritkoordinaten (varyingen vPix), aldrig ur gl_FragCoord.
// HLSL:n kan använda SV_Position eftersom D3D redan är top-left; i GL är
// gl_FragCoord:s y-riktning OLIKA för retained-FBO:n och backbuffern
// (compositen vänder FBO:ns v-axel), så en fast flipp är fel i det ena
// målet. Symtomet är lömskt: masken speglas kring fönstermitten, så en
// form nära mitten ser bara kapad ut medan knappar längre bort tappar sin
// bakgrund HELT.
//
// GL-anropen bor i den här filen, i scenens linux_scene.cpp och i det de
// delar (gl_common.cpp: loadern via eglGetProcAddress — libGL:s export-ABI
// garanterar bara 1.2-eran — shaderkompileringen och klippmaskernas GLSL).
// Text och bild går genom textur-hjälparna här.

#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../gpu_util.h"
#include "gfx/platform.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {

namespace {

// ---------------------------------------------------------------- shaders

// GLSL-porten av d3d11_renderer.cpp:s HLSL — mode-semantiken identisk:
// 0 = texturmask (R8: geometri/text), 1 = fylld rundad rektangel,
// 2 = kontur, 3 = drop shadow, 4 = inner shadow, 5 = RGBA rakt av.
const char kVsSrc[] = R"glsl(#version 330 core
uniform vec2 vp;
in vec2  aPos;
in vec2  aUv;
in vec4  aCol;
in vec4  aP0;
in vec4  aP1;
in float aMode;
out vec2  vUv;
out vec4  vCol;
out vec4  vP0;
out vec4  vP1;
out float vMode;
out vec2  vPix;
void main()
{
    // Pixelkoordinater (origo uppe till vänster) -> NDC — samma formel
    // som HLSL-vertexshadern.
    gl_Position = vec4(aPos.x * 2.0 / vp.x - 1.0,
                       1.0 - aPos.y * 2.0 / vp.y, 0.0, 1.0);
    vUv   = aUv;
    vCol  = aCol;
    vP0   = aP0;
    vP1   = aP1;
    vMode = aMode;
    vPix  = aPos;   // SDF-rymden = ritkoordinaten, aldrig gl_FragCoord
}
)glsl";

// Fragmentshaderns kropp — `#version` och klippmaskernas del
// (kGlslClipSrc: sdRoundRect, uClipP0/P1/N och clipCoverage) klistras FÖRE
// i buildPipeline, samma del som scenens program får (gl_common.h).
const char kFsSrc[] = R"glsl(
uniform sampler2D tex0;
uniform vec2 vp;
in vec2  vUv;
in vec4  vCol;
in vec4  vP0;
in vec4  vP1;
in float vMode;
in vec2  vPix;
out vec4 frag;

void main()
{
    int mode = int(vMode + 0.5);
    // Masken utvärderas mot vPix — ALDRIG gl_FragCoord. Y-riktningen där
    // är olika i retained-FBO:n och i backbuffern, så en fast flipp hade
    // speglat masken kring fönstermitten.
    float clipCov = clipCoverage(vPix);
    if (mode >= 5) {
        // Compositen: retained-FBO:n rakt av — men med en VALFRI rundad
        // mask (vP1.x > 0), som är det som ger fönstret sin form. Masken
        // sitter HÄR och inte i ritvägen därför att den måste gälla ALLT
        // som hamnat i FBO:n, inklusive rects en app målat själv i sin
        // frame()-krok. FBO:n bär premultiplicerad alfa, så coverage
        // multipliceras in i alla fyra kanalerna.
        vec4 tex = texture(tex0, vUv);
        if (mode == 6) {
            // Ett render-måls bild bär PREMULTIPLICERAD färg (gfx/image.h):
            // filtrerad premultiplicerad och därefter delad är rätt ordning —
            // utan delningen får kanterna en mörk frans över ljus yta.
            tex.rgb = tex.a > 0.0 ? tex.rgb / tex.a : vec3(0.0);
        } else if (mode == 7) {
            // En R8-bild är gråskala, helt täckande (gfx/image.h).
            tex = vec4(tex.rrr, 1.0);
        }
        if (vP1.x > 0.0) {
            float d = sdRoundRect(vPix - vP0.xy, vP0.zw, vP1.x);
            tex *= clamp(0.5 - d, 0.0, 1.0);
        }
        // ALFA-KONTRAKTET: en mode 5-BILD i ritvägen (imageQuad) bär RAK
        // alfa, så barnklippet får bara röra alfakanalen — att multiplicera
        // hela RGBA hade mörknat AA-bandet dubbelt. Compositens quad bär
        // premultiplicerat och multiplicerar därför alla fyra kanalerna
        // med FÖNSTERmasken ovan; där är uClipN 0 per konstruktion
        // (klippstacken är balanserad och tom vid composite), så raden
        // nedan är en no-op just där. Vertexfärgens ALFA är bildens
        // OPACITET (Renderer::drawImage) och rör också bara alfan;
        // compositens quad bär full alfa, så den faktorn är 1 där.
        tex.a *= vCol.a * clipCov;
        frag = tex;
        return;
    }
    float alpha;
    if (mode == 0) {
        alpha = texture(tex0, vUv).r;
    } else {
        // SDF-koordinaten kommer från den INTERPOLERADE ritkoordinaten,
        // inte gl_FragCoord: p0 är i top-left-px och vPix är samma rymd per
        // konstruktion. gl_FragCoord skulle kräva en y-flipp som är OLIKA
        // för retained-FBO:n och backbuffern (compositen vänder FBO:ns
        // v-axel) — fel håll speglar masken kring fönstermitten och kapar
        // allt SDF-ritat: knappbakgrunder, paneler, skuggor.
        vec2 pix = vPix;
        float d = sdRoundRect(pix - vP0.xy, vP0.zw, vP1.x);
        if (mode == 1) {
            alpha = clamp(0.5 - d, 0.0, 1.0);
        } else if (mode == 2) {
            alpha = clamp(0.5 - d, 0.0, 1.0) *
                    clamp(d + vP1.y + 0.5, 0.0, 1.0);
        } else if (mode == 3) {
            float blur = max(vP1.y, 0.5);
            alpha = 1.0 - smoothstep(-blur, blur, d);
        } else {
            float blur = max(vP1.y, 0.5);
            float ds = sdRoundRect(pix - vP0.xy - vP1.zw, vP0.zw, vP1.x);
            alpha = smoothstep(-blur, 0.0, ds) * clamp(0.5 - d, 0.0, 1.0);
        }
    }
    // mode 0–4 är alla rak alfa — klippet skalar täckningen, inte färgen.
    frag = vec4(vCol.rgb, vCol.a * alpha * clipCov);
}
)glsl";

std::uint32_t pack(Color c)
{
    // Matchar GL_UNSIGNED_BYTE-attributet i minnesordning r,g,b,a.
    return static_cast<std::uint32_t>(c.r) |
           static_cast<std::uint32_t>(c.g) << 8 |
           static_cast<std::uint32_t>(c.b) << 16 |
           static_cast<std::uint32_t>(c.a) << 24;
}

} // namespace

// ---------------------------------------------------------------- init

bool GlRenderer::init(EGLDisplay dpy, EGLConfig cfg, EGLSurface surf,
                      int w, int h, bool transparent)
{
    display_     = dpy;
    surface_     = surf;
    transparent_ = transparent;
    width_       = w;
    height_      = h;
    targetW_     = w;
    targetH_     = h;

    eglBindAPI(EGL_OPENGL_API);

    // 3.3 core; faller tillbaka på "senaste kompatibla" om begäran nekas
    // (GLSL 330 fungerar även där).
    const EGLint ctxCore[] = {EGL_CONTEXT_MAJOR_VERSION, 3,
                              EGL_CONTEXT_MINOR_VERSION, 3,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK,
                              EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                              EGL_NONE};
    context_ = eglCreateContext(display_, cfg, EGL_NO_CONTEXT, ctxCore);
    if (context_ == EGL_NO_CONTEXT)
        context_ = eglCreateContext(display_, cfg, EGL_NO_CONTEXT, nullptr);
    if (context_ == EGL_NO_CONTEXT) {
        log("eglCreateContext misslyckades: 0x%04x", eglGetError());
        return false;
    }
    if (!makeCurrent()) {
        log("eglMakeCurrent misslyckades: 0x%04x", eglGetError());
        shutdownRenderer();
        return false;
    }

    // KRITISKT: swap-interval 0. Med interval 1
    // väntar Mesa på frame-callbacks som skymda/omappade Wayland-ytor
    // aldrig får — en enda blockerad eglSwapBuffers hänger HELA den
    // enkeltrådade pumpen. Ritningen är händelsestyrd ändå (hård regel 1).
    eglSwapInterval(display_, 0);

    if (!loadGlFunctions() || !buildPipeline()) {
        shutdownRenderer();
        return false;
    }

    // Enhetens namn (`Renderer::deviceName`) och MSAA-taket
    // (llvmpipe har 4).
    if (const auto* name =
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)))
        deviceName_ = name;
    // Kortets id ur EGL — tomt för llvmpipe.
    adapterId_ = eglAdapterId(display_);
    // …och i loggen när den är en annan än förra renderarens
    // — en popup på samma GPU loggar ingenting.
    {
        static std::string senaste;
        if (deviceName_ != senaste) {
            senaste                = deviceName_;
            const std::string valt = gpuAdapter();
            log("GL: ritar med %s [%s] (preferensen %s, kortet %s)",
                deviceName_.c_str(),
                adapterId_.empty() ? "?" : adapterId_.c_str(),
                gpu::preferenceName(gpuPreference()),
                valt.empty() ? "inget" : valt.c_str());
        }
    }
    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    maxSamples_ = std::max(1, static_cast<int>(maxSamples));
    // Den största texturen — större bilder delar glaze i rutor.
    GLint maxTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    if (maxTexture > 0)
        maxTextureSize_ = static_cast<int>(maxTexture);
    // VALET I EFTERHAND: ett mål som inte blev av,
    // mjukvarurendering och en begäran ändrad efter init — loggat.
    reportGpuOutcome(adapterId_, deviceName_);

    // 1×1 vit R8-textur — alfa-masken för ofärgad geometri.
    const std::uint8_t white = 255;
    whiteTex_ = createTexture(ImageFormat::R8, 1, 1, &white, 1, false);

    // Samplerobjekten, ett per filter — se bindSampler. Förminskningen
    // går genom MIP-KEDJAN: Nearest närmaste nivå, Linear mellan
    // nivåerna. En textur utan kedja har GL_TEXTURE_MAX_LEVEL 0
    // (createTexture, createOffscreen) och samplas då bara i nivå 0 — utan
    // det hade mip-filtret gjort den ofullständig (svart).
    auto makeSampler = [](GLint minFilter, GLint magFilter) {
        unsigned s = 0;
        pglGenSamplers(1, &s);
        pglSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minFilter);
        pglSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magFilter);
        pglSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        pglSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return s;
    };
    samplerNearest_ = makeSampler(GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST);
    samplerLinear_  = makeSampler(GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR);

    createOffscreen(w, h);   // misslyckande = degraderat (rita direkt)

    // Scenens program (linux_scene.cpp). Misslyckas det loggas det och
    // drawMesh ritar ingenting — UI:t påverkas inte. Image-programmet
    // (linux_image.cpp) skapas efter det.
    initScene();
    initImageProgram();
    initFillProgram();   // ytorna (linux_fill.cpp)
    initGlyphProgram();  // texten (linux_glyph.cpp)
    initProgramSupport();   // egna program: VAO:n och blocken (linux_program.cpp)
    gpuTimingSupported_ = initGpuQueries();   // GPU-tiden

    batch_.reserve(kMaxVerts);
    return true;
}

bool GlRenderer::buildPipeline()
{
    const StatTimer klocka(stats_.compileNs);   // programbygget
    const char* vsParts[] = {kVsSrc};
    const char* fsParts[] = {kGlslVersion, kGlslClipSrc, kFsSrc};
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, vsParts, 1);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, fsParts, 3);
    if (!vs || !fs) {
        if (vs)
            pglDeleteShader(vs);
        if (fs)
            pglDeleteShader(fs);
        return false;
    }
    static const char* const kAttribs[] = {"aPos", "aUv", "aCol",
                                           "aP0",  "aP1", "aMode"};
    program_ = linkGlProgram(vs, fs, kAttribs, 6);
    if (!program_)
        return false;

    pglUseProgram(program_);
    vpUniform_ = pglGetUniformLocation(program_, "vp");
    pglUniform1i(pglGetUniformLocation(program_, "tex0"), 0);

    // RUNDADE KLIPPMASKER — locations + felkontroll. Uniformarna ANVÄNDS
    // i shadern, så en -1 här betyder att GLSL-källan och C++-sidan har
    // glidit isär; det ska sägas rakt ut och inte bli ett tyst no-op i
    // glUniform (som accepterar -1 utan att göra något).
    clipP0Uniform_    = pglGetUniformLocation(program_, "uClipP0");
    clipP1Uniform_    = pglGetUniformLocation(program_, "uClipP1");
    clipCountUniform_ = pglGetUniformLocation(program_, "uClipN");
    if (clipP0Uniform_ < 0 || clipP1Uniform_ < 0 || clipCountUniform_ < 0) {
        log("GLSL: klippmask-uniformerna saknas (uClipP0=%d uClipP1=%d "
            "uClipN=%d) — rundat barnklipp skulle bli tyst verkningslöst",
            clipP0Uniform_, clipP1Uniform_, clipCountUniform_);
        return false;
    }

    // VAO (krav i core-profil) + streamad VBO med d3d11-vertexformatet.
    pglGenVertexArrays(1, &vao_);
    pglBindVertexArray(vao_);
    pglGenBuffers(1, &vbo_);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    const auto off    = [](std::size_t o) {
        return reinterpret_cast<const void*>(o);
    };
    pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, off(0));
    pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, off(8));
    pglVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, off(16));
    pglVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, off(20));
    pglVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, off(36));
    pglVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, off(52));
    for (unsigned i = 0; i < 6; ++i)
        pglEnableVertexAttribArray(i);

    // UI:ts tillstånd — rak alfa (som D3D11-blend-descen), inget djup, ingen
    // gallring — genom renderlägets cache, som framen och scenen delar.
    // FRAMSIDAN är moturs sett från kameran (gfx/scene.h): fönsterkoordinaterna
    // har y uppåt som vertexshadrarnas NDC, så GL_CCW är moturs för ögat — och
    // den ändras aldrig, så den sätts en gång här.
    glFrontFace(GL_CCW);
    resetStateCache();
    applyUiState();
    return true;
}

void GlRenderer::shutdownRenderer()
{
    if (context_ != EGL_NO_CONTEXT) {
        makeCurrent();
        shutdownGpuQueries();
        shutdownProgramSupport();
        shutdownGlyphProgram();
        shutdownFillProgram();
        shutdownImageProgram();
        shutdownScene();   // scenens buffertar och program före kontexten
        batch_.clear();
        if (samplerNearest_)
            pglDeleteSamplers(1, &samplerNearest_);
        if (samplerLinear_)
            pglDeleteSamplers(1, &samplerLinear_);
        if (offFbo_)
            pglDeleteFramebuffers(1, &offFbo_);
        if (offTex_)
            glDeleteTextures(1, &offTex_);
        if (whiteTex_)
            glDeleteTextures(1, &whiteTex_);
        if (vbo_)
            pglDeleteBuffers(1, &vbo_);
        if (vao_)
            pglDeleteVertexArrays(1, &vao_);
        if (program_)
            pglDeleteProgram(program_);
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        eglDestroyContext(display_, context_);
    }
    offFbo_ = offTex_ = whiteTex_ = vbo_ = vao_ = program_ = 0;
    samplerNearest_ = samplerLinear_ = 0;
    context_       = EGL_NO_CONTEXT;
    surface_       = EGL_NO_SURFACE;
    display_       = EGL_NO_DISPLAY;
    retainedValid_ = false;
    inBackbuffer_  = false;
    currentFbo_    = 0;
    bbOrigin_      = 0;   // marginalen själv är fönstrets och står kvar
    uiVpW_ = uiVpH_ = 0;
    curTex_        = 0;
    pipeline_      = Pipeline::Ui;
    clipCount_     = 0;
    clipGen_ = uiClipGen_ = 0;   // programmens egna nollas i deras shutdown
    bound_         = nullptr;
    inFrame_       = false;
    offscreen_     = false;
    gpuTimingSupported_ = false;
    resetStateCache();
}

bool GlRenderer::makeCurrent()
{
    if (context_ == EGL_NO_CONTEXT)
        return false;
    return eglMakeCurrent(display_, surface_, surface_, context_) ==
           EGL_TRUE;
}

void GlRenderer::setSwapInterval(int interval)
{
    if (!ready())
        return;
    makeCurrent();
    eglSwapInterval(display_, interval);
}

bool GlRenderer::createOffscreen(int w, int h)
{
    if (offFbo_)
        pglDeleteFramebuffers(1, &offFbo_);
    if (offTex_)
        glDeleteTextures(1, &offTex_);
    offFbo_ = offTex_ = 0;
    retainedValid_ = false;   // ny yta = odefinierat innehåll tills composite

    glGenTextures(1, &offTex_);
    glBindTexture(GL_TEXTURE_2D, offTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);   // ingen kedja
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    pglGenFramebuffers(1, &offFbo_);
    pglBindFramebuffer(GL_FRAMEBUFFER, offFbo_);
    pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, offTex_, 0);
    const bool ok = pglCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                    GL_FRAMEBUFFER_COMPLETE;
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        log("Retained-FBO:n kunde inte skapas (%dx%d)", w, h);
        pglDeleteFramebuffers(1, &offFbo_);
        glDeleteTextures(1, &offTex_);
        offFbo_ = offTex_ = 0;
        return false;
    }
    return true;
}

void GlRenderer::resize(int w, int h)
{
    if (!ready() || w <= 0 || h <= 0)
        return;
    if (w == width_ && h == height_)
        return;
    makeCurrent();
    width_   = w;
    height_  = h;
    targetW_ = w;
    targetH_ = h;
    createOffscreen(w, h);
}

// ---------------------------------------------------------------- frame

void GlRenderer::beginFrame()
{
    if (!makeCurrent())
        return;
    ++frameSerial_;
    // GPU-tiden: de färdiga svaren först, sedan framens fråga.
    collectGpuQueries();
    startGpuQuery();
    inFrame_      = true;
    inBackbuffer_ = false;
    currentFbo_   = offFbo_;   // framens eget mål (render-målen återvänder hit)
    bound_        = nullptr;   // framens eget mål: inget djup, ingen MSAA
    pglBindFramebuffer(GL_FRAMEBUFFER, offFbo_);   // 0 = degraderat läge
    // Det degraderade läget ritar direkt i backbuffern — med en
    // storleksmarginal ligger klientytan då m px in (E2L). Marginalen får
    // där fönstrets rensfärg: en synlig ram, men innehållet står rätt.
    bbOrigin_ = offFbo_ ? 0 : backbufferOrigin();
    glViewport(bbOrigin_, bbOrigin_, width_, height_);
    glDisable(GL_SCISSOR_TEST);
    scissorOn_ = false;
    // UI:ts tillstånd (rak alfa, inget djup, ingen gallring) — cachen glöms
    // först, så allt sätts oavsett vad förra framens sista anrop lämnade.
    resetStateCache();
    applyUiState();
    pglUseProgram(program_);
    pglBindVertexArray(vao_);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo_);
    pglUniform2f(vpUniform_, static_cast<float>(width_),
                 static_cast<float>(height_));
    uiVpW_   = width_;
    uiVpH_   = height_;
    targetW_ = width_;    // retained-FBO:n (eller backbuffern i degraderat
    targetH_ = height_;   // läge) — samma storlek
    // En obalanserad pushTarget i förra framen får inte peka hit.
    beginFrameTargets();
    // Klippmaskerna nollställs vid framens början. Stacken är balanserad
    // per konstruktion, men en obalans är ett LOGGAT programfel och får
    // inte dessutom läcka en mask in i nästa frame — generationen stegas om
    // något var kvar, och varje program får nollan när det ritar.
    setClipMasks(nullptr, 0);
    pglActiveTexture(GL_TEXTURE0);
    beginSceneFrame();   // UI-pipelinen är bunden; ringarna börjar om
}

void GlRenderer::composite()
{
    if (!ready())
        return;
    flush();   // allt callback-ritat ska in i retained-FBO:n först

    // Callbacket har ritat klart — FBO:n bär nu senaste framens innehåll
    // och nästa callback får rita partiellt (fasadens retainedValid).
    if (offFbo_)
        retainedValid_ = true;

    // Resten av framen (chrome, overlay) går direkt i backbuffern — som nu
    // är framens eget mål för render-målen också.
    pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    inBackbuffer_ = true;
    currentFbo_   = 0;
    bound_        = nullptr;
    targetW_      = width_;
    targetH_      = height_;
    if (surface_ == EGL_NO_SURFACE || !offTex_)
        return;   // surfacelöst/degraderat — ingen backbuffer att skriva

    // STORLEKSMARGINALEN: backbuffern är klientytan + 2m. Hela ytan
    // rensas till helt genomskinligt — marginalen ska aldrig synas, bara ta
    // emot pekaren — och klientytan läggs m px in: viewporten (y räknas
    // nerifrån, och marginalen är lika bred runt om) förskjuts, så quaden
    // nedan och allt som ritas efter den talar klientens px. Utan marginal
    // täcker quaden hela backbuffern och ingen rensning behövs.
    //
    // COMPOSITEN RITAS UTAN KLIPP: en scissor satt i
    // FBO:ns koordinater hade legat fel efter förskjutningen, och varken den
    // eller en rundad mask får beskära fönstrets yta. Klippstacken står
    // orörd och tillämpas igen efter quaden, nu i backbufferns koordinater
    // (`reapplyClip` → `applyClip` med `bbOrigin_`) — en öppen stack här är
    // ett programfel i ritningen, men chromet och overlayen ska ändå klippas
    // där stacken säger.
    bbOrigin_ = backbufferOrigin();
    applyClip(nullptr, nullptr, 0);
    if (bbOrigin_ > 0) {
        applyUiState();   // glClear lyder färgmasken
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glViewport(bbOrigin_, bbOrigin_, width_, height_);

    // Fullskärms-quad i mode 5 (RGBA rakt av). OBS FBO-texturens v-axel:
    // vårt y=0 (överkanten) ligger på v=1 — UV:erna flippas här (y-flipp
    // nr 3). Transparent-vägen: FBO:n bär redan premultiplicerat (rak
    // alfa mot alfa-0-clear) och Wayland förväntar sig premultiplicerat —
    // skriv UTAN blandning; opaka fönster behåller blend-läget (alfa = 1).
    // NEAREST uttryckligen: 1:1, och ett samplerobjekt som en bild bundit
    // sist i framen får inte bestämma hur fönstret samplas. UI-pipelinen
    // binds FÖRST för den transparenta vägen: annars hade flush() — efter ett
    // scenanrop — återställt UI:ts blandning ovanpå undantaget.
    setState(offTex_, GL_TRIANGLES, ImageFilter::Nearest);
    if (transparent_) {
        if (pipeline_ != Pipeline::Ui)
            bindUiPipeline();
        setBlendState(Blend::Opaque);
    }
    Vertex v{};
    v.rgba = 0xffffffff;
    v.mode = 5.0f;
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    // Fönstrets form som mask (0 = fyrkantigt). Samma konvention som
    // sdfQuad: p0 = centrum + halvmått, p1.x = radien.
    v.p0[0] = w * 0.5f;
    v.p0[1] = h * 0.5f;
    v.p0[2] = w * 0.5f;
    v.p0[3] = h * 0.5f;
    v.p1[0] = compositeRadius_;
    const float xs[6] = {0, w, 0, w, w, 0};
    const float ys[6] = {0, 0, h, 0, h, h};
    const float us[6] = {0, 1, 0, 1, 1, 0};
    const float vs[6] = {1, 1, 0, 1, 0, 0};   // flippad v (se ovan)
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        v.u = us[i];
        v.v = vs[i];
        batch_.push_back(v);
    }
    flush();
    if (transparent_)
        setBlendState(Blend::Alpha);
    reapplyClip();   // stackens klipp igen — i backbufferns koordinater
}

void GlRenderer::present(bool swap)
{
    if (!ready())
        return;
    flush();
    // Framens GPU-tid tar slut här — efter dess sista rit-anrop.
    if (gpuQueryOpen_ >= 0) {
        pglQueryCounter(gpuQueries_[gpuQueryOpen_].end, GL_TIMESTAMP);
        gpuQueryOpen_ = -1;
    }
    if (swap && surface_ != EGL_NO_SURFACE) {
        const StatTimer klocka(stats_.presentNs);   // swappens väntan
        eglSwapBuffers(display_, surface_);
    } else {
        glFlush();   // dolt/surfacelöst — FBO-innehållet ska bli synligt
                     // för captureRetained utan present
    }
    inFrame_ = false;
}

// ---------------------------------------------------------- GPU-tiden

bool GlRenderer::initGpuQueries()
{
    gpuQueryOpen_ = -1;
    if (!loadGlTimerQueries())
        return false;
    for (GpuQuery& q : gpuQueries_) {
        unsigned ids[2] = {};
        pglGenQueries(2, ids);
        q = GpuQuery{ids[0], ids[1], 0};
    }
    return true;
}

// Med kontexten aktuell (shutdownRenderer).
void GlRenderer::shutdownGpuQueries()
{
    for (GpuQuery& q : gpuQueries_) {
        if (q.begin) {
            const unsigned ids[2] = {q.begin, q.end};
            pglDeleteQueries(2, ids);
        }
        q = GpuQuery{};
    }
    gpuQueryOpen_ = -1;
}

// De svar som är färdiga — utan att vänta; ett svar som inte är klart läses
// nästa frame. Tidsstämplarna är i ns.
void GlRenderer::collectGpuQueries()
{
    for (int i = 0; i < kGpuQueries; ++i) {
        GpuQuery& q = gpuQueries_[i];
        if (q.serial == 0 || i == gpuQueryOpen_)
            continue;
        GLint klar = 0;
        pglGetQueryObjectiv(q.end, GL_QUERY_RESULT_AVAILABLE, &klar);
        if (!klar)
            continue;
        GLuint64 t0 = 0, t1 = 0;
        pglGetQueryObjectui64v(q.begin, GL_QUERY_RESULT, &t0);
        pglGetQueryObjectui64v(q.end, GL_QUERY_RESULT, &t1);
        if (t1 >= t0)
            addGpuTime(q.serial, t1 - t0);
        q.serial = 0;
    }
}

// Framens fråga börjar — när tidtagningen är på och en plats är ledig (alla
// fyra väntar annars på svar, och framen mäts inte).
void GlRenderer::startGpuQuery()
{
    gpuQueryOpen_ = -1;
    if (!gpuTiming_ || !gpuTimingSupported_)
        return;
    for (int i = 0; i < kGpuQueries; ++i) {
        GpuQuery& q = gpuQueries_[i];
        if (q.serial != 0)
            continue;
        pglQueryCounter(q.begin, GL_TIMESTAMP);
        q.serial      = frameSerial_;
        gpuQueryOpen_ = i;
        return;
    }
}

// ---------------------------------------------------- ritning utan frame
//
// Se renderer.h. GL behöver bara kontexten: UI-programmet och
// dess tillstånd är bundna från init (eller förra framen — flush() binder om
// efter ett scenanrop). Framens eget mål finns inte i blocket: `bindTarget`
// binder standard-FBO:n (0) när det sista målet poppas och `clear` rensar
// ingenting där — en ritning utanför ett mål (kontraktsbrott) hamnar i en
// backbuffer som nästa frame skriver över helt, eller i ingenting alls för ett
// surfacelöst fönster.
bool GlRenderer::beginOffscreen()
{
    if (!ready() || inFrame_ || offscreen_) {
        log("beginOffscreen: %s — blocket öppnas inte",
            !ready()   ? "ingen kontext"
            : inFrame_ ? "inuti en frame"
                       : "ett block är redan öppet");
        return false;
    }
    if (!makeCurrent())
        return false;
    offscreen_ = true;
    ++frameSerial_;        // blocket har ett eget löpnummer (renderer.h)
    bound_     = nullptr;
    beginFrameTargets();   // en obalanserad stack töms
    return true;
}

void GlRenderer::endOffscreen()
{
    if (!offscreen_) {
        log("endOffscreen utan beginOffscreen — ignoreras");
        return;
    }
    flush();
    glFlush();   // arbetet till GPU:n — readTarget:s glReadPixels väntar in det
    offscreen_ = false;
    bound_     = nullptr;
}

// Readback (glaze-render): retained-FBO:n → rå RGBA8 med rad 0 ÖVERST —
// glReadPixels läser nerifrån (y-flipp nr 2), raderna vänds här.
bool GlRenderer::captureRetained(std::vector<std::uint8_t>& rgba,
                                 int& w, int& h)
{
    if (!ready() || !offFbo_ || !retainedValid_)
        return false;
    if (!makeCurrent())
        return false;
    pglBindFramebuffer(GL_FRAMEBUFFER, offFbo_);
    rgba.resize(static_cast<std::size_t>(width_) * height_ * 4);
    std::vector<std::uint8_t> tmp(rgba.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                 tmp.data());
    const std::size_t stride = static_cast<std::size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y)
        std::memcpy(rgba.data() + static_cast<std::size_t>(y) * stride,
                    tmp.data() +
                        static_cast<std::size_t>(height_ - 1 - y) * stride,
                    stride);
    pglBindFramebuffer(GL_FRAMEBUFFER, currentFbo_);   // det bundna målet
    w = width_;
    h = height_;
    return true;
}

// ---------------------------------------------------------------- batcher

void GlRenderer::flush()
{
    if (batch_.empty() || !ready())
        return;
    // Ett drawMesh efter förra UI-ritningen har lämnat scenens program
    // bundet — återbindningen sitter HÄR, där UI-pipelinen ritar. Därefter får
    // UI-programmet maskerna om de ändrats efter att det ritade sist.
    if (pipeline_ != Pipeline::Ui)
        bindUiPipeline();
    syncClipMasks(clipP0Uniform_, clipP1Uniform_, clipCountUniform_,
                  uiClipGen_);
    // Det BUNDNA målets storlek till vertexshaderns px → NDC — ett
    // render-mål har sin egen. Bara när den skiljer.
    if (uiVpW_ != targetW_ || uiVpH_ != targetH_) {
        pglUniform2f(vpUniform_, static_cast<float>(targetW_),
                     static_cast<float>(targetH_));
        uiVpW_ = targetW_;
        uiVpH_ = targetH_;
    }
    // Orphaning-stream: ny buffert per flush (glBufferData med data).
    pglBufferData(GL_ARRAY_BUFFER,
                  static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)),
                  batch_.data(), GL_STREAM_DRAW);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
    // Batchens filter — vid VARJE ritning, annars hade samplerobjektet som
    // en bild eller scenens program lämnat bundet bestämt samplingen.
    bindSampler(curFilter_);
    glDrawArrays(curTopo_, 0, static_cast<GLsizei>(batch_.size()));
    ++stats_.drawCalls;
    stats_.vertices += batch_.size();
    batch_.clear();
}

// UI-pipelinens tillstånd tillbaka efter scenens program: programmet, VAO:n
// och GL_ARRAY_BUFFER (som INTE är VAO-tillstånd — glBufferData i flush
// skriver i den buffert som är bunden där, så den MÅSTE vara vbo_) — och
// renderläget (`applyUiState`).
void GlRenderer::bindUiPipeline()
{
    pglUseProgram(program_);
    pglBindVertexArray(vao_);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo_);
    applyUiState();
    pipeline_ = Pipeline::Ui;
    ++stats_.programSwitches;
}

// ---------------------------------------------------------- renderläget
//
// Se gfx/scene.h `RenderState`. GL:s tillstånd är globalt per
// kontext, så ett scenanrop som slog på djup, gallring eller en annan
// blandning lämnar det kvar — UI-pipelinen sätter tillbaka sitt (`applyUiState`
// i bindUiPipeline) innan den ritar, och `clear` sätter masken den lyder.
// Djupet verkar bara i ett mål med djup (annars som Off, som D3D11 och Metal).

namespace {

GLenum depthFuncOf(DepthTest t)
{
    switch (t) {
    case DepthTest::Less:         return GL_LESS;
    case DepthTest::LessEqual:    return GL_LEQUAL;
    case DepthTest::Greater:      return GL_GREATER;
    case DepthTest::GreaterEqual: return GL_GEQUAL;
    case DepthTest::Equal:        return GL_EQUAL;
    case DepthTest::Always:
    case DepthTest::Off:          break;
    }
    return GL_ALWAYS;
}

} // namespace

void GlRenderer::applyMeshState(const RenderState& s)
{
    const bool depth =
        s.depth != DepthTest::Off && bound_ && bound_->hasDepth();
    setDepthState(depth, depthFuncOf(s.depth), s.depthWrite);
    // Polygon offset: (faktor, enheter) = (lutningen, biasen). För ett
    // flyttalsdjup räknar GL enheten som 2^(e − 23), som D3D11 och Metal.
    const bool offset =
        depth && (s.depthBias != 0.0f || s.slopeBias != 0.0f);
    setOffsetState(offset, s.slopeBias, s.depthBias);
    setCullState(s.cull);
    setBlendState(s.blend);
}

void GlRenderer::applyUiState()
{
    setDepthState(false, GL_ALWAYS, true);
    setOffsetState(false, 0.0f, 0.0f);
    setCullState(Cull::None);
    setBlendState(Blend::Alpha);
}

void GlRenderer::resetStateCache()
{
    glDepthOn_ = glDepthFunc_ = glDepthMask_ = -1;
    glOffsetOn_ = glCull_ = glBlend_ = -1;
}

void GlRenderer::setDepthState(bool on, unsigned func, bool write)
{
    // Med testet av skriver GL inte heller djupet — som Off ska vara.
    if (glDepthOn_ != static_cast<int>(on)) {
        if (on)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        glDepthOn_ = on;
    }
    if (on && glDepthFunc_ != static_cast<int>(func)) {
        glDepthFunc(func);
        glDepthFunc_ = static_cast<int>(func);
    }
    // Masken sätts också med testet av: `clear` lyder den, och UI:ts läge
    // lämnar den PÅ så att ett djup alltid går att rensa.
    const bool mask = on ? write : true;
    if (glDepthMask_ != static_cast<int>(mask)) {
        glDepthMask(mask ? GL_TRUE : GL_FALSE);
        glDepthMask_ = mask;
    }
}

void GlRenderer::setOffsetState(bool on, float slope, float units)
{
    if (glOffsetOn_ != static_cast<int>(on)) {
        if (on)
            glEnable(GL_POLYGON_OFFSET_FILL);
        else
            glDisable(GL_POLYGON_OFFSET_FILL);
        glOffsetOn_ = on;
    }
    if (on && (slope != glSlope_ || units != glUnits_)) {
        glPolygonOffset(slope, units);
        glSlope_ = slope;
        glUnits_ = units;
    }
}

void GlRenderer::setCullState(Cull c)
{
    if (glCull_ == static_cast<int>(c))
        return;
    if (c == Cull::None) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(c == Cull::Back ? GL_BACK : GL_FRONT);
    }
    glCull_ = static_cast<int>(c);
}

void GlRenderer::setBlendState(Blend b)
{
    if (glBlend_ == static_cast<int>(b))
        return;
    // Blandningen mot det PREMULTIPLICERADE målet (gfx/scene.h `Blend`).
    switch (b) {
    case Blend::Alpha:
        glEnable(GL_BLEND);
        pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                             GL_ONE_MINUS_SRC_ALPHA);
        break;
    case Blend::Premultiplied:
        glEnable(GL_BLEND);
        pglBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                             GL_ONE_MINUS_SRC_ALPHA);
        break;
    case Blend::Additive:
        glEnable(GL_BLEND);
        pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        break;
    case Blend::Opaque:
    case Blend::DepthOnly:
        glDisable(GL_BLEND);
        break;
    }
    // Färgmasken: DepthOnly skriver ingen färg — bara djupet.
    const GLboolean color = b == Blend::DepthOnly ? GL_FALSE : GL_TRUE;
    glColorMask(color, color, color, color);
    glBlend_ = static_cast<int>(b);
}

void GlRenderer::setState(GlTexture tex, unsigned topo, ImageFilter filter)
{
    if (tex != curTex_ || topo != curTopo_ || filter != curFilter_)
        flush();
    curTex_    = tex;
    curTopo_   = topo;
    curFilter_ = filter;
    if (batch_.size() + 6 > kMaxVerts)
        flush();
}

void GlRenderer::bindSampler(ImageFilter f)
{
    pglBindSampler(0, f == ImageFilter::Linear ? samplerLinear_
                                               : samplerNearest_);
}

void GlRenderer::quad(const Rectf& d, const Rectf& uv, Color c)
{
    const std::uint32_t col = pack(c);
    const float x1 = d.x, y1 = d.y, x2 = d.x + d.w, y2 = d.y + d.h;
    const float u1 = uv.x, v1 = uv.y, u2 = uv.x + uv.w, v2 = uv.y + uv.h;
    batch_.push_back({x1, y1, u1, v1, col});
    batch_.push_back({x2, y1, u2, v1, col});
    batch_.push_back({x1, y2, u1, v2, col});
    batch_.push_back({x2, y1, u2, v1, col});
    batch_.push_back({x2, y2, u2, v2, col});
    batch_.push_back({x1, y2, u1, v2, col});
}

void GlRenderer::sdfQuad(const Rectf& d, Color c, float mode,
                         const Rectf& prim, float radius, float param,
                         float offX, float offY)
{
    Vertex v{};
    v.rgba  = pack(c);
    v.p0[0] = prim.x + prim.w * 0.5f;   // centrum
    v.p0[1] = prim.y + prim.h * 0.5f;
    v.p0[2] = prim.w * 0.5f;            // halvmått
    v.p0[3] = prim.h * 0.5f;
    v.p1[0] = radius;
    v.p1[1] = param;
    v.p1[2] = offX;
    v.p1[3] = offY;
    v.mode  = mode;

    const float x1 = d.x, y1 = d.y, x2 = d.x + d.w, y2 = d.y + d.h;
    const float xs[6] = {x1, x2, x1, x2, x2, x1};
    const float ys[6] = {y1, y1, y2, y1, y2, y2};
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        batch_.push_back(v);
    }
}

// ---------------------------------------------------------------- klipp

static float clampRadius(const Rectf& r, float radius);   // se rit-API:t

// GLSL-arrayens längd är skriven som en literal i shaderkällan — den kan
// inte läsa en C++-konstant, så låset står här i stället.
static_assert(kMaxRoundedClips == 4,
              "uClipP0/uClipP1 i kFsSrc är deklarerade [4]");

void GlRenderer::setClipMasks(const RoundedClip* masks, std::size_t count)
{
    // DET VANLIGA FALLET ÄR GRATIS: inga rundade masker nu och inga förut
    // ⇒ ingen generation stegas, och då gör inget program ett enda GL-anrop.
    if (count == 0 && clipCount_ == 0)
        return;

    for (float& f : clipP0_)
        f = 0.0f;
    for (float& f : clipP1_)
        f = 0.0f;
    if (count > kMaxRoundedClips)
        count = kMaxRoundedClips;
    for (std::size_t i = 0; i < count; ++i) {
        const Rectf& m = masks[i].rect;
        clipP0_[i * 4 + 0] = m.x + m.w * 0.5f;   // centrum
        clipP0_[i * 4 + 1] = m.y + m.h * 0.5f;
        clipP0_[i * 4 + 2] = m.w * 0.5f;         // halvmått
        clipP0_[i * 4 + 3] = m.h * 0.5f;
        // Samma klampning som fasadens övriga radier (halva kortsidan) —
        // en radie större än så ger artefakter i sdRoundRect.
        clipP1_[i * 4 + 0] = clampRadius(m, masks[i].radius);
    }
    clipCount_ = static_cast<int>(count);
    ++clipGen_;
}

// Maskerna till det BUNDNA programmet, om dess generation är gammal. Anropas
// av den som just ska rita — UI:ts flush och scenens drawMesh.
void GlRenderer::syncClipMasks(int p0Loc, int p1Loc, int nLoc,
                               unsigned& programGen)
{
    if (programGen == clipGen_)
        return;
    pglUniform4fv(p0Loc, static_cast<GLsizei>(kMaxRoundedClips), clipP0_);
    pglUniform4fv(p1Loc, static_cast<GLsizei>(kMaxRoundedClips), clipP1_);
    pglUniform1i(nLoc, static_cast<GLint>(clipCount_));
    programGen = clipGen_;
}

void GlRenderer::applyClip(const Rectf* r, const RoundedClip* masks,
                           std::size_t count)
{
    if (!ready())
        return;
    // Scissor är context-state — flusha så allt ritat före bytet får
    // gamla klippet (samma mönster som textur-/topologibyte).
    flush();
    // RUNDADE MASKER — lagras här och laddas upp till det program som ritar
    // härnäst (maskgenerationen, se setClipMasks).
    setClipMasks(masks, count);
    if (!r) {
        glDisable(GL_SCISSOR_TEST);
        scissorOn_ = false;
        return;
    }
    // Runda UTÅT till hela pixlar (delvis täckta pixlar ingår) och flippa
    // y — glScissor räknar nerifrån (y-flipp nr 1): sy = målets höjd −
    // bottom. MÅLETS höjd, inte fönstrets: ett render-mål har sin egen.
    // I backbufferfasen med en storleksmarginal (E2L) ligger klientytan
    // `bbOrigin_` px in från båda kanterna, och backbuffern är 2m högre —
    // sy blir (höjd + 2m) − (m + bottom), alltså höjd − bottom + m.
    const int left   = static_cast<int>(std::floor(r->x));
    const int top    = static_cast<int>(std::floor(r->y));
    const int right  = static_cast<int>(std::ceil(r->x + r->w));
    const int bottom = static_cast<int>(std::ceil(r->y + r->h));
    glScissor(left + bbOrigin_, targetH_ - bottom + bbOrigin_,
              std::max(0, right - left), std::max(0, bottom - top));
    glEnable(GL_SCISSOR_TEST);
    scissorOn_ = true;
}

// ---------------------------------------------------------------- rit-API

void GlRenderer::clear(Color c)
{
    if (!ready() || (offscreen_ && !bound_))
        return;   // ett offscreen-block utan bundet mål har inget att rensa
    flush();   // allt ritat före clear ska bort — bevara ordningen
    // glClear lyder färg- och djupmasken: ett scenanrop med DepthOnly eller
    // utan djupskrivning hade lämnat målet orensat. UI:ts läge har båda PÅ —
    // och det är det som gäller efter en rensning (ett scenanrop sätter sitt).
    applyUiState();
    // glClear respekterar scissor — clear:a HELA målet som D3D11:s
    // ClearRenderTargetView (klippet återställs efteråt).
    if (scissorOn_)
        glDisable(GL_SCISSOR_TEST);
    // PREMULTIPLICERAD: varje mål bär premultiplicerat innehåll (rak-alfa-
    // blandning mot en genomskinlig yta ger exakt det), så rensfärgen
    // lagras i samma form. Opak och helt genomskinlig är oförändrade.
    const float a = c.a / 255.0f;
    glClearColor(c.r / 255.0f * a, c.g / 255.0f * a, c.b / 255.0f * a, a);
    GLbitfield bits = GL_COLOR_BUFFER_BIT;
    if (bound_ && bound_->hasDepth()) {
        glClearDepth(1.0);   // ett mål med djup: det bortre planet
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(bits);
    if (scissorOn_)
        glEnable(GL_SCISSOR_TEST);
}

void GlRenderer::fillRect(const Rectf& r, Color c)
{
    setState(0, GL_TRIANGLES, ImageFilter::Nearest);
    quad(r, Rectf{0, 0, 0, 0}, c);   // uv 0 → vita texturens enda texel
}

void GlRenderer::strokeRect(const Rectf& r, Color c)
{
    // Fyra 1-px-stavar — exakt pixeltäckning i hörnen (d3d11-mönstret).
    fillRect(Rectf{r.x, r.y, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + r.h - 1.0f, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
    fillRect(Rectf{r.x + r.w - 1.0f, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
}

void GlRenderer::line(float x1, float y1, float x2, float y2, Color c)
{
    setState(0, GL_LINES, ImageFilter::Nearest);
    const std::uint32_t col = pack(c);
    // +0.5: träffa pixelcentrum så linjen inte hamnar mellan pixlar
    batch_.push_back({x1 + 0.5f, y1 + 0.5f, 0, 0, col});
    batch_.push_back({x2 + 0.5f, y2 + 0.5f, 0, 0, col});
}

static float clampRadius(const Rectf& r, float radius)
{
    const float m = std::min(r.w, r.h) * 0.5f;
    return radius < 0.0f ? 0.0f : (radius > m ? m : radius);
}

void GlRenderer::fillRoundedRect(const Rectf& r, float radius, Color c)
{
    setState(0, GL_TRIANGLES, ImageFilter::Nearest);
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 1.0f, r, clampRadius(r, radius), 0.0f, 0.0f, 0.0f);
}

void GlRenderer::strokeRoundedRect(const Rectf& r, float radius, Color c,
                                   float width)
{
    setState(0, GL_TRIANGLES, ImageFilter::Nearest);
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 2.0f, r, clampRadius(r, radius), width, 0.0f, 0.0f);
}

void GlRenderer::dropShadow(const Rectf& r, float radius,
                            float offsetX, float offsetY, float blur,
                            Color c)
{
    setState(0, GL_TRIANGLES, ImageFilter::Nearest);
    const Rectf s{r.x + offsetX, r.y + offsetY, r.w, r.h};
    const float b = blur < 0.5f ? 0.5f : blur;
    const Rectf q{s.x - b - 1.0f, s.y - b - 1.0f,
                  s.w + 2.0f * (b + 1.0f), s.h + 2.0f * (b + 1.0f)};
    sdfQuad(q, c, 3.0f, s, clampRadius(s, radius), b, 0.0f, 0.0f);
}

void GlRenderer::innerShadow(const Rectf& r, float radius,
                             float offsetX, float offsetY, float blur,
                             Color c)
{
    setState(0, GL_TRIANGLES, ImageFilter::Nearest);
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 4.0f, r, clampRadius(r, radius),
            blur < 0.5f ? 0.5f : blur, offsetX, offsetY);
}

float GlRenderer::drawText(Font& f, float x, float y,
                           const std::string& utf8, Color c)
{
    // Fonten kommer alltid ur samma backend — casten är säker per
    // konstruktion (som D3D11).
    return static_cast<FtFont&>(f).draw(*this, x, y, utf8, c);
}

// ------------------------------------------------------- textur-hjälpare

// Formatets GL-tal: internformat och pixelformat.
static GLint glInternalFormat(ImageFormat f)
{
    return f == ImageFormat::R8 ? GL_R8 : GL_RGBA8;
}
static GLenum glPixelFormat(ImageFormat f)
{
    return f == ImageFormat::R8 ? GL_RED : GL_RGBA;
}

GlTexture GlRenderer::createTexture(ImageFormat f, int w, int h,
                                    const void* px, std::size_t rowBytes,
                                    bool mipmaps)
{
    if (w <= 0 || h <= 0 || !makeCurrent())
        return 0;
    while (glGetError() != GL_NO_ERROR) {
        // ett gammalt fel får inte tas för texturens
    }
    GlTexture t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    // Radlängden i PIXLAR (stride / pixelstorlek) och justeringen 1, så att
    // varje stride som är en multipel av pixelstorleken går att uttrycka.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                  px ? static_cast<GLint>(rowBytes / bytesPerPixel(f)) : 0);
    glTexImage2D(GL_TEXTURE_2D, 0, glInternalFormat(f), w, h, 0,
                 glPixelFormat(f), GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const bool ok = glGetError() == GL_NO_ERROR;
    // Texturens EGNA parametrar: Nearest och kant. De gäller aldrig vid
    // ritning — samplerobjektet som binds per ritning skuggar dem (se
    // bindSampler) — men en textur utan mipmaps måste ha ett MIN-filter
    // utan mipmap för att vara komplett. Samplernas MIN-filter går genom
    // kedjan, så en textur UTAN kedja får bara nivå 0
    // (MAX_LEVEL 0) och är komplett för dem också.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipmaps ? 1000 : 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
    if (!ok) {
        log("GL: en textur på %d × %d kunde inte skapas", w, h);
        glDeleteTextures(1, &t);
        return 0;
    }
    return t;
}

void GlRenderer::updateTexture(GlTexture t, ImageFormat f, int x, int y,
                               int w, int h, const void* px,
                               std::size_t rowBytes)
{
    if (!t || !makeCurrent())
        return;
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                  static_cast<GLint>(rowBytes / bytesPerPixel(f)));
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, glPixelFormat(f),
                    GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
}

// Kedjan ur nivå 0 — vid skapandet, efter varje skrivning och när
// ett mål med kedja lämnas.
void GlRenderer::generateMips(GlTexture t)
{
    if (!t || !makeCurrent())
        return;
    glBindTexture(GL_TEXTURE_2D, t);
    pglGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
}

void GlRenderer::destroyTexture(GlTexture t)
{
    if (!t || !ready())
        return;
    makeCurrent();
    if (curTex_ == t)
        curTex_ = 0;
    glDeleteTextures(1, &t);
}

void GlRenderer::texturedQuad(GlTexture t, const Rectf& dst,
                              const Rectf& uv, Color c)
{
    // Glyferna är rasteriserade för hela pixlar och ritas 1:1 — NEAREST
    // uttryckligen, oberoende av vad en bild före dem samplades med.
    setState(t, GL_TRIANGLES, ImageFilter::Nearest);
    quad(dst, uv, c);
}

void GlRenderer::imageQuad(GlTexture t, const Rectf& dst, const Rectf& uv,
                           float opacity, float mode, ImageFilter filter)
{
    setState(t, GL_TRIANGLES, filter);
    // OPACITETEN i vertexfärgens alfa (minnesordning r,g,b,a — se `pack`);
    // shaderns mode 5–7 multiplicerar in den i bildens rak-alfa.
    const float a = std::clamp(opacity, 0.0f, 1.0f);
    Vertex v{};
    v.rgba = pack(Color{255, 255, 255,
                        static_cast<std::uint8_t>(std::lround(a * 255.0f))});
    v.mode = mode;
    const float x1 = dst.x, y1 = dst.y;
    const float x2 = dst.x + dst.w, y2 = dst.y + dst.h;
    const float u1 = uv.x, v1c = uv.y;
    const float u2 = uv.x + uv.w, v2c = uv.y + uv.h;
    const float xs[6] = {x1, x2, x1, x2, x2, x1};
    const float ys[6] = {y1, y1, y2, y1, y2, y2};
    const float us[6] = {u1, u2, u1, u2, u2, u1};
    const float vs[6] = {v1c, v1c, v2c, v1c, v2c, v2c};
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        v.u = us[i];
        v.v = vs[i];
        batch_.push_back(v);
    }
}

// ---------------------------------------------------------------- fonter

std::unique_ptr<Font> GlRenderer::loadFont(const std::string& path,
                                           float sizePt)
{
    // Returnerar alltid ett objekt — valid() avgör (fasadens kontrakt).
    auto font = std::make_unique<FtFont>();
    font->setRenderer(this);
    font->load(path, sizePt);
    return font;
}

std::unique_ptr<Font> GlRenderer::loadFont(FontFamily family, float sizePt,
                                           FontStyle style)
{
    auto font = std::make_unique<FtFont>();
    font->setRenderer(this);
    font->loadFamily(family, sizePt, style);
    return font;
}

} // namespace gfx
