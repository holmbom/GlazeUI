#pragma once
// EGNA PROGRAM — appens egna
// shaders bredvid ramverkets inbyggda. KÄLLKOD PER BACKEND: HLSL (D3D11),
// GLSL 330 (GL) och MSL (Metal) — inget eget shaderspråk och ingen
// transpilering. `Renderer::createProgram` kompilerar i körtid och svarar
// ALLTID med ett objekt; `valid()` avgör och `error()` bär kompilatorns text
// med radnummer i appens EGEN källa. Programmet ritas med
// `MeshProgram::Custom` (gfx/scene.h), och strömmarnas form följer med varje
// rit-anrop (`VertexLayout`) — ett program kan läsa olika strömuppsättningar.
//
// TRE KONTRAKT bor här, lika i alla tre språken:
//
// 1. FÖRSPELET. Backenden lägger sin egen bit FÖRE appens källa — blocket
//    GlazeFrame (`FrameUniforms`, gfx/camera.h: std140, 240 byte) med namnet
//    `glaze`, och `glaze_toClip(c)`, som gör kamerans PIXELKLIPP (x/w och y/w
//    i målets fysiska px) till backendens egen klipprymd — och sist
//    `#line 1`, så att ett fel på appens rad 7 rapporteras på rad 7. I GLSL
//    står `#version 330 core` och definitionerna först; i HLSL och MSL går
//    definitionerna genom kompilatorn (D3D_SHADER_MACRO, preprocessorMacros).
//      HLSL  cbuffer GlazeFrame : register(b3) { GlazeFrameData glaze; };
//            float4 glaze_toClip(float4 c)
//      GLSL  layout(std140) uniform GlazeFrame { … } glaze;
//            vec4 glaze_toClip(vec4 c)
//      MSL   struct GlazeFrame { … }; — funktionen tar emot blocket själv:
//            constant GlazeFrame& glaze [[buffer(2)]]
//            float4 glaze_toClip(float4 c, constant GlazeFrame& glaze)
//    Medlemmarna heter som fälten i `FrameUniforms`: glaze.viewProj,
//    glaze.invViewProj, glaze.eye, glaze.forward, glaze.right, glaze.up,
//    glaze.target, glaze.scale och glaze.pixelSize. Matriserna läses som
//    kolumnvektorer: `mul(glaze.viewProj, v)` i HLSL, `glaze.viewProj * v` i
//    GLSL och MSL. `target` skriver BACKENDEN (det bundna målets storlek).
//    Förspelet bär också FÄRGJUSTERINGEN — samma funktion som
//    Image-programmet och ritytans Mesh-program kör, i varje språk EN gång:
//      HLSL/MSL  float3 glaze_colorAdjust(float3 c, float3 a)
//      GLSL      vec3 glaze_colorAdjust(vec3 c, vec3 a)
//    `c` är en RAK färg 0–1 och a = (mättnad, kontrast, ljushet); ljusheten
//    multiplicerar, kontrasten sträcker kring mittgrått 0,5 och mättnaden
//    blandar mot luminansen (Rec. 709 på de gammakodade talen), i den
//    ordningen och klampad till 0–1 efter varje steg — (1, 1, 1) lämnar
//    färgen EXAKT orörd (gfx/scene.h `SceneUniforms::saturation`).
//    Och de SKÄRMVÄNDA HÖRNENS DJUP — samma funktion som Line-,
//    Glyph- och ritytans Points-program kör, så ett eget program kan stå på
//    marken som ramverkets gör:
//      HLSL      float glaze_standingZ(float4x4 m, float4 c, float2 off)
//      GLSL      float glaze_standingZ(mat4 m, vec4 c, vec2 off)
//      MSL       float glaze_standingZ(float4x4 m, float4 c, float2 off)
//    `c` är ankarets pixelklipp (`m` gånger punkten, oftast
//    `glaze.viewProj`), `off` hörnets förskjutning i målets px (y nedåt), och
//    svaret är hörnets z att lägga i klippet bredvid `c.xy + off * c.w` och
//    `c.w`: djupet av planet genom ankaret parallellt med xy-planet där det
//    ligger närmare än ankaret (gfx/scene.h `MeshProgram`). Prefixet `glaze_`
//    är förspelets — appens egna namn får aldrig börja så.
//
// 2. PLATSERNA. Strömmarnas attribut adresseras med PLATS, aldrig med namn —
//    `VertexAttrib::location`, och konventionen i `attrib::` nedan:
//      HLSL  semantiken ATTR<N>         float3 pos : ATTR0;
//      GLSL  layout(location = N) in    layout(location = 0) in vec3 aPos;
//      MSL   [[attribute(N)]]           float3 pos [[attribute(0)]];
//    Varje plats programmet läser MÅSTE finnas i anropets layout, med ett
//    format av rätt slag (flyttal eller heltal) — annars ritas anropet inte,
//    och det loggas en gång per program och layout i alla backends.
//
// 3. BLOCKEN, TEXTURERNA OCH STRÖMMARNA (båda stegen ser allt):
//      GlazeFrame  D3D11 b3 · GL bindningspunkt 3 (på NAMN) · Metal buffer(2)
//      GlazeUser   D3D11 b4 · GL bindningspunkt 4 (på NAMN) · Metal buffer(3)
//                  — appens eget std140-block, exakt `ProgramDesc::userBytes`
//                  stort: HLSL `cbuffer GlazeUser : register(b4)`, GLSL
//                  `layout(std140) uniform GlazeUser`, MSL en egen struct
//      texturerna  D3D11 t0–t3/s0–s3 · GL `uniform sampler2D GlazeTexture0`–`3`
//                  (enhet 0–3, på NAMN) · Metal texture(0–3)/sampler(0–3)
//      strömmarna  vertexströmmen och instansströmmen: D3D11 slot 0 och 1,
//                  Metal buffer(0) och buffer(1)
//    b0–b2 är UI:ts och scenens i D3D11. Texturerna samplas med bildens EGET
//    filter, och SOM DE ÄR: ett render-måls bild är premultiplicerad i alla
//    backends och på GL dessutom speglad (v = 1 är överkanten,
//    `Image::flippedV`) — programmet tar hand om det själv.
//
// FRAMSIDAN som fragmentsteget läser den — SV_IsFrontFace, gl_FrontFacing,
// [[front_facing]] — är gallringens (gfx/scene.h `Cull`): triangeln som är
// MOTURS SETT FRÅN KAMERAN, i alla tre backends och i varje renderläge, också
// med Cull::None och utan djup. (Tecknet på en kryssprodukt av ddx/ddy beror
// däremot på skärmens y-riktning — nedåt i D3D11 och Metal, uppåt i GL.)
//
// FALLGROPEN I GL: målen är y-vända, så ett program som vill ha fragmentets
// läge i målets px ska ta det ur en varying (pixelklippet delat med w) och
// ALDRIG ur `gl_FragCoord.y`.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gfx/camera.h"   // FrameUniforms — GlazeFrame-blockets form

namespace gfx {

// Ett attributs format i strömmen och hur programmet läser det:
//   Float, Float2, Float3, Float4  32-bitars flyttal — float…float4 (HLSL,
//                                  MSL), float…vec4 (GLSL)
//   UNorm8x4  fyra byte normerade till 0–1 (färgen) — float4 / vec4
//   SNorm8x4  fyra byte normerade till −1–1 (normalen) — float4 / vec4
//   UInt8x4   fyra byte som HELTAL — uint4 / uvec4 (GL: glVertexAttribIPointer)
//   UInt      ett 32-bitars heltal — uint
enum class AttribFormat : std::uint8_t { Float, Float2, Float3, Float4,
                                         UNorm8x4, SNorm8x4, UInt8x4, UInt };

// Formatets storlek i byte — alla är hela fyrbytes.
inline constexpr std::uint16_t attribBytes(AttribFormat f)
{
    switch (f) {
    case AttribFormat::Float2: return 8;
    case AttribFormat::Float3: return 12;
    case AttribFormat::Float4: return 16;
    default:                   return 4;
    }
}

// Läses formatet som HELTAL? Programmet deklarerar då uint/uvec — ett
// flyttalsformat mot ett heltalsattribut (eller tvärtom) passar inte.
inline constexpr bool attribInteger(AttribFormat f)
{
    return f == AttribFormat::UInt8x4 || f == AttribFormat::UInt;
}

struct VertexAttrib {
    std::uint8_t  location = 0;                    // platsen (attrib::k…)
    AttribFormat  format   = AttribFormat::Float;
    std::uint16_t offset   = 0;   // byte in i elementet — hela fyrbytes
    std::uint8_t  stream   = 0;   // 0 = vertexströmmen (MeshDraw::vertices),
                                  // 1 = instansströmmen (MeshDraw::instances)
};

// Ett elements största steg (D3D11:s tak för en ström).
inline constexpr std::uint16_t kMaxVertexStride = 2048;

// STRÖMMARNAS FORM — per rit-anrop och inte per program. Reglerna,
// prövade i `drawMesh` i alla backends: platserna unika och
// under `attrib::kCount`, strömmen 0 eller 1, offset och steg i hela
// fyrbytes, och varje attribut inom sitt elements steg (högst
// `kMaxVertexStride`). En ström utan attribut läses inte, och dess steg
// gäller inte. En TOM layout är helskärmspassets: programmet tar sina
// vertex ur SV_VertexID/gl_VertexID/[[vertex_id]].
struct VertexLayout {
    std::vector<VertexAttrib> attribs;
    std::uint16_t             stride[2]{};   // byte per element, ström 0 och 1

    // Nyckeln backendarna cachar sin form efter (D3D11:s input layout,
    // Metals PSO, GL:s kontroll mot programmets indata): FNV-1a över stegen
    // och attributen I ORDNING — två lika layouter får samma nyckel, och två
    // olika kolliderar med sannolikheten 2⁻⁶⁴. Samma attribut i en annan
    // ordning är en annan nyckel (en post till i cachen, aldrig ett fel).
    std::uint64_t key() const;
};

// PLATSKONVENTIONEN — det ramverkets inbyggda Mesh- och Points-program
// läser, och det ett eget program gör klokt i att följa för att kunna ta
// meshens strömmar.
namespace attrib {
inline constexpr std::uint8_t kPosition  = 0;
inline constexpr std::uint8_t kNormal    = 1;
inline constexpr std::uint8_t kColor     = 2;
inline constexpr std::uint8_t kUv        = 3;
inline constexpr std::uint8_t kScalar    = 4;
inline constexpr std::uint8_t kUser0     = 5;   // user0–user3: platserna 5–8
inline constexpr std::uint8_t kInstance0 = 9;   // instansens: platserna 9–14
inline constexpr std::uint8_t kCount     = 16;  // platserna 0–15 — GL 3.3 lovar 16
} // namespace attrib

// Texturplatserna ett program kan sampla (0 … kMaxProgramTextures − 1).
inline constexpr int kMaxProgramTextures = 4;

// GlazeUser-blockets tak: Metal sätter det med setVertexBytes, som tar högst
// 4 KB.
inline constexpr std::uint32_t kMaxProgramUserBytes = 4096;

// Ett program. Bara källan för den backend som kör behöver finnas — men saknas
// DEN blir programmet ogiltigt med ett läsbart fel ("programmet 'himmel'
// saknar MSL-källa (macOS)"), loggat en gång, aldrig tyst.
struct ProgramDesc {
    std::string name;                       // i felen och loggen
    std::string hlsl;                       // D3D11 — båda funktionerna
    std::string glslVertex, glslFragment;   // GL — två källor med var sin main()
    std::string msl;                        // Metal — båda funktionerna
    // Funktionerna i HLSL och MSL (GLSL:s heter alltid main).
    std::string vertexEntry   = "vsMain";
    std::string fragmentEntry = "fsMain";
    // `#define namn värde` före förspelet — varianter ur en och samma källa.
    // Namnet är en identifierare (bokstäver, siffror, _, inte en siffra
    // först) och värdet en rad; tomt värde = definierat utan värde.
    std::vector<std::pair<std::string, std::string>> defines;
    // GlazeUser-blockets storlek i byte: 0 = inget block, annars en multipel
    // av 16 (std140, float4-packat) och högst kMaxProgramUserBytes. Källans
    // block ska vara EXAKT så stort — D3D11 och GL prövar det.
    std::uint32_t userBytes = 0;
    // Texturerna programmet samplar: platserna 0 … textures − 1, högst
    // kMaxProgramTextures. Varje rit-anrop ger dem alla och inga fler.
    std::uint8_t textures = 0;
};

// Ett kompilerat program — skapat av sin Renderer och, som fonter, bilder och
// buffertar, bundet till den: släpp programmet INNAN fönstret rivs.
class ShaderProgram {
public:
    virtual ~ShaderProgram() = default;

    virtual bool               valid() const = 0;
    // Felet för ett ogiltigt program — kompilatorns text med radnummer i
    // appens källa, en saknad källa eller en ogiltig beskrivning. Tomt för
    // ett giltigt program.
    virtual const std::string& error() const = 0;

    // Det programmet FÖRKLARADE i sin beskrivning — `drawMesh` prövar varje
    // anrop mot dem, likadant i alla backends.
    const std::string& name() const { return name_; }
    std::uint32_t      userBytes() const { return userBytes_; }
    int                textures() const { return textures_; }

    // Det vertexsteget LÄSER, reflekterat ur den kompilerade källan (D3D11:s
    // indatasignatur, GL:s aktiva attribut, Metals vertexattribut): bit N =
    // platsen N, och i `integerInputs` de platser det läser som heltal.
    // `drawMesh` kräver att anropets layout har varje sådan plats med ett
    // format av samma slag (`attribInteger`) — samma regel i alla backends.
    // 0 för ett ogiltigt program.
    std::uint32_t inputs() const { return inputs_; }
    std::uint32_t integerInputs() const { return integerInputs_; }

protected:
    explicit ShaderProgram(const ProgramDesc& d)
        : name_(d.name), userBytes_(d.userBytes), textures_(d.textures)
    {
    }

    // Backenden lägger in reflektionen när programmet kompilerats.
    void setInputs(std::uint32_t inputs, std::uint32_t integerInputs)
    {
        inputs_        = inputs;
        integerInputs_ = integerInputs & inputs;
    }

private:
    std::string   name_;
    std::uint32_t userBytes_     = 0;
    int           textures_      = 0;
    std::uint32_t inputs_        = 0;
    std::uint32_t integerInputs_ = 0;
};

inline std::uint64_t VertexLayout::key() const
{
    std::uint64_t h   = 14695981039346656037ull;   // FNV-1a, 64 bitar
    auto          mix = [&h](std::uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    };
    mix(stride[0], 2);
    mix(stride[1], 2);
    for (const VertexAttrib& a : attribs)
        mix(static_cast<std::uint64_t>(a.location) |
                static_cast<std::uint64_t>(a.format) << 8 |
                static_cast<std::uint64_t>(a.offset) << 16 |
                static_cast<std::uint64_t>(a.stream) << 32,
            5);
    return h;
}

} // namespace gfx
