#pragma once
// Det GL-programmen i backend_linux DELAR — UI-pipelinen
// (linux_renderer.cpp), scenens program (linux_scene/image/fill/glyph.cpp)
// och de egna programmen (linux_program.cpp): loadern för funktionerna nyare
// än GL 1.1, shaderkompileringen och klippmaskernas GLSL. Intern header —
// bara renderarens filer drar in den, och ALLA GL-anrop bor i dem.
//
// Funktionerna nyare än 1.1 laddas via eglGetProcAddress EN gång vid
// första kontexten (libGL:s export-ABI garanterar bara 1.2-eran).

#include <GL/gl.h>
#include <GL/glext.h>

#include <string>

namespace gfx {

extern PFNGLCREATESHADERPROC            pglCreateShader;
extern PFNGLSHADERSOURCEPROC            pglShaderSource;
extern PFNGLCOMPILESHADERPROC           pglCompileShader;
extern PFNGLGETSHADERIVPROC             pglGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC        pglGetShaderInfoLog;
extern PFNGLDELETESHADERPROC            pglDeleteShader;
extern PFNGLCREATEPROGRAMPROC           pglCreateProgram;
extern PFNGLATTACHSHADERPROC            pglAttachShader;
extern PFNGLLINKPROGRAMPROC             pglLinkProgram;
extern PFNGLGETPROGRAMIVPROC            pglGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC       pglGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC              pglUseProgram;
extern PFNGLDELETEPROGRAMPROC           pglDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC      pglGetUniformLocation;
extern PFNGLUNIFORM2FPROC               pglUniform2f;
extern PFNGLUNIFORM4FPROC               pglUniform4f;          // scenen
extern PFNGLUNIFORM1IPROC               pglUniform1i;
extern PFNGLUNIFORM4FVPROC              pglUniform4fv;         // klippmasker
extern PFNGLUNIFORMMATRIX4FVPROC        pglUniformMatrix4fv;   // scenen
extern PFNGLBINDATTRIBLOCATIONPROC      pglBindAttribLocation;
extern PFNGLGENBUFFERSPROC              pglGenBuffers;
extern PFNGLDELETEBUFFERSPROC           pglDeleteBuffers;
extern PFNGLBINDBUFFERPROC              pglBindBuffer;
extern PFNGLBUFFERDATAPROC              pglBufferData;
extern PFNGLBUFFERSUBDATAPROC           pglBufferSubData;      // scenen
extern PFNGLVERTEXATTRIBPOINTERPROC     pglVertexAttribPointer;
extern PFNGLVERTEXATTRIBIPOINTERPROC    pglVertexAttribIPointer;   // heltal
extern PFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;
extern PFNGLGENVERTEXARRAYSPROC         pglGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC      pglDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC         pglBindVertexArray;
extern PFNGLGENFRAMEBUFFERSPROC         pglGenFramebuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC      pglDeleteFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC         pglBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC    pglFramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC  pglCheckFramebufferStatus;
extern PFNGLBLENDFUNCSEPARATEPROC       pglBlendFuncSeparate;
extern PFNGLACTIVETEXTUREPROC           pglActiveTexture;
// SAMPLEROBJEKTEN (GL 3.3) — filtret är batch-tillstånd.
// Ett bundet samplerobjekt SKUGGAR texturens egna parametrar, så den som
// binder en sampler för en bild måste veta att nästa ritning binder sin.
extern PFNGLGENSAMPLERSPROC             pglGenSamplers;
extern PFNGLDELETESAMPLERSPROC          pglDeleteSamplers;
extern PFNGLBINDSAMPLERPROC             pglBindSampler;
extern PFNGLSAMPLERPARAMETERIPROC       pglSamplerParameteri;
// MIP-KEDJAN (GL 3.0) — nivåerna ur nivå 0, för bilder och mål med
// kedja.
extern PFNGLGENERATEMIPMAPPROC          pglGenerateMipmap;
// DJUP OCH MSAA I MÅLEN (GL 3.0): provtagna
// renderbuffertar för färgen och djupet, och blitten som löser upp dem.
extern PFNGLGENRENDERBUFFERSPROC                    pglGenRenderbuffers;
extern PFNGLDELETERENDERBUFFERSPROC                 pglDeleteRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC                    pglBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC      pglRenderbufferStorageMultisample;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC             pglFramebufferRenderbuffer;
extern PFNGLBLITFRAMEBUFFERPROC                     pglBlitFramebuffer;
// EGNA PROGRAM (GL 2.0–3.3): uniform-blocken bundna på
// NAMN (GLSL 330 saknar layout(binding)), instansieringen utan baseInstance
// och programmets aktiva attribut och uniformer — reflektionen.
extern PFNGLGETUNIFORMBLOCKINDEXPROC                pglGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC                 pglUniformBlockBinding;
extern PFNGLGETACTIVEUNIFORMBLOCKIVPROC             pglGetActiveUniformBlockiv;
extern PFNGLBINDBUFFERBASEPROC                      pglBindBufferBase;
extern PFNGLVERTEXATTRIBDIVISORPROC                 pglVertexAttribDivisor;
extern PFNGLDRAWARRAYSINSTANCEDPROC                 pglDrawArraysInstanced;
extern PFNGLDRAWELEMENTSINSTANCEDPROC               pglDrawElementsInstanced;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC            pglDisableVertexAttribArray;
extern PFNGLGETACTIVEATTRIBPROC                     pglGetActiveAttrib;
extern PFNGLGETATTRIBLOCATIONPROC                   pglGetAttribLocation;
extern PFNGLGETACTIVEUNIFORMPROC                    pglGetActiveUniform;
extern PFNGLGETACTIVEUNIFORMSIVPROC                 pglGetActiveUniformsiv;

// Laddar alla pekare ovan EN gång (kräver en aktuell kontext). false + logg
// om någon saknas — då finns ingen pipeline att bygga.
bool loadGlFunctions();

// GPU-TIDEN (GL 3.3 / ARB_timer_query) — tidsstämpelfrågorna.
// Laddas VALFRITT av `loadGlTimerQueries` (EN gång, en aktuell kontext): en
// drivrutin utan dem ritar men mäter ingen GPU-tid — false + logg.
extern PFNGLGENQUERIESPROC          pglGenQueries;
extern PFNGLDELETEQUERIESPROC       pglDeleteQueries;
extern PFNGLQUERYCOUNTERPROC        pglQueryCounter;
extern PFNGLGETQUERYOBJECTIVPROC    pglGetQueryObjectiv;
extern PFNGLGETQUERYOBJECTUI64VPROC pglGetQueryObjectui64v;
bool loadGlTimerQueries();

// Kompilerar en shader ur `count` delar i ordning (glShaderSource tar dem
// som de är — ingen sammanslagning behövs). 0 + logg vid fel — eller, med
// `error` (de egna programmen), kompilatorns logg dit i stället.
unsigned compileGlShader(unsigned type, const char* const* parts, int count,
                         std::string* error = nullptr);

// Länkar vs + fs med attribut i fasta platser: `attribs[i]` får plats i
// (inga för ett program vars källa bär layout(location)). Shaderobjekten
// raderas efter länkningen (programmet håller dem). 0 + logg vid fel — eller
// länkarens logg i `error`.
unsigned linkGlProgram(unsigned vs, unsigned fs, const char* const* attribs,
                       int count, std::string* error = nullptr);

// "#version 330 core\n" — första delen i varje shader som byggs av delar.
extern const char kGlslVersion[];

// Klippmaskernas GLSL — uniformerna `uClipP0`/`uClipP1`/`uClipN`,
// `sdRoundRect` och `clipCoverage(p)`, där p är fragmentets läge i målets
// px (varyingen vPix, aldrig gl_FragCoord — se linux_renderer.cpp). VARJE
// fragmentshader som ritar i ett mål med klippstacken klistrar in den
// direkt efter #version, annars gäller de rundade maskerna inte för
// programmet. Och målarens klipp: uniformerna
// `uSceneClip`/`uSceneClipN` och `sceneClip(p)`, som scenens program
// multiplicerar alfan med och som KASTAR fragmentet utanför.
extern const char kGlslClipSrc[];

// Färgens GLSL — uniformerna `uPaint0`/`uPaint1`, rampen
// `uRamp` (enhet 0) och `paintColor(p)`: 1 för enfärgat, rampens färg i
// världspunkten p för en gradient (pad). Klistras in efter klippmaskernas del
// i Fill-, Line- och Glyph-programmens fragmentshader.
extern const char kGlslPaintSrc[];

// De SKÄRMVÄNDA hörnens djup (markdjupet) —
// `glaze_standingZ(m, c, off)`, gfx/program.h: HLSL:ns kHlslStandSrc rad för
// rad. EN definition: i de egna programmens förspel (där ritytans
// Points-program når den, båda stegen) och efter #version och definitionerna
// i Line- och Glyph-programmens vertexshader.
extern const char kGlslStandSrc[];

// FÄRGJUSTERINGEN — `glaze_colorAdjust(c, a)`, gfx/program.h:
// HLSL:ns kHlslAdjustSrc rad för rad. EN definition: i de egna programmens
// förspel (där ritytans Mesh-program når den) och i Image-programmets
// fragmentshader.
extern const char kGlslAdjustSrc[];

} // namespace gfx
