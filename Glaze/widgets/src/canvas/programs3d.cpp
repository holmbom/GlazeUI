// Ritytans inbyggda 3D-program — modellen och
// kontrakten står i programs3d.h. Här: källorna i tre språk, fabriken
// (beskrivningen, strömmarnas form, texturernas platser), packningen och
// blockens fyllare. HLSL och GLSL grindas i pixlar av
// tests/mesh_program_gate.cpp; MSL följs upp i
// doc/os/macos/TESTS.md.
//
// KÄLLORNA följer fasadens kontrakt (gfx/program.h): förspelet ger
// GlazeFrame som `glaze`, `glaze_toClip` och färgjusteringen
// `glaze_colorAdjust` (Image-programmets egen), platserna är ATTR<N> /
// layout(location = N) / [[attribute(N)]], GlazeUser ligger i b4 /
// bindningspunkt 4 / buffer(3) och texturerna på de platser fabriken
// definierar (`TEX_T`/`TEX_S` i HLSL, `TEX_SAMPLER` i GLSL och `TEX_SLOT` i
// MSL — likadant för RAMP och DRAPE). Tre saker skiljer språken åt och står
// vid koden: den platta normalens tecken (skärmens y-riktning), MSL:s block
// som argument och GLSL:s två källor (det delade blocket först i båda).

#include "programs3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <gfx/image.h>

namespace glaze::detail {
namespace {

// lang_gate:av shaderkällor och förprocessorns namn — kod till GPU:n, ingen användartext

// ================================================================== MESH

const char kMeshHlsl[] = R"hlsl(
// Mesh-programmet (glaze) — HLSL, vs_4_0/ps_4_0.
struct MeshUser
{
    float4 model[3];
    float4 normalRows[3];
    float4 color;
    float4 light;
    float4 toLight;
    float4 rimDir;
    float4 fogColor;
    float4 fog;
    float4 surface;
    float4 size;
    float4 ramp;
    float4 textures;
    float4 drape;
    float4 adjust;
};
cbuffer GlazeUser : register(b4)
{
    MeshUser u;
};
#if defined(HAS_UV)
Texture2D    gTex   : register(TEX_T);
SamplerState gTexS  : register(TEX_S);
#endif
#if defined(HAS_SCALAR)
Texture2D    gRamp  : register(RAMP_T);
SamplerState gRampS : register(RAMP_S);
#endif
#if defined(DRAPED)
Texture2D    gDrape  : register(DRAPE_T);
SamplerState gDrapeS : register(DRAPE_S);
#endif

struct VsIn
{
    float3 pos : ATTR0;
#if defined(HAS_NORMAL)
    float4 normal : ATTR1;
#endif
#if defined(HAS_COLOR)
    float4 color : ATTR2;
#endif
#if defined(HAS_UV)
    float2 uv : ATTR3;
#endif
#if defined(HAS_SCALAR)
    float scalar : ATTR4;
#endif
#if defined(INSTANCED)
    float4 inst0 : ATTR9;
    float4 inst1 : ATTR10;
    float4 inst2 : ATTR11;
    float4 instColor : ATTR12;
#endif
};

struct VsOut
{
    float4 pos   : SV_Position;
    float3 world : TEXCOORD0;   // chunk-lokalt
    float4 color : TEXCOORD1;
#if defined(HAS_NORMAL)
    float3 normal : TEXCOORD2;
#endif
#if defined(HAS_UV)
    float2 uv : TEXCOORD3;
#endif
#if defined(HAS_SCALAR)
    float scalar : TEXCOORD4;
#endif
    nointerpolation float mirror : TEXCOORD5;
};

// En affin 3 × 4 som tre rader (x, y, z, förflyttning).
float3 rowsPoint(float4 r0, float4 r1, float4 r2, float3 p)
{
    return float3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w,
                  dot(r2.xyz, p) + r2.w);
}

VsOut vsMain(VsIn i)
{
    VsOut  o;
    float3 local  = i.pos;
    float  mirror = u.surface.z;
#if defined(INSTANCED)
    // Instansens 3 × 3 som kolumner — normalen tar invers-transponatet upp
    // till en positiv faktor: (b×c, c×a, a×b) · tecknet på determinanten.
    float3 ia    = float3(i.inst0.x, i.inst1.x, i.inst2.x);
    float3 ib    = float3(i.inst0.y, i.inst1.y, i.inst2.y);
    float3 ic    = float3(i.inst0.z, i.inst1.z, i.inst2.z);
    float  isign = dot(ia, cross(ib, ic)) < 0.0 ? -1.0 : 1.0;
    mirror *= isign;
#endif
#if defined(SCREEN_SIZED)
    // Ankaret är det lokala origots plats; världen per px på dess djup.
    float3 anchor = float3(u.model[0].w, u.model[1].w, u.model[2].w);
#if defined(INSTANCED)
    anchor = rowsPoint(i.inst0, i.inst1, i.inst2, anchor);
#endif
    float s = u.size.x;
    if (u.size.y < 1.5) {
        float perPx = max(glaze.pixelSize.x *
                              dot(anchor - glaze.eye.xyz, glaze.forward.xyz) +
                              glaze.pixelSize.y,
                          0.0);
        s *= (u.size.y > 0.5 ? glaze.scale.x : glaze.scale.y) * perPx;
    }
    local *= s;
#endif
    float3 p = rowsPoint(u.model[0], u.model[1], u.model[2], local);
#if defined(INSTANCED)
    p = rowsPoint(i.inst0, i.inst1, i.inst2, p);
#endif
    o.pos   = glaze_toClip(mul(glaze.viewProj, float4(p, 1.0)));
    o.world = p;
    float4 c = u.color;
#if defined(HAS_COLOR)
    c *= i.color;
#endif
#if defined(INSTANCED)
    c *= i.instColor.a > 0.0 ? i.instColor : float4(1.0, 1.0, 1.0, 1.0);
#endif
    o.color = c;
#if defined(HAS_NORMAL)
    float3 n = float3(dot(u.normalRows[0].xyz, i.normal.xyz),
                      dot(u.normalRows[1].xyz, i.normal.xyz),
                      dot(u.normalRows[2].xyz, i.normal.xyz));
#if defined(INSTANCED)
    n = (cross(ib, ic) * n.x + cross(ic, ia) * n.y + cross(ia, ib) * n.z) *
        isign;
#endif
    o.normal = n;
#endif
#if defined(HAS_UV)
    o.uv = i.uv;
#endif
#if defined(HAS_SCALAR)
    o.scalar = i.scalar;
#endif
    o.mirror = mirror;
    return o;
}

#if defined(HAS_SCALAR)
// Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
float4 rampColor(float s)
{
    float t = saturate((s - u.ramp.x) * u.ramp.y);
    float n = max(u.ramp.z, 1.0);
    return gRamp.SampleLevel(gRampS, float2((0.5 + t * (n - 1.0)) / n, 0.5),
                             0.0);
}
#endif

// Dimmans andel: avståndet till ögat (ortografiskt djupet längs blicken).
float fogAmount(float3 p)
{
    float3 d    = p - glaze.eye.xyz;
    float  dist = glaze.eye.w > 0.5 ? length(d) : dot(d, glaze.forward.xyz);
    float  f    = saturate((dist - u.fog.x) * u.fog.y);
    return f > 0.0 ? pow(f, u.fog.z) : 0.0;
}

float4 fsMain(VsOut i, bool front : SV_IsFrontFace) : SV_Target
{
    // Framsidan — en speglande transform vänder vindningen.
    bool fram = front != (i.mirror < 0.0);
    // Den platta normalen: D3D:s skärm-y växer NEDÅT, så cross(ddy, ddx)
    // pekar MOT ögat för varje synlig yta; vänd efter vindningen är den
    // ytans framsida.
    float3 mot = cross(ddy(i.world), ddx(i.world));
    float  ml  = dot(mot, mot);
    mot        = ml > 0.0 ? mot * rsqrt(ml) : float3(0.0, 0.0, 0.0);
    float3 platt = fram ? mot : -mot;
#if defined(HAS_NORMAL)
    float  nl = dot(i.normal, i.normal);
    float3 N  = nl > 1e-12 ? i.normal * rsqrt(nl) : platt;
#else
    float3 N = platt;
#endif
    if (u.rimDir.w > 0.5 && !fram)
        N = -N;   // tvåsidigt: baksidan belyses som framsidan

    float4 c = i.color;
#if defined(HAS_UV)
    float4 t = gTex.Sample(gTexS, float2(i.uv.x, u.textures.y > 0.5 ? 1.0 - i.uv.y
                                                               : i.uv.y));
    if (u.textures.x > 0.5)
        t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0, 0.0, 0.0);
    c *= t;
#endif
#if defined(HAS_SCALAR)
    c *= rampColor(i.scalar);
#endif
#if defined(DRAPED)
    // Draperingen ÖVER basen, premultiplicerat — rad 0 vid lådans norra kant.
    float2 duv = float2((i.world.x - u.drape.x) * u.drape.z,
                        (u.drape.y - i.world.y) * u.drape.w);
    float4 d   = gDrape.Sample(gDrapeS, float2(duv.x, u.textures.w > 0.5 ? 1.0 - duv.y
                                                                    : duv.y));
    if (u.textures.z < 0.5)
        d.rgb *= d.a;
    float a = d.a + c.a * (1.0 - d.a);
    c.rgb   = a > 0.0 ? (d.rgb + c.rgb * c.a * (1.0 - d.a)) / a : c.rgb;
    c.a     = a;
#endif
    // Färgjusteringen — förspelets, samma som Image-programmets.
    c.rgb = glaze_colorAdjust(c.rgb, u.adjust.xyz);
    if (u.toLight.w > 0.5) {
        float ndl  = dot(N, u.toLight.xyz);
        float diff = ndl > 0.0 ? pow(ndl, u.light.z) : 0.0;
        float rim  = max(dot(N, u.rimDir.xyz), 0.0);
        c.rgb = saturate(c.rgb * (u.light.x + u.light.y * diff + u.light.w * rim));
    }
    c.rgb = lerp(c.rgb, u.fogColor.rgb, fogAmount(i.world) * u.fogColor.a);
    c.a  *= u.surface.x * glaze.scale.z;
    return c;
}
)hlsl";

// GLSL: blocket och hjälparen först i BÅDA källorna (GL länkar två
// deklarationer av samma block — de ska vara identiska).
const char kMeshGlslCommon[] = R"glsl(
// Mesh-programmet (glaze) — GLSL 330.
layout(std140) uniform GlazeUser
{
    vec4 model[3];
    vec4 normalRows[3];
    vec4 color;
    vec4 light;
    vec4 toLight;
    vec4 rimDir;
    vec4 fogColor;
    vec4 fog;
    vec4 surface;
    vec4 size;
    vec4 ramp;
    vec4 textures;
    vec4 drape;
    vec4 adjust;
} u;

// En affin 3 × 4 som tre rader (x, y, z, förflyttning).
vec3 rowsPoint(vec4 r0, vec4 r1, vec4 r2, vec3 p)
{
    return vec3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w,
                dot(r2.xyz, p) + r2.w);
}
)glsl";

const char kMeshGlslVertex[] = R"glsl(
layout(location = 0) in vec3 aPos;
#if defined(HAS_NORMAL)
layout(location = 1) in vec4 aNormal;
#endif
#if defined(HAS_COLOR)
layout(location = 2) in vec4 aColor;
#endif
#if defined(HAS_UV)
layout(location = 3) in vec2 aUv;
#endif
#if defined(HAS_SCALAR)
layout(location = 4) in float aScalar;
#endif
#if defined(INSTANCED)
layout(location = 9)  in vec4 aInst0;
layout(location = 10) in vec4 aInst1;
layout(location = 11) in vec4 aInst2;
layout(location = 12) in vec4 aInstColor;
#endif

out vec3 vWorld;   // chunk-lokalt
out vec4 vColor;
#if defined(HAS_NORMAL)
out vec3 vNormal;
#endif
#if defined(HAS_UV)
out vec2 vUv;
#endif
#if defined(HAS_SCALAR)
out float vScalar;
#endif
flat out float vMirror;

void main()
{
    vec3  local  = aPos;
    float mirror = u.surface.z;
#if defined(INSTANCED)
    // Instansens 3 × 3 som kolumner — normalen tar invers-transponatet upp
    // till en positiv faktor: (b×c, c×a, a×b) · tecknet på determinanten.
    vec3  ia    = vec3(aInst0.x, aInst1.x, aInst2.x);
    vec3  ib    = vec3(aInst0.y, aInst1.y, aInst2.y);
    vec3  ic    = vec3(aInst0.z, aInst1.z, aInst2.z);
    float isign = dot(ia, cross(ib, ic)) < 0.0 ? -1.0 : 1.0;
    mirror *= isign;
#endif
#if defined(SCREEN_SIZED)
    // Ankaret är det lokala origots plats; världen per px på dess djup.
    vec3 anchor = vec3(u.model[0].w, u.model[1].w, u.model[2].w);
#if defined(INSTANCED)
    anchor = rowsPoint(aInst0, aInst1, aInst2, anchor);
#endif
    float s = u.size.x;
    if (u.size.y < 1.5) {
        float perPx = max(glaze.pixelSize.x *
                              dot(anchor - glaze.eye.xyz, glaze.forward.xyz) +
                              glaze.pixelSize.y,
                          0.0);
        s *= (u.size.y > 0.5 ? glaze.scale.x : glaze.scale.y) * perPx;
    }
    local *= s;
#endif
    vec3 p = rowsPoint(u.model[0], u.model[1], u.model[2], local);
#if defined(INSTANCED)
    p = rowsPoint(aInst0, aInst1, aInst2, p);
#endif
    gl_Position = glaze_toClip(glaze.viewProj * vec4(p, 1.0));
    vWorld      = p;
    vec4 c = u.color;
#if defined(HAS_COLOR)
    c *= aColor;
#endif
#if defined(INSTANCED)
    c *= aInstColor.a > 0.0 ? aInstColor : vec4(1.0);
#endif
    vColor = c;
#if defined(HAS_NORMAL)
    vec3 n = vec3(dot(u.normalRows[0].xyz, aNormal.xyz),
                  dot(u.normalRows[1].xyz, aNormal.xyz),
                  dot(u.normalRows[2].xyz, aNormal.xyz));
#if defined(INSTANCED)
    n = (cross(ib, ic) * n.x + cross(ic, ia) * n.y + cross(ia, ib) * n.z) *
        isign;
#endif
    vNormal = n;
#endif
#if defined(HAS_UV)
    vUv = aUv;
#endif
#if defined(HAS_SCALAR)
    vScalar = aScalar;
#endif
    vMirror = mirror;
}
)glsl";

const char kMeshGlslFragment[] = R"glsl(
in vec3 vWorld;
in vec4 vColor;
#if defined(HAS_NORMAL)
in vec3 vNormal;
#endif
#if defined(HAS_UV)
in vec2 vUv;
uniform sampler2D TEX_SAMPLER;
#endif
#if defined(HAS_SCALAR)
in float vScalar;
uniform sampler2D RAMP_SAMPLER;
#endif
#if defined(DRAPED)
uniform sampler2D DRAPE_SAMPLER;
#endif
flat in float vMirror;
out vec4 frag;

#if defined(HAS_SCALAR)
// Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
vec4 rampColor(float s)
{
    float t = clamp((s - u.ramp.x) * u.ramp.y, 0.0, 1.0);
    float n = max(u.ramp.z, 1.0);
    return textureLod(RAMP_SAMPLER, vec2((0.5 + t * (n - 1.0)) / n, 0.5), 0.0);
}
#endif

// Dimmans andel: avståndet till ögat (ortografiskt djupet längs blicken).
float fogAmount(vec3 p)
{
    vec3  d    = p - glaze.eye.xyz;
    float dist = glaze.eye.w > 0.5 ? length(d) : dot(d, glaze.forward.xyz);
    float f    = clamp((dist - u.fog.x) * u.fog.y, 0.0, 1.0);
    return f > 0.0 ? pow(f, u.fog.z) : 0.0;
}

void main()
{
    // Framsidan — en speglande transform vänder vindningen.
    bool fram = gl_FrontFacing != (vMirror < 0.0);
    // Den platta normalen: GL:s fönster-y växer UPPÅT, så cross(dFdx, dFdy)
    // pekar MOT ögat för varje synlig yta; vänd efter vindningen är den
    // ytans framsida.
    vec3  mot = cross(dFdx(vWorld), dFdy(vWorld));
    float ml  = dot(mot, mot);
    mot       = ml > 0.0 ? mot * inversesqrt(ml) : vec3(0.0);
    vec3 platt = fram ? mot : -mot;
#if defined(HAS_NORMAL)
    float nl = dot(vNormal, vNormal);
    vec3  N  = nl > 1e-12 ? vNormal * inversesqrt(nl) : platt;
#else
    vec3 N = platt;
#endif
    if (u.rimDir.w > 0.5 && !fram)
        N = -N;   // tvåsidigt: baksidan belyses som framsidan

    vec4 c = vColor;
#if defined(HAS_UV)
    vec4 t = texture(TEX_SAMPLER, vec2(vUv.x, u.textures.y > 0.5 ? 1.0 - vUv.y : vUv.y));
    if (u.textures.x > 0.5)
        t.rgb = t.a > 0.0 ? t.rgb / t.a : vec3(0.0);
    c *= t;
#endif
#if defined(HAS_SCALAR)
    c *= rampColor(vScalar);
#endif
#if defined(DRAPED)
    // Draperingen ÖVER basen, premultiplicerat — rad 0 vid lådans norra kant.
    vec2 duv = vec2((vWorld.x - u.drape.x) * u.drape.z,
                    (u.drape.y - vWorld.y) * u.drape.w);
    vec4 d   = texture(DRAPE_SAMPLER, vec2(duv.x, u.textures.w > 0.5 ? 1.0 - duv.y : duv.y));
    if (u.textures.z < 0.5)
        d.rgb *= d.a;
    float a = d.a + c.a * (1.0 - d.a);
    c.rgb   = a > 0.0 ? (d.rgb + c.rgb * c.a * (1.0 - d.a)) / a : c.rgb;
    c.a     = a;
#endif
    // Färgjusteringen — förspelets, samma som Image-programmets.
    c.rgb = glaze_colorAdjust(c.rgb, u.adjust.xyz);
    if (u.toLight.w > 0.5) {
        float ndl  = dot(N, u.toLight.xyz);
        float diff = ndl > 0.0 ? pow(ndl, u.light.z) : 0.0;
        float rim  = max(dot(N, u.rimDir.xyz), 0.0);
        c.rgb = clamp(c.rgb * (u.light.x + u.light.y * diff + u.light.w * rim),
                      0.0, 1.0);
    }
    c.rgb = mix(c.rgb, u.fogColor.rgb, fogAmount(vWorld) * u.fogColor.a);
    c.a  *= u.surface.x * glaze.scale.z;
    frag  = c;
}
)glsl";

// MSL. Blocken går som argument (MSL har inga globala), och
// texturerna är fragmentfunktionens parametrar.
const char kMeshMsl[] = R"msl(
// Mesh-programmet (glaze) — MSL.
struct MeshUser
{
    float4 model[3];
    float4 normalRows[3];
    float4 color;
    float4 light;
    float4 toLight;
    float4 rimDir;
    float4 fogColor;
    float4 fog;
    float4 surface;
    float4 size;
    float4 ramp;
    float4 textures;
    float4 drape;
    float4 adjust;
};

struct VsIn
{
    float3 pos [[attribute(0)]];
#if defined(HAS_NORMAL)
    float4 normal [[attribute(1)]];
#endif
#if defined(HAS_COLOR)
    float4 color [[attribute(2)]];
#endif
#if defined(HAS_UV)
    float2 uv [[attribute(3)]];
#endif
#if defined(HAS_SCALAR)
    float scalar [[attribute(4)]];
#endif
#if defined(INSTANCED)
    float4 inst0 [[attribute(9)]];
    float4 inst1 [[attribute(10)]];
    float4 inst2 [[attribute(11)]];
    float4 instColor [[attribute(12)]];
#endif
};

struct VsOut
{
    float4 pos [[position]];
    float3 world;   // chunk-lokalt
    float4 color;
#if defined(HAS_NORMAL)
    float3 normal;
#endif
#if defined(HAS_UV)
    float2 uv;
#endif
#if defined(HAS_SCALAR)
    float scalar;
#endif
    float mirror [[flat]];
};

// En affin 3 × 4 som tre rader (x, y, z, förflyttning).
static float3 rowsPoint(float4 r0, float4 r1, float4 r2, float3 p)
{
    return float3(dot(r0.xyz, p) + r0.w, dot(r1.xyz, p) + r1.w,
                  dot(r2.xyz, p) + r2.w);
}

vertex VsOut vsMain(VsIn i [[stage_in]],
                    constant GlazeFrame& glaze [[buffer(2)]],
                    constant MeshUser& u [[buffer(3)]])
{
    VsOut  o;
    float3 local  = i.pos;
    float  mirror = u.surface.z;
#if defined(INSTANCED)
    // Instansens 3 × 3 som kolumner — normalen tar invers-transponatet upp
    // till en positiv faktor: (b×c, c×a, a×b) · tecknet på determinanten.
    float3 ia    = float3(i.inst0.x, i.inst1.x, i.inst2.x);
    float3 ib    = float3(i.inst0.y, i.inst1.y, i.inst2.y);
    float3 ic    = float3(i.inst0.z, i.inst1.z, i.inst2.z);
    float  isign = dot(ia, cross(ib, ic)) < 0.0 ? -1.0 : 1.0;
    mirror *= isign;
#endif
#if defined(SCREEN_SIZED)
    // Ankaret är det lokala origots plats; världen per px på dess djup.
    float3 anchor = float3(u.model[0].w, u.model[1].w, u.model[2].w);
#if defined(INSTANCED)
    anchor = rowsPoint(i.inst0, i.inst1, i.inst2, anchor);
#endif
    float s = u.size.x;
    if (u.size.y < 1.5) {
        float perPx = max(glaze.pixelSize.x *
                              dot(anchor - glaze.eye.xyz, glaze.forward.xyz) +
                              glaze.pixelSize.y,
                          0.0);
        s *= (u.size.y > 0.5 ? glaze.scale.x : glaze.scale.y) * perPx;
    }
    local *= s;
#endif
    float3 p = rowsPoint(u.model[0], u.model[1], u.model[2], local);
#if defined(INSTANCED)
    p = rowsPoint(i.inst0, i.inst1, i.inst2, p);
#endif
    o.pos   = glaze_toClip(glaze.viewProj * float4(p, 1.0), glaze);
    o.world = p;
    float4 c = u.color;
#if defined(HAS_COLOR)
    c *= i.color;
#endif
#if defined(INSTANCED)
    c *= i.instColor.a > 0.0 ? i.instColor : float4(1.0);
#endif
    o.color = c;
#if defined(HAS_NORMAL)
    float3 n = float3(dot(u.normalRows[0].xyz, i.normal.xyz),
                      dot(u.normalRows[1].xyz, i.normal.xyz),
                      dot(u.normalRows[2].xyz, i.normal.xyz));
#if defined(INSTANCED)
    n = (cross(ib, ic) * n.x + cross(ic, ia) * n.y + cross(ia, ib) * n.z) *
        isign;
#endif
    o.normal = n;
#endif
#if defined(HAS_UV)
    o.uv = i.uv;
#endif
#if defined(HAS_SCALAR)
    o.scalar = i.scalar;
#endif
    o.mirror = mirror;
    return o;
}

// Dimmans andel: avståndet till ögat (ortografiskt djupet längs blicken).
static float fogAmount(float3 p, constant GlazeFrame& glaze,
                       constant MeshUser& u)
{
    float3 d    = p - glaze.eye.xyz;
    float  dist = glaze.eye.w > 0.5 ? length(d) : dot(d, glaze.forward.xyz);
    float  f    = saturate((dist - u.fog.x) * u.fog.y);
    return f > 0.0 ? pow(f, u.fog.z) : 0.0;
}

fragment float4 fsMain(VsOut i [[stage_in]],
                       bool front [[front_facing]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant MeshUser& u [[buffer(3)]]
#if defined(HAS_UV)
                       , texture2d<float> tex [[texture(TEX_SLOT)]]
                       , sampler texSampler [[sampler(TEX_SLOT)]]
#endif
#if defined(HAS_SCALAR)
                       , texture2d<float> ramp [[texture(RAMP_SLOT)]]
                       , sampler rampSampler [[sampler(RAMP_SLOT)]]
#endif
#if defined(DRAPED)
                       , texture2d<float> drapeTex [[texture(DRAPE_SLOT)]]
                       , sampler drapeSampler [[sampler(DRAPE_SLOT)]]
#endif
                       )
{
    // Framsidan — en speglande transform vänder vindningen.
    bool fram = front != (i.mirror < 0.0);
    // Den platta normalen: Metals fönster-y växer NEDÅT, som D3D:s, så
    // cross(dfdy, dfdx) pekar MOT ögat för varje synlig yta; vänd efter
    // vindningen är den ytans framsida.
    float3 mot = cross(dfdy(i.world), dfdx(i.world));
    float  ml  = dot(mot, mot);
    mot        = ml > 0.0 ? mot * rsqrt(ml) : float3(0.0);
    float3 platt = fram ? mot : -mot;
#if defined(HAS_NORMAL)
    float  nl = dot(i.normal, i.normal);
    float3 N  = nl > 1e-12 ? i.normal * rsqrt(nl) : platt;
#else
    float3 N = platt;
#endif
    if (u.rimDir.w > 0.5 && !fram)
        N = -N;   // tvåsidigt: baksidan belyses som framsidan

    float4 c = i.color;
#if defined(HAS_UV)
    float4 t = tex.sample(texSampler, float2(i.uv.x, u.textures.y > 0.5 ? 1.0 - i.uv.y
                                                                   : i.uv.y));
    if (u.textures.x > 0.5)
        t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0);
    c *= t;
#endif
#if defined(HAS_SCALAR)
    {
        // Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
        float rt = saturate((i.scalar - u.ramp.x) * u.ramp.y);
        float rn = max(u.ramp.z, 1.0);
        c *= ramp.sample(rampSampler, float2((0.5 + rt * (rn - 1.0)) / rn, 0.5),
                         level(0.0));
    }
#endif
#if defined(DRAPED)
    {
        // Draperingen ÖVER basen, premultiplicerat — rad 0 vid lådans norra
        // kant.
        float2 duv = float2((i.world.x - u.drape.x) * u.drape.z,
                            (u.drape.y - i.world.y) * u.drape.w);
        float4 d   = drapeTex.sample(drapeSampler,
                                     float2(duv.x, u.textures.w > 0.5 ? 1.0 - duv.y
                                                                 : duv.y));
        if (u.textures.z < 0.5)
            d.rgb *= d.a;
        float a = d.a + c.a * (1.0 - d.a);
        c.rgb   = a > 0.0 ? (d.rgb + c.rgb * c.a * (1.0 - d.a)) / a : c.rgb;
        c.a     = a;
    }
#endif
    // Färgjusteringen — förspelets, samma som Image-programmets.
    c.rgb = glaze_colorAdjust(c.rgb, u.adjust.xyz);
    if (u.toLight.w > 0.5) {
        float ndl  = dot(N, u.toLight.xyz);
        float diff = ndl > 0.0 ? pow(ndl, u.light.z) : 0.0;
        float rim  = max(dot(N, u.rimDir.xyz), 0.0);
        c.rgb = saturate(c.rgb * (u.light.x + u.light.y * diff + u.light.w * rim));
    }
    c.rgb = mix(c.rgb, u.fogColor.rgb, fogAmount(i.world, glaze, u) * u.fogColor.a);
    c.a  *= u.surface.x * glaze.scale.z;
    return c;
}
)msl";

// ================================================================ POINTS
//
// PUNKTENS TAK: ett hörn dras
// fram högst så många RADIER i världen (`POINT_STAND_K` i källorna) — utöver
// förspelets andel av djupet. Fyra täcker en markör på marken ned till
// ungefär 14° elevation (cot 14° ≈ 4), och ett tätt moln skrivs inte över
// av en granne längre bak.
constexpr const char* kPointStandK = "4.0";
//
// Hörnen ur vertexindexet: trianglarna (0, 2, 1) och (1, 2, 3) över hörnen
// k → (k & 1 ? +1 : −1, k & 2 ? +1 : −1) i målets px (y nedåt) — MOTURS sett
// från kameran, så en punkt överlever Cull::Back. Vertex 2, 3 och 5 ligger
// till höger, 1, 4 och 5 nedtill.

const char kPointsHlsl[] = R"hlsl(
// Points-programmet (glaze) — HLSL, vs_4_0/ps_4_0.
struct PointUser
{
    float4 color;
    float4 fogColor;
    float4 fog;
    float4 marker;
    float4 ramp;
};
cbuffer GlazeUser : register(b4)
{
    PointUser u;
};
#if defined(HAS_SCALAR)
Texture2D    gRamp  : register(RAMP_T);
SamplerState gRampS : register(RAMP_S);
#endif

struct VsIn
{
    float3 pos : ATTR0;
#if defined(HAS_COLOR)
    float4 color : ATTR2;
#endif
#if defined(HAS_SCALAR)
    float scalar : ATTR4;
#endif
};

struct VsOut
{
    float4 pos : SV_Position;
    noperspective float2 offset : TEXCOORD0;   // px från punktens mitt
    nointerpolation float4 color : TEXCOORD1;
    nointerpolation float2 shape : TEXCOORD2;  // radien i px, dimmans andel
#if defined(HAS_SCALAR)
    nointerpolation float scalar : TEXCOORD3;
#endif
};

// STÅENDE (markdjupet): hörnet `off` px från punktens pixelklipp `c` står
// på planet genom punkten (förspelets `glaze_standingZ`) — med ett TAK TILL
// för täta moln: aldrig närmare än POINT_STAND_K radier i världen
// (`rw`) framför punkten, djupet hos punkten flyttad så långt mot ögat längs
// blicken. En granne längre fram skrivs aldrig över.
float pointZ(float4 c, float2 off, float3 pos, float rw)
{
    float  z = glaze_standingZ(glaze.viewProj, c, off);
    float4 f = mul(glaze.viewProj,
                   float4(pos - glaze.forward.xyz * (POINT_STAND_K * rw), 1.0));
    return f.w > 0.0 ? max(z, f.z / f.w * c.w) : z;
}

VsOut vsMain(VsIn i, uint vid : SV_VertexID)
{
    VsOut  o;
    float4 c     = mul(glaze.viewProj, float4(i.pos, 1.0));
    float3 d     = i.pos - glaze.eye.xyz;
    float  depth = dot(d, glaze.forward.xyz);
    float  perPx = max(glaze.pixelSize.x * depth + glaze.pixelSize.y, 1e-30);
    float  px    = u.marker.y > 1.5
                       ? u.marker.x / perPx
                       : u.marker.x * (u.marker.y > 0.5 ? glaze.scale.x
                                                      : glaze.scale.y);
    float  r      = 0.5 * px;
    float2 corner = float2((vid == 2u || vid == 3u || vid == 5u) ? 1.0 : -1.0,
                           (vid == 1u || vid == 4u || vid == 5u) ? 1.0 : -1.0);
    float2 off    = corner * (r + 1.0);   // kantutjämningen når ½ px utanför
    if (c.w <= 0.0 || c.z < 0.0 || c.z > c.w)
        o.pos = float4(-2.0, -2.0, 0.0, 1.0);   // utanför och utan yta
    else
        o.pos = glaze_toClip(float4(c.xy + off * c.w,
                                    pointZ(c, off, i.pos, r * perPx), c.w));
    o.offset   = off;
    float4 col = u.color;
#if defined(HAS_COLOR)
    col *= i.color;
#endif
    o.color    = col;
    float dist = glaze.eye.w > 0.5 ? length(d) : depth;
    float f    = saturate((dist - u.fog.x) * u.fog.y);
    o.shape    = float2(r, (f > 0.0 ? pow(f, u.fog.z) : 0.0) * u.fogColor.a);
#if defined(HAS_SCALAR)
    o.scalar = i.scalar;
#endif
    return o;
}

#if defined(HAS_SCALAR)
// Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
float4 rampColor(float s)
{
    float t = saturate((s - u.ramp.x) * u.ramp.y);
    float n = max(u.ramp.z, 1.0);
    return gRamp.SampleLevel(gRampS, float2((0.5 + t * (n - 1.0)) / n, 0.5),
                             0.0);
}
#endif

float4 fsMain(VsOut i) : SV_Target
{
    // Avståndsfältet i px: cirkeln radien, kvadraten max-normen.
    float sd = u.marker.z > 0.5
                   ? length(i.offset) - i.shape.x
                   : max(abs(i.offset.x), abs(i.offset.y)) - i.shape.x;
    float4 c = i.color;
#if defined(HAS_SCALAR)
    c *= rampColor(i.scalar);
#endif
    c.rgb  = lerp(c.rgb, u.fogColor.rgb, i.shape.y);
    c.a   *= saturate(0.5 - sd) * u.marker.w * glaze.scale.z;
    if (c.a <= 0.0)
        discard;   // marginalen skriver inget djup
    return c;
}
)hlsl";

const char kPointsGlslCommon[] = R"glsl(
// Points-programmet (glaze) — GLSL 330.
layout(std140) uniform GlazeUser
{
    vec4 color;
    vec4 fogColor;
    vec4 fog;
    vec4 marker;
    vec4 ramp;
} u;
)glsl";

const char kPointsGlslVertex[] = R"glsl(
layout(location = 0) in vec3 aPos;
#if defined(HAS_COLOR)
layout(location = 2) in vec4 aColor;
#endif
#if defined(HAS_SCALAR)
layout(location = 4) in float aScalar;
#endif

noperspective out vec2 vOffset;   // px från punktens mitt
flat out vec4 vColor;
flat out vec2 vShape;             // radien i px, dimmans andel
#if defined(HAS_SCALAR)
flat out float vScalar;
#endif

// STÅENDE med punktens tak — HLSL-källans `pointZ` rad för rad.
float pointZ(vec4 c, vec2 off, vec3 pos, float rw)
{
    float z = glaze_standingZ(glaze.viewProj, c, off);
    vec4  f = glaze.viewProj *
              vec4(pos - glaze.forward.xyz * (POINT_STAND_K * rw), 1.0);
    return f.w > 0.0 ? max(z, f.z / f.w * c.w) : z;
}

void main()
{
    vec4  c     = glaze.viewProj * vec4(aPos, 1.0);
    vec3  d     = aPos - glaze.eye.xyz;
    float depth = dot(d, glaze.forward.xyz);
    float perPx = max(glaze.pixelSize.x * depth + glaze.pixelSize.y, 1e-30);
    float px    = u.marker.y > 1.5
                      ? u.marker.x / perPx
                      : u.marker.x * (u.marker.y > 0.5 ? glaze.scale.x
                                                     : glaze.scale.y);
    float r      = 0.5 * px;
    int   vid    = gl_VertexID;
    vec2  corner = vec2((vid == 2 || vid == 3 || vid == 5) ? 1.0 : -1.0,
                        (vid == 1 || vid == 4 || vid == 5) ? 1.0 : -1.0);
    vec2  off    = corner * (r + 1.0);   // kantutjämningen når ½ px utanför
    if (c.w <= 0.0 || c.z < 0.0 || c.z > c.w)
        gl_Position = vec4(-2.0, -2.0, 0.0, 1.0);   // utanför och utan yta
    else
        gl_Position = glaze_toClip(vec4(c.xy + off * c.w,
                                        pointZ(c, off, aPos, r * perPx), c.w));
    vOffset  = off;
    vec4 col = u.color;
#if defined(HAS_COLOR)
    col *= aColor;
#endif
    vColor     = col;
    float dist = glaze.eye.w > 0.5 ? length(d) : depth;
    float f    = clamp((dist - u.fog.x) * u.fog.y, 0.0, 1.0);
    vShape     = vec2(r, (f > 0.0 ? pow(f, u.fog.z) : 0.0) * u.fogColor.a);
#if defined(HAS_SCALAR)
    vScalar = aScalar;
#endif
}
)glsl";

const char kPointsGlslFragment[] = R"glsl(
noperspective in vec2 vOffset;
flat in vec4 vColor;
flat in vec2 vShape;
#if defined(HAS_SCALAR)
flat in float vScalar;
uniform sampler2D RAMP_SAMPLER;
#endif
out vec4 frag;

#if defined(HAS_SCALAR)
// Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
vec4 rampColor(float s)
{
    float t = clamp((s - u.ramp.x) * u.ramp.y, 0.0, 1.0);
    float n = max(u.ramp.z, 1.0);
    return textureLod(RAMP_SAMPLER, vec2((0.5 + t * (n - 1.0)) / n, 0.5), 0.0);
}
#endif

void main()
{
    // Avståndsfältet i px: cirkeln radien, kvadraten max-normen.
    float sd = u.marker.z > 0.5
                   ? length(vOffset) - vShape.x
                   : max(abs(vOffset.x), abs(vOffset.y)) - vShape.x;
    vec4 c = vColor;
#if defined(HAS_SCALAR)
    c *= rampColor(vScalar);
#endif
    c.rgb  = mix(c.rgb, u.fogColor.rgb, vShape.y);
    c.a   *= clamp(0.5 - sd, 0.0, 1.0) * u.marker.w * glaze.scale.z;
    if (c.a <= 0.0)
        discard;   // marginalen skriver inget djup
    frag = c;
}
)glsl";

const char kPointsMsl[] = R"msl(
// Points-programmet (glaze) — MSL.
struct PointUser
{
    float4 color;
    float4 fogColor;
    float4 fog;
    float4 marker;
    float4 ramp;
};

struct VsIn
{
    float3 pos [[attribute(0)]];
#if defined(HAS_COLOR)
    float4 color [[attribute(2)]];
#endif
#if defined(HAS_SCALAR)
    float scalar [[attribute(4)]];
#endif
};

struct VsOut
{
    float4 pos [[position]];
    float2 offset [[center_no_perspective]];   // px från punktens mitt
    float4 color [[flat]];
    float2 shape [[flat]];                     // radien i px, dimmans andel
#if defined(HAS_SCALAR)
    float scalar [[flat]];
#endif
};

// STÅENDE med punktens tak — HLSL-källans `pointZ` rad för rad.
static float pointZ(float4 c, float2 off, float3 pos, float rw,
                    constant GlazeFrame& glaze)
{
    float  z = glaze_standingZ(glaze.viewProj, c, off);
    float4 f = glaze.viewProj *
               float4(pos - glaze.forward.xyz * (POINT_STAND_K * rw), 1.0);
    return f.w > 0.0 ? max(z, f.z / f.w * c.w) : z;
}

vertex VsOut vsMain(VsIn i [[stage_in]],
                    uint vid [[vertex_id]],
                    constant GlazeFrame& glaze [[buffer(2)]],
                    constant PointUser& u [[buffer(3)]])
{
    VsOut  o;
    float4 c     = glaze.viewProj * float4(i.pos, 1.0);
    float3 d     = i.pos - glaze.eye.xyz;
    float  depth = dot(d, glaze.forward.xyz);
    float  perPx = max(glaze.pixelSize.x * depth + glaze.pixelSize.y, 1e-30);
    float  px    = u.marker.y > 1.5
                       ? u.marker.x / perPx
                       : u.marker.x * (u.marker.y > 0.5 ? glaze.scale.x
                                                      : glaze.scale.y);
    float  r      = 0.5 * px;
    float2 corner = float2((vid == 2u || vid == 3u || vid == 5u) ? 1.0 : -1.0,
                           (vid == 1u || vid == 4u || vid == 5u) ? 1.0 : -1.0);
    float2 off    = corner * (r + 1.0);   // kantutjämningen når ½ px utanför
    if (c.w <= 0.0 || c.z < 0.0 || c.z > c.w)
        o.pos = float4(-2.0, -2.0, 0.0, 1.0);   // utanför och utan yta
    else
        o.pos = glaze_toClip(
            float4(c.xy + off * c.w, pointZ(c, off, i.pos, r * perPx, glaze),
                   c.w),
            glaze);
    o.offset   = off;
    float4 col = u.color;
#if defined(HAS_COLOR)
    col *= i.color;
#endif
    o.color    = col;
    float dist = glaze.eye.w > 0.5 ? length(d) : depth;
    float f    = saturate((dist - u.fog.x) * u.fog.y);
    o.shape    = float2(r, (f > 0.0 ? pow(f, u.fog.z) : 0.0) * u.fogColor.a);
#if defined(HAS_SCALAR)
    o.scalar = i.scalar;
#endif
    return o;
}

fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant PointUser& u [[buffer(3)]]
#if defined(HAS_SCALAR)
                       , texture2d<float> ramp [[texture(RAMP_SLOT)]]
                       , sampler rampSampler [[sampler(RAMP_SLOT)]]
#endif
                       )
{
    // Avståndsfältet i px: cirkeln radien, kvadraten max-normen.
    float sd = u.marker.z > 0.5
                   ? length(i.offset) - i.shape.x
                   : max(abs(i.offset.x), abs(i.offset.y)) - i.shape.x;
    float4 c = i.color;
#if defined(HAS_SCALAR)
    {
        // Rampens texel i bär t = i / (n − 1): u = (½ + t · (n − 1)) / n.
        float rt = saturate((i.scalar - u.ramp.x) * u.ramp.y);
        float rn = max(u.ramp.z, 1.0);
        c *= ramp.sample(rampSampler, float2((0.5 + rt * (rn - 1.0)) / rn, 0.5),
                         level(0.0));
    }
#endif
    c.rgb  = mix(c.rgb, u.fogColor.rgb, i.shape.y);
    c.a   *= saturate(0.5 - sd) * u.marker.w * glaze.scale.z;
    if (c.a <= 0.0)
        discard_fragment();   // marginalen skriver inget djup
    return c;
}
)msl";

// ----------------------------------------------------------- definitionerna

using Defines = std::vector<std::pair<std::string, std::string>>;

// En texturroll på platsen `slot` (−1 = ingen): registren i HLSL
// (`<ROLL>_T` = tN, `<ROLL>_S` = sN), samplerns namn i GLSL
// (`<ROLL>_SAMPLER` = GlazeTextureN — GL binder samplers på namn) och platsen
// i MSL (`<ROLL>_SLOT` = N). Alla tre står i varje beskrivning; varje språk
// läser sin.
void defineSlot(Defines& defs, const std::string& role, int slot)
{
    if (slot < 0)
        return;
    const std::string n = std::to_string(slot);
    defs.emplace_back(role + "_T", "t" + n);
    defs.emplace_back(role + "_S", "s" + n);
    defs.emplace_back(role + "_SAMPLER", "GlazeTexture" + n);
    defs.emplace_back(role + "_SLOT", n);
}

// Varianten som definition och som del av namnet ("glaze_mesh.normal").
void defineFlag(gfx::ProgramDesc& d, bool on, const char* define,
                const char* suffix)
{
    if (!on)
        return;
    d.defines.emplace_back(define, "1");
    d.name += suffix;
}

} // namespace

gfx::ProgramDesc meshProgramDesc(const MeshVariant& v)
{
    gfx::ProgramDesc d;
    d.name = "glaze_mesh";
    defineFlag(d, v.normals, "HAS_NORMAL", ".normal");
    defineFlag(d, v.colors, "HAS_COLOR", ".color");
    defineFlag(d, v.uvs, "HAS_UV", ".uv");
    defineFlag(d, v.scalars, "HAS_SCALAR", ".scalar");
    defineFlag(d, v.instanced, "INSTANCED", ".instanced");
    defineFlag(d, v.screenSized, "SCREEN_SIZED", ".screen");
    defineFlag(d, v.draped, "DRAPED", ".draped");
    const ProgramSlots s = meshSlots(v);
    defineSlot(d.defines, "TEX", s.texture);
    defineSlot(d.defines, "RAMP", s.ramp);
    defineSlot(d.defines, "DRAPE", s.drape);
    d.hlsl         = kMeshHlsl;
    d.glslVertex   = std::string(kMeshGlslCommon) + kMeshGlslVertex;
    d.glslFragment = std::string(kMeshGlslCommon) + kMeshGlslFragment;
    d.msl          = kMeshMsl;
    d.userBytes    = sizeof(MeshUniforms);
    d.textures     = static_cast<std::uint8_t>(s.count);
    return d;
}

gfx::ProgramDesc pointProgramDesc(const PointVariant& v)
{
    gfx::ProgramDesc d;
    d.name = "glaze_points";
    defineFlag(d, v.colors, "HAS_COLOR", ".color");
    defineFlag(d, v.scalars, "HAS_SCALAR", ".scalar");
    defineSlot(d.defines, "RAMP", pointSlots(v).ramp);
    d.defines.emplace_back("POINT_STAND_K", kPointStandK);
    d.hlsl         = kPointsHlsl;
    d.glslVertex   = std::string(kPointsGlslCommon) + kPointsGlslVertex;
    d.glslFragment = std::string(kPointsGlslCommon) + kPointsGlslFragment;
    d.msl          = kPointsMsl;
    d.userBytes    = sizeof(PointUniforms);
    d.textures     = static_cast<std::uint8_t>(pointSlots(v).count);
    return d;
}

// lang_gate:på

// --------------------------------------------------------------- varianterna

std::uint32_t MeshVariant::key() const
{
    return (normals ? 1u : 0u) | (colors ? 2u : 0u) | (uvs ? 4u : 0u) |
           (scalars ? 8u : 0u) | (instanced ? 16u : 0u) |
           (screenSized ? 32u : 0u) | (draped ? 64u : 0u);
}

MeshVariant MeshVariant::fromKey(std::uint32_t k)
{
    MeshVariant v;
    v.normals     = (k & 1u) != 0;
    v.colors      = (k & 2u) != 0;
    v.uvs         = (k & 4u) != 0;
    v.scalars     = (k & 8u) != 0;
    v.instanced   = (k & 16u) != 0;
    v.screenSized = (k & 32u) != 0;
    v.draped      = (k & 64u) != 0;
    return v;
}

std::uint32_t PointVariant::key() const
{
    return (colors ? 1u : 0u) | (scalars ? 2u : 0u);
}

PointVariant PointVariant::fromKey(std::uint32_t k)
{
    PointVariant v;
    v.colors  = (k & 1u) != 0;
    v.scalars = (k & 2u) != 0;
    return v;
}

ProgramSlots meshSlots(const MeshVariant& v)
{
    ProgramSlots s;
    if (v.uvs)
        s.texture = s.count++;
    if (v.scalars)
        s.ramp = s.count++;
    if (v.draped)
        s.drape = s.count++;
    return s;
}

ProgramSlots pointSlots(const PointVariant& v)
{
    ProgramSlots s;
    if (v.scalars)
        s.ramp = s.count++;
    return s;
}

// --------------------------------------------------------------- strömmarna

namespace {

std::uint8_t at(std::uint8_t base, int extra)
{
    return static_cast<std::uint8_t>(base + extra);
}

void put(std::uint8_t*& out, const void* data, std::size_t bytes)
{
    std::memcpy(out, data, bytes);
    out += bytes;
}

// En normerad komponent som SNorm8: −1 … 1 → −127 … 127 (−128 används inte —
// D3D11 och GL 3.3 normerar den olika).
std::int8_t snorm8(float x)
{
    return static_cast<std::int8_t>(
        std::lround(std::clamp(x, -1.0f, 1.0f) * 127.0f));
}

} // namespace

gfx::VertexLayout meshLayout(const MeshVariant& v)
{
    using gfx::AttribFormat;
    namespace attrib = gfx::attrib;
    gfx::VertexLayout l;
    l.attribs.reserve(9);   // fem attribut + fyra instansens — utan reserv
                            // larmar GCC 14 -O2 falskt (-Wstringop-overflow)
    std::uint16_t     off = 0;
    const auto add = [&](std::uint8_t loc, AttribFormat f) {
        l.attribs.push_back(gfx::VertexAttrib{loc, f, off, 0});
        off = static_cast<std::uint16_t>(off + gfx::attribBytes(f));
    };
    add(attrib::kPosition, AttribFormat::Float3);
    if (v.normals)
        add(attrib::kNormal, AttribFormat::SNorm8x4);
    if (v.colors)
        add(attrib::kColor, AttribFormat::UNorm8x4);
    if (v.uvs)
        add(attrib::kUv, AttribFormat::Float2);
    if (v.scalars)
        add(attrib::kScalar, AttribFormat::Float);
    l.stride[0] = off;
    if (v.instanced) {
        l.attribs.push_back(
            gfx::VertexAttrib{attrib::kInstance0, AttribFormat::Float4, 0, 1});
        l.attribs.push_back(gfx::VertexAttrib{at(attrib::kInstance0, 1),
                                              AttribFormat::Float4, 16, 1});
        l.attribs.push_back(gfx::VertexAttrib{at(attrib::kInstance0, 2),
                                              AttribFormat::Float4, 32, 1});
        l.attribs.push_back(gfx::VertexAttrib{at(attrib::kInstance0, 3),
                                              AttribFormat::UNorm8x4, 48, 1});
        l.stride[1] = sizeof(MeshInstance);
    }
    return l;
}

gfx::VertexLayout pointLayout(const PointVariant& v)
{
    using gfx::AttribFormat;
    namespace attrib = gfx::attrib;
    gfx::VertexLayout l;
    l.attribs.reserve(3);   // som meshLayout: reserven tystar GCC 14:s falsklarm
    std::uint16_t     off = 0;
    const auto add = [&](std::uint8_t loc, AttribFormat f) {
        l.attribs.push_back(gfx::VertexAttrib{loc, f, off, 1});
        off = static_cast<std::uint16_t>(off + gfx::attribBytes(f));
    };
    add(attrib::kPosition, AttribFormat::Float3);
    if (v.colors)
        add(attrib::kColor, AttribFormat::UNorm8x4);
    if (v.scalars)
        add(attrib::kScalar, AttribFormat::Float);
    l.stride[1] = off;
    return l;
}

void packMeshVertex(const MeshVariant& v, const MeshVertex& in,
                    std::uint8_t* out)
{
    const float pos[3] = {in.position.x, in.position.y, in.position.z};
    put(out, pos, sizeof pos);
    if (v.normals) {
        const float len = std::sqrt(in.normal.x * in.normal.x +
                                    in.normal.y * in.normal.y +
                                    in.normal.z * in.normal.z);
        const float k   = len > 0.0f ? 1.0f / len : 0.0f;
        const std::int8_t n[4] = {snorm8(in.normal.x * k),
                                  snorm8(in.normal.y * k),
                                  snorm8(in.normal.z * k), 0};
        put(out, n, sizeof n);
    }
    if (v.colors) {
        const std::uint8_t c[4] = {in.color.r, in.color.g, in.color.b,
                                   in.color.a};
        put(out, c, sizeof c);
    }
    if (v.uvs) {
        const float uv[2] = {in.u, in.v};
        put(out, uv, sizeof uv);
    }
    if (v.scalars)
        put(out, &in.scalar, sizeof in.scalar);
}

void packPoint(const PointVariant& v, const PointVertex& in, std::uint8_t* out)
{
    const float pos[3] = {in.position.x, in.position.y, in.position.z};
    put(out, pos, sizeof pos);
    if (v.colors) {
        const std::uint8_t c[4] = {in.color.r, in.color.g, in.color.b,
                                   in.color.a};
        put(out, c, sizeof c);
    }
    if (v.scalars)
        put(out, &in.scalar, sizeof in.scalar);
}

// ------------------------------------------------------------------- blocken

namespace {

void color4(float (&dst)[4], gfx::Color c)
{
    dst[0] = c.r / 255.0f;
    dst[1] = c.g / 255.0f;
    dst[2] = c.b / 255.0f;
    dst[3] = c.a / 255.0f;
}

// En riktning normerad i double; noll (eller icke-ändlig) blir noll.
void unit3(float (&dst)[4], double x, double y, double z)
{
    const double len = std::sqrt(x * x + y * y + z * z);
    const bool   ok  = len > 0.0 && std::isfinite(len);
    dst[0] = ok ? static_cast<float>(x / len) : 0.0f;
    dst[1] = ok ? static_cast<float>(y / len) : 0.0f;
    dst[2] = ok ? static_cast<float>(z / len) : 0.0f;
}

void fog4(float (&color)[4], float (&fog)[4], gfx::Color c, double start,
          double end, float power)
{
    color4(color, c);
    fog[0] = static_cast<float>(start);
    fog[1] = end > start ? static_cast<float>(1.0 / (end - start)) : 0.0f;
    fog[2] = std::max(power, 0.0f);
}

void ramp4(float (&dst)[4], double min, double max, const gfx::Image& ramp)
{
    dst[0] = static_cast<float>(min);
    dst[1] = max > min ? static_cast<float>(1.0 / (max - min)) : 0.0f;
    dst[2] = static_cast<float>(ramp.width());
}

float unitCode(Unit unit)
{
    return unit == Unit::Mm ? 1.0f : unit == Unit::World ? 2.0f : 0.0f;
}

} // namespace

void setModel(MeshUniforms& u, const float (&rows)[12])
{
    std::copy(std::begin(rows), std::end(rows), std::begin(u.model));
    // Kolumnerna a, b, c i 3 × 3-delen; invers-transponatets KOLUMNER är
    // (b×c, c×a, a×b) / det, så rad i är (b×c[i], c×a[i], a×b[i]) / det.
    const double a[3] = {rows[0], rows[4], rows[8]};
    const double b[3] = {rows[1], rows[5], rows[9]};
    const double c[3] = {rows[2], rows[6], rows[10]};
    const auto cross = [](const double (&p)[3], const double (&q)[3], int i) {
        const int j = (i + 1) % 3, k = (i + 2) % 3;
        return p[j] * q[k] - p[k] * q[j];
    };
    double bc[3], ca[3], ab[3];
    for (int i = 0; i < 3; ++i) {
        bc[i] = cross(b, c, i);
        ca[i] = cross(c, a, i);
        ab[i] = cross(a, b, i);
    }
    const double det = a[0] * bc[0] + a[1] * bc[1] + a[2] * bc[2];
    const double k   = det != 0.0 ? 1.0 / det : 1.0;
    for (int i = 0; i < 3; ++i) {
        u.normalRows[i * 4 + 0] = static_cast<float>(bc[i] * k);
        u.normalRows[i * 4 + 1] = static_cast<float>(ca[i] * k);
        u.normalRows[i * 4 + 2] = static_cast<float>(ab[i] * k);
        u.normalRows[i * 4 + 3] = 0.0f;
    }
    u.surface[2] = det < 0.0 ? -1.0f : 1.0f;
}

void setColor(MeshUniforms& u, gfx::Color c)
{
    color4(u.color, c);
}

void setColor(PointUniforms& u, gfx::Color c)
{
    color4(u.color, c);
}

void setLight(MeshUniforms& u, const Vec3& direction, float ambient,
              float diffuse, float exponent, bool twoSided)
{
    unit3(u.toLight, -direction.x, -direction.y, -direction.z);
    u.toLight[3] = 1.0f;
    u.light[0]   = ambient;
    u.light[1]   = diffuse;
    u.light[2]   = exponent;
    u.rimDir[3]  = twoSided ? 1.0f : 0.0f;
}

void setLit(MeshUniforms& u, bool lit)
{
    u.toLight[3] = lit ? 1.0f : 0.0f;
}

void setRim(MeshUniforms& u, const Vec3& direction, float strength)
{
    unit3(u.rimDir, direction.x, direction.y, direction.z);
    u.light[3] = strength;
}

void setFog(MeshUniforms& u, gfx::Color color, double start, double end,
            float power)
{
    fog4(u.fogColor, u.fog, color, start, end, power);
}

void setFog(PointUniforms& u, gfx::Color color, double start, double end,
            float power)
{
    fog4(u.fogColor, u.fog, color, start, end, power);
}

void setScreenSize(MeshUniforms& u, float size, Unit unit)
{
    u.size[0] = size;
    u.size[1] = unitCode(unit);
}

void setPointSize(PointUniforms& u, float size, Unit unit,
                  gfx::MarkerShape shape)
{
    u.marker[0] = size;
    u.marker[1] = unitCode(unit);
    u.marker[2] = shape == gfx::MarkerShape::Circle ? 1.0f : 0.0f;
}

void setRamp(MeshUniforms& u, double min, double max, const gfx::Image& ramp)
{
    ramp4(u.ramp, min, max, ramp);
}

void setRamp(PointUniforms& u, double min, double max, const gfx::Image& ramp)
{
    ramp4(u.ramp, min, max, ramp);
}

void setTexture(MeshUniforms& u, const gfx::Image& texture)
{
    u.textures[0] = texture.premultiplied() ? 1.0f : 0.0f;
    u.textures[1] = texture.flippedV() ? 1.0f : 0.0f;
}

void setDrape(MeshUniforms& u, const gfx::Image& drape, double minX,
              double minY, double maxX, double maxY, bool northUp)
{
    // v = (drape.y − y) · drape.w: från maxY nedåt med 1/höjd, eller — rad 0
    // i söder — från minY uppåt med −1/höjd.
    const double h = maxY > minY ? 1.0 / (maxY - minY) : 0.0;
    u.textures[2] = drape.premultiplied() ? 1.0f : 0.0f;
    u.textures[3] = drape.flippedV() ? 1.0f : 0.0f;
    u.drape[0]    = static_cast<float>(minX);
    u.drape[1]    = static_cast<float>(northUp ? maxY : minY);
    u.drape[2]    = maxX > minX ? static_cast<float>(1.0 / (maxX - minX)) : 0.0f;
    u.drape[3]    = static_cast<float>(northUp ? h : -h);
}

} // namespace glaze::detail
