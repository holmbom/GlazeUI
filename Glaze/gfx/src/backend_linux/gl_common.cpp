// Det GL-programmen i backend_linux delar — se gl_common.h. Loadern bor här
// och inte fil-lokalt i linux_renderer.cpp, så att scenens program
// (linux_scene.cpp) och renderaren delar ETT exemplar.

#include "gl_common.h"

#include <EGL/egl.h>

#include "gfx/platform.h"

namespace gfx {

PFNGLCREATESHADERPROC            pglCreateShader;
PFNGLSHADERSOURCEPROC            pglShaderSource;
PFNGLCOMPILESHADERPROC           pglCompileShader;
PFNGLGETSHADERIVPROC             pglGetShaderiv;
PFNGLGETSHADERINFOLOGPROC        pglGetShaderInfoLog;
PFNGLDELETESHADERPROC            pglDeleteShader;
PFNGLCREATEPROGRAMPROC           pglCreateProgram;
PFNGLATTACHSHADERPROC            pglAttachShader;
PFNGLLINKPROGRAMPROC             pglLinkProgram;
PFNGLGETPROGRAMIVPROC            pglGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC       pglGetProgramInfoLog;
PFNGLUSEPROGRAMPROC              pglUseProgram;
PFNGLDELETEPROGRAMPROC           pglDeleteProgram;
PFNGLGETUNIFORMLOCATIONPROC      pglGetUniformLocation;
PFNGLUNIFORM2FPROC               pglUniform2f;
PFNGLUNIFORM4FPROC               pglUniform4f;
PFNGLUNIFORM1IPROC               pglUniform1i;
PFNGLUNIFORM4FVPROC              pglUniform4fv;
PFNGLUNIFORMMATRIX4FVPROC        pglUniformMatrix4fv;
PFNGLBINDATTRIBLOCATIONPROC      pglBindAttribLocation;
PFNGLGENBUFFERSPROC              pglGenBuffers;
PFNGLDELETEBUFFERSPROC           pglDeleteBuffers;
PFNGLBINDBUFFERPROC              pglBindBuffer;
PFNGLBUFFERDATAPROC              pglBufferData;
PFNGLBUFFERSUBDATAPROC           pglBufferSubData;
PFNGLVERTEXATTRIBPOINTERPROC     pglVertexAttribPointer;
PFNGLVERTEXATTRIBIPOINTERPROC    pglVertexAttribIPointer;
PFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;
PFNGLGENVERTEXARRAYSPROC         pglGenVertexArrays;
PFNGLDELETEVERTEXARRAYSPROC      pglDeleteVertexArrays;
PFNGLBINDVERTEXARRAYPROC         pglBindVertexArray;
PFNGLGENFRAMEBUFFERSPROC         pglGenFramebuffers;
PFNGLDELETEFRAMEBUFFERSPROC      pglDeleteFramebuffers;
PFNGLBINDFRAMEBUFFERPROC         pglBindFramebuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC    pglFramebufferTexture2D;
PFNGLCHECKFRAMEBUFFERSTATUSPROC  pglCheckFramebufferStatus;
PFNGLBLENDFUNCSEPARATEPROC       pglBlendFuncSeparate;
PFNGLACTIVETEXTUREPROC           pglActiveTexture;
PFNGLGENSAMPLERSPROC             pglGenSamplers;
PFNGLDELETESAMPLERSPROC          pglDeleteSamplers;
PFNGLBINDSAMPLERPROC             pglBindSampler;
PFNGLSAMPLERPARAMETERIPROC       pglSamplerParameteri;
PFNGLGENERATEMIPMAPPROC          pglGenerateMipmap;
PFNGLGENRENDERBUFFERSPROC               pglGenRenderbuffers;
PFNGLDELETERENDERBUFFERSPROC            pglDeleteRenderbuffers;
PFNGLBINDRENDERBUFFERPROC               pglBindRenderbuffer;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC pglRenderbufferStorageMultisample;
PFNGLFRAMEBUFFERRENDERBUFFERPROC        pglFramebufferRenderbuffer;
PFNGLBLITFRAMEBUFFERPROC                pglBlitFramebuffer;
PFNGLGETUNIFORMBLOCKINDEXPROC           pglGetUniformBlockIndex;
PFNGLUNIFORMBLOCKBINDINGPROC            pglUniformBlockBinding;
PFNGLGETACTIVEUNIFORMBLOCKIVPROC        pglGetActiveUniformBlockiv;
PFNGLBINDBUFFERBASEPROC                 pglBindBufferBase;
PFNGLVERTEXATTRIBDIVISORPROC            pglVertexAttribDivisor;
PFNGLDRAWARRAYSINSTANCEDPROC            pglDrawArraysInstanced;
PFNGLDRAWELEMENTSINSTANCEDPROC          pglDrawElementsInstanced;
PFNGLDISABLEVERTEXATTRIBARRAYPROC       pglDisableVertexAttribArray;
PFNGLGETACTIVEATTRIBPROC                pglGetActiveAttrib;
PFNGLGETATTRIBLOCATIONPROC              pglGetAttribLocation;
PFNGLGETACTIVEUNIFORMPROC               pglGetActiveUniform;
PFNGLGETACTIVEUNIFORMSIVPROC            pglGetActiveUniformsiv;

bool loadGlFunctions()
{
    static bool loaded = false;
    static bool ok     = false;
    if (loaded)
        return ok;
    loaded = true;

    auto get = [](const char* name) {
        return eglGetProcAddress(name);
    };
#define GLZ_LOAD(var, name)                                     \
    var = reinterpret_cast<decltype(var)>(get(name));           \
    if (!(var)) {                                               \
        log("GL-loadern: %s saknas", name);                     \
        return false;                                           \
    }
    auto loadAll = [&]() -> bool {
        GLZ_LOAD(pglCreateShader, "glCreateShader")
        GLZ_LOAD(pglShaderSource, "glShaderSource")
        GLZ_LOAD(pglCompileShader, "glCompileShader")
        GLZ_LOAD(pglGetShaderiv, "glGetShaderiv")
        GLZ_LOAD(pglGetShaderInfoLog, "glGetShaderInfoLog")
        GLZ_LOAD(pglDeleteShader, "glDeleteShader")
        GLZ_LOAD(pglCreateProgram, "glCreateProgram")
        GLZ_LOAD(pglAttachShader, "glAttachShader")
        GLZ_LOAD(pglLinkProgram, "glLinkProgram")
        GLZ_LOAD(pglGetProgramiv, "glGetProgramiv")
        GLZ_LOAD(pglGetProgramInfoLog, "glGetProgramInfoLog")
        GLZ_LOAD(pglUseProgram, "glUseProgram")
        GLZ_LOAD(pglDeleteProgram, "glDeleteProgram")
        GLZ_LOAD(pglGetUniformLocation, "glGetUniformLocation")
        GLZ_LOAD(pglUniform2f, "glUniform2f")
        GLZ_LOAD(pglUniform4f, "glUniform4f")
        GLZ_LOAD(pglUniform1i, "glUniform1i")
        GLZ_LOAD(pglUniform4fv, "glUniform4fv")
        GLZ_LOAD(pglUniformMatrix4fv, "glUniformMatrix4fv")
        GLZ_LOAD(pglBindAttribLocation, "glBindAttribLocation")
        GLZ_LOAD(pglGenBuffers, "glGenBuffers")
        GLZ_LOAD(pglDeleteBuffers, "glDeleteBuffers")
        GLZ_LOAD(pglBindBuffer, "glBindBuffer")
        GLZ_LOAD(pglBufferData, "glBufferData")
        GLZ_LOAD(pglBufferSubData, "glBufferSubData")
        GLZ_LOAD(pglVertexAttribPointer, "glVertexAttribPointer")
        GLZ_LOAD(pglVertexAttribIPointer, "glVertexAttribIPointer")
        GLZ_LOAD(pglEnableVertexAttribArray, "glEnableVertexAttribArray")
        GLZ_LOAD(pglGenVertexArrays, "glGenVertexArrays")
        GLZ_LOAD(pglDeleteVertexArrays, "glDeleteVertexArrays")
        GLZ_LOAD(pglBindVertexArray, "glBindVertexArray")
        GLZ_LOAD(pglGenFramebuffers, "glGenFramebuffers")
        GLZ_LOAD(pglDeleteFramebuffers, "glDeleteFramebuffers")
        GLZ_LOAD(pglBindFramebuffer, "glBindFramebuffer")
        GLZ_LOAD(pglFramebufferTexture2D, "glFramebufferTexture2D")
        GLZ_LOAD(pglCheckFramebufferStatus, "glCheckFramebufferStatus")
        GLZ_LOAD(pglBlendFuncSeparate, "glBlendFuncSeparate")
        GLZ_LOAD(pglActiveTexture, "glActiveTexture")
        GLZ_LOAD(pglGenSamplers, "glGenSamplers")
        GLZ_LOAD(pglDeleteSamplers, "glDeleteSamplers")
        GLZ_LOAD(pglBindSampler, "glBindSampler")
        GLZ_LOAD(pglSamplerParameteri, "glSamplerParameteri")
        GLZ_LOAD(pglGenerateMipmap, "glGenerateMipmap")
        GLZ_LOAD(pglGenRenderbuffers, "glGenRenderbuffers")
        GLZ_LOAD(pglDeleteRenderbuffers, "glDeleteRenderbuffers")
        GLZ_LOAD(pglBindRenderbuffer, "glBindRenderbuffer")
        GLZ_LOAD(pglRenderbufferStorageMultisample,
                 "glRenderbufferStorageMultisample")
        GLZ_LOAD(pglFramebufferRenderbuffer, "glFramebufferRenderbuffer")
        GLZ_LOAD(pglBlitFramebuffer, "glBlitFramebuffer")
        GLZ_LOAD(pglGetUniformBlockIndex, "glGetUniformBlockIndex")
        GLZ_LOAD(pglUniformBlockBinding, "glUniformBlockBinding")
        GLZ_LOAD(pglGetActiveUniformBlockiv, "glGetActiveUniformBlockiv")
        GLZ_LOAD(pglBindBufferBase, "glBindBufferBase")
        GLZ_LOAD(pglVertexAttribDivisor, "glVertexAttribDivisor")
        GLZ_LOAD(pglDrawArraysInstanced, "glDrawArraysInstanced")
        GLZ_LOAD(pglDrawElementsInstanced, "glDrawElementsInstanced")
        GLZ_LOAD(pglDisableVertexAttribArray, "glDisableVertexAttribArray")
        GLZ_LOAD(pglGetActiveAttrib, "glGetActiveAttrib")
        GLZ_LOAD(pglGetAttribLocation, "glGetAttribLocation")
        GLZ_LOAD(pglGetActiveUniform, "glGetActiveUniform")
        GLZ_LOAD(pglGetActiveUniformsiv, "glGetActiveUniformsiv")
        return true;
    };
#undef GLZ_LOAD
    ok = loadAll();
    return ok;
}

PFNGLGENQUERIESPROC          pglGenQueries;
PFNGLDELETEQUERIESPROC       pglDeleteQueries;
PFNGLQUERYCOUNTERPROC        pglQueryCounter;
PFNGLGETQUERYOBJECTIVPROC    pglGetQueryObjectiv;
PFNGLGETQUERYOBJECTUI64VPROC pglGetQueryObjectui64v;

bool loadGlTimerQueries()
{
    static bool loaded = false;
    static bool ok     = false;
    if (loaded)
        return ok;
    loaded = true;
    // VERSIONEN FÖRST: eglGetProcAddress får svara med en pekare också för en
    // funktion drivrutinen inte har, så en adress bevisar ingenting.
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 3)) {
        log("GL-loadern: GL %d.%d har inga tidsstämpelfrågor (3.3) — GPU-tiden "
            "mäts inte",
            major, minor);
        return false;
    }
    pglGenQueries = reinterpret_cast<PFNGLGENQUERIESPROC>(
        eglGetProcAddress("glGenQueries"));
    pglDeleteQueries = reinterpret_cast<PFNGLDELETEQUERIESPROC>(
        eglGetProcAddress("glDeleteQueries"));
    pglQueryCounter = reinterpret_cast<PFNGLQUERYCOUNTERPROC>(
        eglGetProcAddress("glQueryCounter"));
    pglGetQueryObjectiv = reinterpret_cast<PFNGLGETQUERYOBJECTIVPROC>(
        eglGetProcAddress("glGetQueryObjectiv"));
    pglGetQueryObjectui64v = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VPROC>(
        eglGetProcAddress("glGetQueryObjectui64v"));
    ok = pglGenQueries && pglDeleteQueries && pglQueryCounter &&
         pglGetQueryObjectiv && pglGetQueryObjectui64v;
    if (!ok)
        log("GL-loadern: tidsstämpelfrågorna saknas — GPU-tiden mäts inte");
    return ok;
}

namespace {

// Loggens text utan avslutande radslut (kompilatorerna lägger till dem).
std::string trimmed(const char* buf)
{
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

} // namespace

unsigned compileGlShader(unsigned type, const char* const* parts, int count,
                         std::string* error)
{
    const unsigned s = pglCreateShader(type);
    pglShaderSource(s, count, parts, nullptr);
    pglCompileShader(s);
    GLint ok = 0;
    pglGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[4096] = {};
        pglGetShaderInfoLog(s, sizeof buf, nullptr, buf);
        if (error)
            *error = trimmed(buf);
        else
            log("GLSL-kompilering misslyckades: %s", buf);
        pglDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned linkGlProgram(unsigned vs, unsigned fs, const char* const* attribs,
                       int count, std::string* error)
{
    const unsigned prog = pglCreateProgram();
    pglAttachShader(prog, vs);
    pglAttachShader(prog, fs);
    for (int i = 0; i < count; ++i)
        pglBindAttribLocation(prog, static_cast<GLuint>(i), attribs[i]);
    pglLinkProgram(prog);
    pglDeleteShader(vs);
    pglDeleteShader(fs);
    GLint ok = 0;
    pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[4096] = {};
        pglGetProgramInfoLog(prog, sizeof buf, nullptr, buf);
        if (error)
            *error = trimmed(buf);
        else
            log("GLSL-länkning misslyckades: %s", buf);
        pglDeleteProgram(prog);
        return 0;
    }
    return prog;
}

const char kGlslVersion[] = "#version 330 core\n";

const char kGlslClipSrc[] = R"glsl(
// Avstånd till en rundad rektangel — identisk med HLSL:ns sdRoundRect.
float sdRoundRect(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// RUNDADE KLIPPMASKER — klippstackens
// rundade nivåer som UNIFORM-state. Konventionen är EXAKT SDF-vertexens:
// p0 = centrum.xy + halvmått.zw, p1.x = radien. Arrayens längd MÅSTE
// matcha gfx::kMaxRoundedClips (static_assert i linux_renderer.cpp).
// Uniformer är PER PROGRAM i GL — varje program som klistrar in den här
// delen får maskerna uppladdade när det tas i bruk (maskgenerationen).
uniform vec4 uClipP0[4];
uniform vec4 uClipP1[4];
uniform int  uClipN;

// Produkten av täckningarna: 0 så fort EN mask är 0, och 1 bara där alla
// är 1. uClipN == 0 (det vanliga fallet) hoppar över loopen helt.
float clipCoverage(vec2 p)
{
    float cov = 1.0;
    for (int i = 0; i < uClipN; ++i) {
        float d = sdRoundRect(p - uClipP0[i].xy, uClipP0[i].zw, uClipP1[i].x);
        cov *= clamp(0.5 - d, 0.0, 1.0);
    }
    return cov;
}

// MÅLARENS KLIPP (gfx::SceneClip) — kantplanen (nx, ny, d) i målets
// px, positivt inne, som uniformer PER PROGRAM (`setSceneClip`); bara
// scenens program läser dem. Arrayens längd MÅSTE matcha gfx::kMaxClipPlanes.
uniform vec4 uSceneClip[8];
uniform int  uSceneClipN;   // 0 = inget klipp

// HLSL:ns sceneClip rad för rad: närmaste plans avstånd + ½, och där
// täckningen är 0 KASTAS fragmentet — varken färg eller djup. p som
// clipCoverage, aldrig gl_FragCoord.
float sceneClip(vec2 p)
{
    if (uSceneClipN == 0)
        return 1.0;
    float d = 1.0e30;
    for (int i = 0; i < uSceneClipN; ++i)
        d = min(d, dot(uSceneClip[i].xy, p) + uSceneClip[i].z);
    float cov = clamp(d + 0.5, 0.0, 1.0);
    if (cov <= 0.0)
        discard;
    return cov;
}
)glsl";

const char kGlslPaintSrc[] = R"glsl(
// FÄRGEN — samma som HLSL:ns kHlslPaintSrc.
uniform vec4 uPaint0;   // linjär: från.xy, till.zw; radiell: mitt.xy, r0, r1
uniform vec4 uPaint1;   // .x färgens slag (0 enfärgad, 1 linjär, 2 radiell),
                        // .y opaciteten, .zw per program
uniform sampler2D uRamp;   // enhet 0

vec4 paintColor(vec2 p)
{
    if (uPaint1.x < 0.5)
        return vec4(1.0);
    float t;
    if (uPaint1.x < 1.5) {
        vec2 ab = uPaint0.zw - uPaint0.xy;
        t = dot(p - uPaint0.xy, ab) / max(dot(ab, ab), 1e-20);
    } else {
        t = (length(p - uPaint0.xy) - uPaint0.z) /
            max(uPaint0.w - uPaint0.z, 1e-20);
    }
    // Texelcentrum i ändarna: t = 0 och t = 1 är ändstoppens färg exakt.
    return texture(uRamp, vec2((clamp(t, 0.0, 1.0) * 255.0 + 0.5) / 256.0,
                               0.5));
}
)glsl";

const char kGlslStandSrc[] = R"glsl(
// ETT SKÄRMVÄNT HÖRNS DJUP — HLSL:ns glaze_standingZ (kHlslStandSrc) rad för
// rad: hörnet `off` px från ankarets pixelklipp `c` (D3D:s djup [0, w] —
// toNdc gör GL:s av det) får djupet av planet genom ankaret parallellt med
// xy-planet där det ligger närmare än ankaret, och draget framåt är en
// ANDEL av ankarets djup: helt upp till 1/4, inget från 1/2.
float glaze_standingZ(mat4 m, vec4 c, vec2 off)
{
    if (c.w <= 0.0 || c.z > c.w)
        return c.z;
    vec4  mx  = m * vec4(1.0, 0.0, 0.0, 0.0);
    vec4  my  = m * vec4(0.0, 1.0, 0.0, 0.0);
    vec2  p   = c.xy / c.w + off;
    vec2  u   = mx.xy - p * mx.w;
    vec2  v   = my.xy - p * my.w;
    float det = u.x * v.y - u.y * v.x;
    if (abs(det) <= 1e-6 * length(u) * length(v))
        return c.z;
    vec2  r  = off * c.w;
    float a  = (r.x * v.y - r.y * v.x) / det;
    float b  = (u.x * r.y - u.y * r.x) / det;
    float wq = c.w + a * mx.w + b * my.w;
    if (wq <= 1e-6 * c.w)
        return c.z;
    float zq   = (c.z + a * mx.z + b * my.z) / wq;
    float z0   = c.z / c.w;
    float drag = 1.0 - wq / c.w;
    float k    = clamp((0.5 - drag) / 0.25, 0.0, 1.0);
    return mix(z0, min(z0, max(zq, 0.0)), k) * c.w;
}
)glsl";

const char kGlslAdjustSrc[] = R"glsl(
// FÄRGJUSTERINGEN av en RAK färg 0–1 — HLSL:ns glaze_colorAdjust
// rad för rad: a = (mättnad, kontrast, ljushet); ljusheten multiplicerar,
// kontrasten sträcker kring mittgrått 0,5 och mättnaden blandar mot
// luminansen (Rec. 709), klampad efter varje steg. Ljusheten c·k och de två
// andra c·k + p·(1 − k): (1, 1, 1) är EXAKT identiteten.
vec3 glaze_colorAdjust(vec3 c, vec3 a)
{
    c = clamp(c * a.z, 0.0, 1.0);
    c = clamp(c * a.y + 0.5 * (1.0 - a.y), 0.0, 1.0);
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return clamp(c * a.x + lum * (1.0 - a.x), 0.0, 1.0);
}
)glsl";

} // namespace gfx
