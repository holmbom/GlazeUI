#pragma once
#include <cstddef>
#include <cstdint>

#include <gfx/color.h>
#include <gfx/program.h>
#include <gfx/sdf.h>   // MarkerShape

#include "glaze/canvas/geometry.h"   // Float3, Vec3, Unit

namespace gfx {
class Image;
}

// INTERN — RITYTANS INBYGGDA 3D-PROGRAM: MESH och
// POINTS, i HLSL, GLSL 330 och MSL (Metal), kompilerade genom fasadens
// EGEN väg — `Renderer::createProgram` och `MeshProgram::Custom`. Det är med
// flit: ramverkets tyngsta bruk bevisar appens väg (skälet mot nya
// `MeshProgram`-poster). Varianterna är DEFINITIONER ur en och samma källa
// (HAS_NORMAL, HAS_COLOR, HAS_UV, HAS_SCALAR, INSTANCED, SCREEN_SIZED, DRAPED
// för Mesh; HAS_COLOR och HAS_SCALAR för Points). Filen är en FABRIK: den ger
// programmets beskrivning, strömmarnas form, texturernas platser, packningen
// av ett element och GlazeUser-blocken med sina fyllare — ingen renderer,
// inget fönster och inget glaze-API. Ritytans `Lighting`/`MeshStyle`/
// `PointStyle` (style3d.h) fylls in i blocken av mesh-ritningen;
// kompileringen och cachen per variant är `CanvasResources`.
//
// MESH-PROGRAMMET — i den här ordningen, PER FRAGMENT:
//   basen    stilens färg × vertexfärgen (HAS_COLOR) × instansens färg
//            (INSTANCED; alfa 0 = ingen) × texturen (HAS_UV) × rampen
//            (HAS_SCALAR); draperingen (DRAPED) läggs ÖVER basen,
//            premultiplicerat — alla färger med RAK alfa
//   justering  FÄRGJUSTERINGEN — basens ljushet (× talet), kontrast
//            (kring mittgrått) och mättnad (mot LUMINANSEN, Rec. 709 på de
//            gammakodade talen: 0,2126 R + 0,7152 G + 0,0722 B), i den
//            ordningen och klampad efter varje steg: förspelets
//            `glaze_colorAdjust` (gfx/program.h), samma funktion som
//            Image-programmets. 0 i mättnaden ger grått, (1, 1, 1) orört
//   ljuset   belyst: färgen × (ambient + diffuse · max(N·L, 0)^exponent +
//            rim · max(N·R, 0)), klampad till 0–1. L är riktningen MOT ljuset
//            (`setLight` tar ljusets FÄRDRIKTNING och vänder den), R
//            rimvektorn — en yta vars normal pekar dit får rimljuset (på en karta: norr).
//            Obelyst: färgen orörd.
//   dimman   färgen mot dimfärgen med andelen ((d − start)/(end − start))^power
//            × dimfärgens alfa, klampad till 0–1; d är avståndet till ögat i
//            perspektiv och DJUPET från närplanet längs blicken ortografiskt
//            (ögat finns inte där — skriven gräns)
//   alfan    basens × opaciteten × GlazeFrame:s opacitet (`scale.z`)
// Programmet lämnar RAK alfa: lagrets `Blend::Alpha` (och `Opaque` för
// ogenomskinligt innehåll).
//
// NORMALEN. HAS_NORMAL läser den per vertex (SNorm8×4) genom normalmatrisen
// (`setModel`) och, med INSTANCED, instansens invers-transponat; en normal som
// interpolerats till noll ersätts av den platta. Utan HAS_NORMAL är normalen
// PLATT, ur derivatorna av fragmentets läge: kryssprodukten pekar mot ögat för
// varje synlig yta (tecknet beror på skärmens y-riktning — D3D11 och Metal
// nedåt, GL uppåt — och står per språk i källan), och den vänds efter
// vindningen, så den platta normalen är ytans FRAMSIDA. TVÅSIDIGT vänder
// normalen på BAKSIDAN — den triangel som är medurs SETT FRÅN KAMERAN
// (fasadens kontrakt, gfx/program.h, lika i tre backends också med
// Cull::None). En speglande transform (determinanten < 0) vänder vindningen;
// programmet räknar med det i både framsidan och normalen. Gallringen kan det
// inte rätta: en speglad mesh med Cull::Back gallrar sin utsida.
//
// TRANSFORMEN (RTC). Lägena är meshens LOKALA float. Utan
// instanser: chunk-lokalt = modell · lokalt, där modellen är placeringens
// rotation och skala (`setModel`, en affin 3 × 4) och anropets origo (i
// GlazeFrame:s matris) är `Mesh::origin + Placement::at`, i double. Med
// INSTANCED: chunk-lokalt = instans · (modell · lokalt), instansens
// förflyttning relativ anropets origo — cellens. SCREEN_SIZED skalar det
// lokala med storleken i px × världen per px på ankarets djup (GlazeFrame:s
// `pixelSize`: k · djup + c) — ankaret är det lokala origots plats, så
// meshens LOKALA ENHET blir storleken på skärmen (en sfär med diametern 1 och
// storleken 4 mm är 4 mm), och placeringens och instansens egen skala gäller
// ovanpå. Enheten: px × uiScale, mm × pxPerMm (GlazeFrame:s `scale`), world
// rakt av.
//
// PUNKTERNA — instansierade quads, en per punkt, med hörnen ur
// vertexindexet (`kPointQuadVertices` = 6 vertex och ingen vertexström;
// punkterna är INSTANSSTRÖMMEN, 12–20 byte: läget, färgen och skalären på
// platserna kPosition/kColor/kScalar). Quaden står upprätt i skärmen på
// punktens djup — djuptestet gäller punkten mot en mesh —, och formen är ett
// AVSTÅNDSFÄLT (kvadrat: max-normen, cirkel: radien) med en halv px
// kantutjämning och täckningen i alfan; ett fragment utan täckning KASTAS, så
// marginalen inte skriver djup. Storleken är diametern: px × uiScale, mm ×
// pxPerMm, world genom djupet (k · djup + c). En punkt bakom närplanet eller
// bortom det bortre ritas inte, i alla tre backends. Färgen: stilens ×
// punktens (HAS_COLOR) × rampen (HAS_SCALAR), dimman per punkt, inget ljus.
//
// RAMPEN är 2D-gradientens bild (`gfx::buildGradientRamp`,
// `CanvasResources::ramp`): texeln i bär t = i / (n − 1), så programmet
// samplar u = (½ + t · (n − 1)) / n med bildens EGET filter — rampbilden ska
// ha Linear (bildens förval), och då är rampen linjär mellan texlarna. t =
// (skalär − min) / (max − min), klampad; max ≤ min ger första stoppet.
//
// TEXTURERNA samplas som de är (fasadens kontrakt): ett render-måls bild är
// premultiplicerad och på GL speglad, och programmet tar hand om det med
// flaggorna i blocket (`setTexture`, `setDrape`). DRAPED (terrängen —
// drape.h): uv ur fragmentets chunk-lokala x och y mot en låda —
// u = (x − minX) / bredd, v = (maxY − y) / höjd, alltså bildens rad 0 vid
// lådans NORRA kant (draperingen ritad ovanifrån med y uppåt), eller vid den
// SÖDRA för en scen med y nedåt (`setDrape` vänder v) — och bilden läggs över
// basen FÖRE färgjusteringen och ljuset.

namespace glaze::detail {

// MESH-PROGRAMMETS VARIANT — en definition per fält.
struct MeshVariant {
    bool normals     = false;   // HAS_NORMAL — normalen per vertex
    bool colors      = false;   // HAS_COLOR — färgen per vertex
    bool uvs         = false;   // HAS_UV — texturkoordinaten och texturen
    bool scalars     = false;   // HAS_SCALAR — skalären genom rampen
    bool instanced   = false;   // INSTANCED — instansströmmen (MeshInstance)
    bool screenSized = false;   // SCREEN_SIZED — konstant skärmstorlek
    bool draped      = false;   // DRAPED — terrängens drapering

    // Bitarna i fältens ordning (bit 0 = normals) — cachens nyckel, 0–127.
    std::uint32_t      key() const;
    static MeshVariant fromKey(std::uint32_t key);
};
inline constexpr std::uint32_t kMeshVariantCount = 128;

// POINTS-PROGRAMMETS VARIANT.
struct PointVariant {
    bool colors  = false;   // HAS_COLOR — färgen per punkt
    bool scalars = false;   // HAS_SCALAR — skalären genom rampen

    std::uint32_t       key() const;   // 0–3
    static PointVariant fromKey(std::uint32_t key);
};
inline constexpr std::uint32_t kPointVariantCount = 4;

// TEXTURERNAS PLATSER för en variant: −1 = ingen. Ordningen är texturen,
// rampen, draperingen, så platserna är alltid 0 … count − 1 utan lucka —
// `count` är `ProgramDesc::textures`, och rit-anropet ger exakt de bilderna.
struct ProgramSlots {
    int texture = -1;
    int ramp    = -1;
    int drape   = -1;
    int count   = 0;
};

// Programmets beskrivning för varianten: källorna i tre språk, definitionerna
// (varianten och texturernas register/platser/samplernamn), GlazeUser-blockets
// storlek och texturantalet. Namnet bär varianten ("glaze_mesh.normal.color").
gfx::ProgramDesc  meshProgramDesc(const MeshVariant& v);
gfx::VertexLayout meshLayout(const MeshVariant& v);
ProgramSlots      meshSlots(const MeshVariant& v);
gfx::ProgramDesc  pointProgramDesc(const PointVariant& v);
gfx::VertexLayout pointLayout(const PointVariant& v);
ProgramSlots      pointSlots(const PointVariant& v);

// En punkt är två trianglar ur vertexindexet: rit-anropet har `vertexCount`
// 6 och `instanceCount` = punkterna, utan vertexström.
inline constexpr std::uint32_t kPointQuadVertices = 6;

// ------------------------------------------------------------ strömmarna
//
// MESHENS VERTEX — sammanflätad i ström 0, fälten i den här ordningen och
// bara de varianten har: läget Float3 (12), normalen SNorm8×4 (4, w = 0),
// färgen UNorm8×4 (4), uv Float2 (8) och skalären Float (4) — 12 till 32
// byte, `meshLayout(v).stride[0]`. `packMeshVertex` skriver ETT element, så
// varje packningsloop (segment, intervall, ommappning) går genom samma form.
struct MeshVertex {
    Float3     position;                   // lokalt, relativt meshens origo
    Float3     normal;                     // normeras vid packningen
    gfx::Color color{255, 255, 255, 255};  // rak alfa
    float      u = 0.0f, v = 0.0f;         // rad 0 överst (bildens form)
    float      scalar = 0.0f;
};
void packMeshVertex(const MeshVariant& v, const MeshVertex& in,
                    std::uint8_t* out);

// PUNKTEN — ett element i instansströmmen: läget Float3, färgen UNorm8×4
// (HAS_COLOR) och skalären Float (HAS_SCALAR), 12–20 byte.
struct PointVertex {
    Float3     position;                   // lokalt, relativt chunkens origo
    gfx::Color color{255, 255, 255, 255};  // rak alfa
    float      scalar = 0.0f;
};
void packPoint(const PointVariant& v, const PointVertex& in, std::uint8_t* out);

// INSTANSEN (INSTANCED) — 52 byte i ström 1: en affin 3 × 4 som tre RADER
// (x, y, z, förflyttning) på platserna kInstance0 … +2 och färgen på +3.
// Färgen MULTIPLICERAR basen; alfa 0 är ingen färg (glaze-instansens
// förval), så en vit mesh får instansens färg och en färgad tonas.
struct MeshInstance {
    float      rows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    gfx::Color color{0, 0, 0, 0};
};
static_assert(sizeof(MeshInstance) == 52, "MeshInstance är ett 52-bytes kontrakt");
static_assert(offsetof(MeshInstance, color) == 48,
              "instansens färg läses på offset 48 av tre backends");

// --------------------------------------------------------------- blocken
//
// GLAZEUSER — std140, float4-packat, KONTRAKT mot tre språk (fälten i den här
// ordningen, offseten kontrollerade nedan; källornas `MeshUser`/`PointUser`
// är samma lista). Förvalen är NEUTRALA: identitet, vit, obelyst, ingen
// dimma, ingen färgjustering och full opacitet — ritningen sätter resten med
// fyllarna.
struct MeshUniforms {
    float model[12]      = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // 3 rader (x, y, z, t)
    float normalRows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};   // 3 rader (x, y, z, 0)
    float color[4]    = {1, 1, 1, 1};   // stilens färg, rak alfa 0–1
    float light[4]    = {0, 0, 1, 0};   // ambient, diffuse, exponent, rim
    float toLight[4]  = {0, 0, 1, 0};   // xyz mot ljuset; w 1 belyst, 0 obelyst
    float rimDir[4]   = {0, 1, 0, 0};   // xyz rimvektorn; w 1 tvåsidigt
    float fogColor[4] = {0, 0, 0, 0};   // rgb 0–1; a dimmans styrka
    float fog[4]      = {0, 0, 1, 0};   // start, 1/(end − start), power, 0
    float surface[4]  = {1, 0, 1, 0};   // opacitet, 0, spegling ±1, 0
    float size[4]     = {0, 0, 0, 0};   // SCREEN_SIZED: storleken, enheten (Unit)
    float ramp[4]     = {0, 0, 1, 0};   // min, 1/(max − min), rampens texlar, 0
    // texturen premultiplicerad, speglad; draperingen premultiplicerad, speglad
    float textures[4] = {0, 0, 0, 0};
    float drape[4]    = {0, 0, 0, 0};   // minX, maxY, 1/bredd, 1/höjd — chunk-lokalt
    // FÄRGJUSTERINGEN: mättnad, kontrast, ljushet, 0 — förspelets
    // `glaze_colorAdjust` (gfx/program.h) med samma vektor som Image-programmets
    float adjust[4]   = {1, 1, 1, 0};
};
static_assert(sizeof(MeshUniforms) == 288, "MeshUniforms är ett 288-bytes kontrakt");
static_assert(offsetof(MeshUniforms, normalRows) == 48 &&
                  offsetof(MeshUniforms, color) == 96 &&
                  offsetof(MeshUniforms, light) == 112 &&
                  offsetof(MeshUniforms, toLight) == 128 &&
                  offsetof(MeshUniforms, rimDir) == 144 &&
                  offsetof(MeshUniforms, fogColor) == 160 &&
                  offsetof(MeshUniforms, fog) == 176 &&
                  offsetof(MeshUniforms, surface) == 192 &&
                  offsetof(MeshUniforms, size) == 208 &&
                  offsetof(MeshUniforms, ramp) == 224 &&
                  offsetof(MeshUniforms, textures) == 240 &&
                  offsetof(MeshUniforms, drape) == 256 &&
                  offsetof(MeshUniforms, adjust) == 272,
              "MeshUniforms-fälten läses på fasta offset av tre språk");

struct PointUniforms {
    float color[4]    = {1, 1, 1, 1};   // stilens färg, rak alfa 0–1
    float fogColor[4] = {0, 0, 0, 0};   // rgb 0–1; a dimmans styrka
    float fog[4]      = {0, 0, 1, 0};   // start, 1/(end − start), power, 0
    float marker[4]   = {1, 0, 0, 1};   // diametern, enheten (Unit), formen
                                        // (0 kvadrat, 1 cirkel), opacitet
    float ramp[4]     = {0, 0, 1, 0};   // min, 1/(max − min), rampens texlar, 0
};
static_assert(sizeof(PointUniforms) == 80, "PointUniforms är ett 80-bytes kontrakt");
static_assert(offsetof(PointUniforms, fogColor) == 16 &&
                  offsetof(PointUniforms, fog) == 32 &&
                  offsetof(PointUniforms, marker) == 48 &&
                  offsetof(PointUniforms, ramp) == 64,
              "PointUniforms-fälten läses på fasta offset av tre språk");

// FYLLARNA — de omräkningar som inte är en tilldelning. Ett fält som bara är
// ett tal (opaciteten, färgjusteringen) skrivs direkt.
//
// Modellen: tre rader (x, y, z, förflyttning). Normalmatrisen blir
// invers-transponatet (kofaktorerna / determinanten — exakt, också speglat)
// och speglingen determinantens tecken; en singulär modell får kofaktorerna
// som de är (normalen kan bli noll — då gäller den platta).
void setModel(MeshUniforms& u, const float (&rows)[12]);
void setColor(MeshUniforms& u, gfx::Color c);
void setColor(PointUniforms& u, gfx::Color c);
// Ljuset: `direction` är ljusets FÄRDRIKTNING (glaze-ljusets form — nedåt för
// en sol), L = −direction normerad; noll ger bara ambient. Slår på ljuset.
void setLight(MeshUniforms& u, const Vec3& direction, float ambient,
              float diffuse, float exponent, bool twoSided);
void setLit(MeshUniforms& u, bool lit);
// Rimljuset: `direction` är vektorn ytans normal jämförs med (normerad).
void setRim(MeshUniforms& u, const Vec3& direction, float strength);
// Dimman i världsenheter; end ≤ start ger ingen dimma, power < 0 räknas som 0.
void setFog(MeshUniforms& u, gfx::Color color, double start, double end,
            float power);
void setFog(PointUniforms& u, gfx::Color color, double start, double end,
            float power);
// SCREEN_SIZED: meshens lokala enhet blir `size` i enheten (world: rakt av).
void setScreenSize(MeshUniforms& u, float size, Unit unit);
// Punkten: diametern i enheten och formen; opaciteten är `marker[3]`.
void setPointSize(PointUniforms& u, float size, Unit unit,
                  gfx::MarkerShape shape);
// Rampen: skalärens intervall och rampbildens bredd (texlarna läses ur den).
void setRamp(MeshUniforms& u, double min, double max, const gfx::Image& ramp);
void setRamp(PointUniforms& u, double min, double max, const gfx::Image& ramp);
// Texturens premultiplicering och spegling (HAS_UV).
void setTexture(MeshUniforms& u, const gfx::Image& texture);
// DRAPED: draperingens bild och lådan i CHUNK-LOKALA koordinater (världen
// minus anropets origo, räknat i double av anroparen). `northUp`: bildens
// rad 0 ligger vid lådans norra kant (maxY) — annars vid den södra (minY),
// för en drapering ritad med y nedåt.
void setDrape(MeshUniforms& u, const gfx::Image& drape, double minX,
              double minY, double maxX, double maxY, bool northUp = true);

} // namespace glaze::detail
