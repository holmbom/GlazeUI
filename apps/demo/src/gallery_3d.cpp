// GALLERIETS 3D — fliken "3D". EN rityta med en
// `Camera3D` kring en liten kraftledning i SWEREF 99 TM (meter, z uppåt), och
// var och en av delarna visar ett av ritytans verktyg:
//
//   rutnät     LINJER I RYMDEN (`line3`) var tionde meter — rutnätet är
//              APPENS, byggt av ritytans linjeverktyg (ramverket har med flit
//              inget rutnät)
//   stolpar    PRIMITIVER: en cylinder (stolpen), en låda (traversen) och
//              små cylindrar (isolatorerna) — delade enhetsmeshar, så alla
//              stolpar är några få meshar på GPU:n
//   linor      tre faser i två spann som RÖR (`tube`) längs en parabel —
//              diametern är data per form (förstorad här, så den syns på
//              avstånd)
//   byggnader  en nätstation ur en EGEN MESH (`glaze::Mesh` ur lägen,
//              normaler och färger) som en `Placement` vrider
//   träd       en kon i två DETALJNIVÅER som INSTANSER — en form, ett fåtal
//              rit-anrop, och ingen uppladdning när kameran rör sig
//   punktmoln  en laserskannad kulle med träd: skalären är HÖJDEN ÖVER MARK
//              och en FÄRGRAMP färgar den —
//              marken marinblå oavsett hur högt kullen når, kronorna cyan
//              till röda. Ett KLICK plockar närmaste punkt inom 8 px bland
//              punkterna inom 10 m (`hitTest` med
//              `HitOptions`) och skriver dess plats och höjd i statusraden
//   etiketter  text i rymden (`text3`), översatt och alltid överst — lagret
//              står på MARKEN (Ground), så etiketterna ritas efter 3D:n
//   himmel     ett HELSKÄRMSPASS ur ett EGET PROGRAM (`Program` +
//              `screenPass`) i HLSL, GLSL och MSL — himlen är APPENS, byggd
//              med verktyget (ramverket skapar ingen skydome — appen ska
//              kunna skapa den); strålen genom varje pixel kommer ur
//              GlazeFrame:s bas
//   terräng    rutor av egna meshar i ett TERRAIN-lager, med ett hål — och
//              vägar i ett Ground-lager DRAPERADE på dem, en stående etikett
//              vid markhöjd och en LIGGANDE (`Alignment::Ground`) längs
//              stigen; kullen under laserskanningen är också terräng
//   förstaperson
//              en växel: ögat går på
//              marken med `Camera3D::lookFrom` — W/A/S/D (och pilarna) i
//              blickens riktning, Q/E vrider, ett drag tittar — genom
//              `onKeyDown`, som TAR tangenterna, och `onTick`, som läser
//              `isKeyHeld` och följer marken med `heightAt`. Ritytan tickar
//              bara så länge en tagen tangent hålls (canvas.h, regel 1)
//
// Koordinaterna är världens: appen räknar aldrig ett origo — ritytan ritar
// varje form relativt sitt eget i double (RTC). Gesterna är påslagna; med en
// `Camera3D` betyder de orbiten kring målet (canvas.h). MARKFÖLJNINGEN,
// HÖJDEN ÖVER MARK och PLOCKREGELN är modulens kod och bor här.
//
// FÄRGERNA är scenens DATA och följer inte temat (shape.h) — de är valda så
// att de syns mot både den ljusa och den mörka ytan bakom, som följer temat.
// Rutnätet är halvgenomskinligt grått av samma skäl.
//
// REGEL 8: varje form här går att skriva i `.glazeui` — utom stationens och
// konens meshar, terrängens rutor och punktmolnet, som räknas fram i drift
// (Bitmaps prejudikat; höjden över mark är molnets skalär),
// loopen som bygger rutnätet och himlens PROGRAM (koddata: previewn kör
// aldrig ett program och ritar passets reservfärg i stället — horisontens).
// Förstapersonen och plockningen är handlers, som previewn aldrig kör;
// växeln och lagren de skriver i har sin vanliga form.

#include "widget_gallery.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <vector>

namespace {

constexpr double kPi = std::numbers::pi;

// Scenens mitt i SWEREF 99 TM (meter) — samma plats som kartan.
constexpr double kMittX = 674000.0;
constexpr double kMittY = 6580000.0;

glaze::Point3 Q(double dx, double dy, double z)
{
    return glaze::Point3{kMittX + dx, kMittY + dy, z};
}

// Lagren, nedifrån och upp.
constexpr int kRutnat    = 0;
constexpr int kStolpar   = 1;
constexpr int kLinor     = 2;
constexpr int kByggnader = 3;
constexpr int kTrad      = 4;
constexpr int kPunkter   = 5;
constexpr int kEtiketter = 6;
constexpr int kTerrang   = 7;
constexpr int kVagar     = 8;
constexpr int kMarkering = 9;   // den plockade punkten — på marken, överst

// ÖVERSIKTEN: orbitens mål, avstånd, vinklar och synfält — också dit
// förstapersonen lämnar tillbaka kameran (kring där ögat stod).
constexpr double kOversiktAvst = 96.0;
constexpr float  kOversiktAz   = 20.0f;
constexpr float  kOversiktEl   = 21.0f;
constexpr float  kOversiktFov  = 42.0f;

// FÖRSTAPERSONEN — ögat minst 2 m över marken, vridningen 67,5°/s, musen
// 0,1°/px och sidled i halva farten; farten är en promenad i den lilla scenen.
constexpr double kOgonhojd = 2.0;     // m över marken
constexpr double kGang     = 5.0;     // m/s
constexpr double kVridFart = 67.5;    // °/s, Q och E
constexpr double kTitt     = 0.1;     // ° per logisk px under draget
constexpr float  kTittMax  = 85.0f;   // blicken upp och ned
constexpr float  kFovGang  = 45.0f;   // synfält

// PUNKTPLOCKNINGEN: närmast på skärmen inom 8 px, bland
// punkterna inom 10 m längs synlinjen.
constexpr float  kPlockPx = 8.0f;
constexpr double kPlockM  = 10.0;

// LEDNINGEN: tre stolpar öster–väster, traversen tvärs ledningen och
// fasernas lägen på den, linans nedhäng mitt i spannet och dess diameter.
constexpr double kStolpeX[]  = {-45.0, 0.0, 45.0};
constexpr double kStolpeH    = 14.0;
constexpr double kStolpeR    = 0.2;
constexpr double kFas[]      = {-1.8, 0.0, 1.8};
constexpr double kIsolatorH  = 0.4;
constexpr double kNedhang    = 1.6;
constexpr double kLinaD      = 0.14;
constexpr int    kLinaPunkter = 25;

// RUTNÄTET: linjer var tionde meter ut till ±80 m.
constexpr int    kRutor  = 8;
constexpr double kRutSteg = 10.0;

// SKOGEN i nordväst och KULLEN (punktmolnet) i nordost: mitt, halva sidan
// och kullens höjd — och skanningens träd: antalet, höjderna och punkterna
// per krona.
constexpr double kSkogX = -55.0, kSkogY = 58.0, kSkogHalv = 28.0;
constexpr int    kSkogTrad = 280;
constexpr double kKulleX = 66.0, kKulleY = 56.0, kKulleHalv = 24.0;
constexpr double kKulleH = 6.0;
constexpr int    kSkanTrad  = 24;
constexpr double kSkanMinH  = 7.0, kSkanMaxH = 20.0;
constexpr int    kKronPunkt = 260;

// TERRÄNGEN i sydost: 3 × 3 rutor om 12 m med 9 × 9 hörn, en låg kulle
// (höjden och dess mitt), och hålet — 2 × 2 celler i den mellersta rutan i
// östra kanten (en damm). Vägarna och etiketterna ligger i den.
constexpr double kTerrangX0 = 30.0, kTerrangY0 = -58.0, kRuta = 12.0;
constexpr int    kRutor3d   = 3, kRutHorn = 9;
constexpr double kTerrangH  = 2.6;
constexpr double kTerrangMittX = 47.0, kTerrangMittY = -41.0;

// Ett deterministiskt brus 0–1 (demon ska se likadan ut varje gång).
double brus(std::uint32_t i, std::uint32_t frö)
{
    std::uint32_t h = i * 2654435761u ^ (frö * 2246822519u + 0x9e3779b9u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return static_cast<double>(h) / 4294967295.0;
}

// Ett plant hörn med normalen — nätstationens tak och väggar byggs av dem.
void kvadrat(glaze::MeshDesc& d, glaze::Float3 a, glaze::Float3 b,
             glaze::Float3 c, glaze::Float3 e, glaze::Float3 n, gfx::Color färg)
{
    const auto bas = static_cast<std::uint32_t>(d.positions.size());
    d.positions.insert(d.positions.end(), {a, b, c, e});
    d.normals.insert(d.normals.end(), 4, n);
    d.colors.insert(d.colors.end(), 4, färg);
    d.indices.insert(d.indices.end(), {bas, bas + 1, bas + 2, bas, bas + 2, bas + 3});
}

// NÄTSTATIONEN — en egen mesh i lokala meter kring golvets mitt: fyra
// väggar och ett sadeltak med gavlar, färg och normal per sida, moturs sett
// utifrån.
glaze::Mesh natstation()
{
    constexpr float     bx = 1.6f, by = 1.2f, h = 2.4f, nock = 3.3f;
    const gfx::Color    vagg{226, 214, 190, 255}, tak{150, 62, 50, 255};
    glaze::MeshDesc     d;
    kvadrat(d, {-bx, -by, 0}, {bx, -by, 0}, {bx, -by, h}, {-bx, -by, h}, {0, -1, 0}, vagg);
    kvadrat(d, {bx, by, 0}, {-bx, by, 0}, {-bx, by, h}, {bx, by, h}, {0, 1, 0}, vagg);
    kvadrat(d, {bx, -by, 0}, {bx, by, 0}, {bx, by, h}, {bx, -by, h}, {1, 0, 0}, vagg);
    kvadrat(d, {-bx, by, 0}, {-bx, -by, 0}, {-bx, -by, h}, {-bx, by, h}, {-1, 0, 0}, vagg);
    // Taket: två sluttningar längs x, och gavlarnas trianglar.
    const float lut = std::sqrt((nock - h) * (nock - h) + by * by);
    kvadrat(d, {-bx, -by, h}, {bx, -by, h}, {bx, 0, nock}, {-bx, 0, nock},
            {0, -(nock - h) / lut, by / lut}, tak);
    kvadrat(d, {bx, by, h}, {-bx, by, h}, {-bx, 0, nock}, {bx, 0, nock},
            {0, (nock - h) / lut, by / lut}, tak);
    for (const float sida : {1.0f, -1.0f}) {
        const auto bas = static_cast<std::uint32_t>(d.positions.size());
        d.positions.insert(d.positions.end(), {{sida * bx, -sida * by, h},
                                               {sida * bx, sida * by, h},
                                               {sida * bx, 0, nock}});
        d.normals.insert(d.normals.end(), 3, glaze::Float3{sida, 0, 0});
        d.colors.insert(d.colors.end(), 3, vagg);
        d.indices.insert(d.indices.end(), {bas, bas + 1, bas + 2});
    }
    return glaze::Mesh(std::move(d));
}

// TRÄDET — en kon i två detaljnivåer ur EN delad vertexmängd: 24 sidor upp
// till 120 m, sedan 6.
glaze::Mesh tradkon()
{
    glaze::MeshDesc d;
    for (const int sidor : {24, 6}) {
        const auto bas   = static_cast<std::uint32_t>(d.positions.size());
        const auto forst = static_cast<std::uint32_t>(d.indices.size());
        d.positions.push_back({0, 0, 7.5f});
        d.normals.push_back({0, 0, 1});
        for (int i = 0; i < sidor; ++i) {
            const double v = 2.0 * kPi * i / sidor;
            d.positions.push_back({static_cast<float>(2.2 * std::cos(v)),
                                   static_cast<float>(2.2 * std::sin(v)), 1.2f});
            d.normals.push_back({static_cast<float>(std::cos(v)),
                                 static_cast<float>(std::sin(v)), 0.35f});
        }
        for (int i = 0; i < sidor; ++i)
            d.indices.insert(d.indices.end(),
                             {bas + 1 + static_cast<std::uint32_t>(i),
                              bas + 1 + static_cast<std::uint32_t>((i + 1) % sidor),
                              bas});
        d.lods.push_back(glaze::Lod{forst,
                                    static_cast<std::uint32_t>(d.indices.size()) - forst,
                                    sidor == 24 ? 120.0 : 0.0});
    }
    return glaze::Mesh(std::move(d));
}

// Terrängens höjd i (dx, dy) relativt scenens mitt — en låg kulle och en
// grund kant mot väster.
double terrangHojd(double dx, double dy)
{
    const double rx = dx - kTerrangMittX, ry = dy - kTerrangMittY;
    return 0.3 + kTerrangH * std::exp(-(rx * rx + ry * ry) / 160.0) +
           0.25 * std::sin(dx * 0.35) * std::cos(dy * 0.3);
}

// EN TERRÄNGRUTA (ti, tj) — 9 × 9 hörn i världen (origot räknas fram), utan
// normaler: Mesh-programmet tar den platta normalen ur derivatorna, så ljuset
// visar kullen. Den mellersta rutan i östra kanten har ett HÅL.
glaze::Mesh terrangRuta(int ti, int tj)
{
    std::vector<glaze::Point3> lagen;
    for (int j = 0; j < kRutHorn; ++j)
        for (int i = 0; i < kRutHorn; ++i) {
            const double dx = kTerrangX0 + kRuta * (ti + i / double(kRutHorn - 1));
            const double dy = kTerrangY0 + kRuta * (tj + j / double(kRutHorn - 1));
            lagen.push_back(Q(dx, dy, terrangHojd(dx, dy)));
        }
    std::vector<std::uint32_t> index;
    const bool hal = ti == kRutor3d - 1 && tj == 1;
    for (int j = 0; j + 1 < kRutHorn; ++j)
        for (int i = 0; i + 1 < kRutHorn; ++i) {
            if (hal && (i == 3 || i == 4) && (j == 3 || j == 4))
                continue;   // dammen
            const auto a = static_cast<std::uint32_t>(j * kRutHorn + i);
            index.insert(index.end(),
                         {a, a + 1, a + kRutHorn + 1, a, a + kRutHorn + 1, a + kRutHorn});
        }
    return glaze::Mesh::fromWorld(lagen, std::move(index));
}

// KULLENS MARK i (x, y) relativt kullens mitt — en mjuk kulle. Den är både
// TERRÄNGEN under laserskanningen (förstapersonen går på den) och marken som
// höjden över mark räknas från — i en riktig app terrängmodellen (DEM).
double kulleMark(double x, double y)
{
    return kKulleH * std::exp(-(x * x + y * y) / (0.45 * kKulleHalv * kKulleHalv));
}

// KULLENS TERRÄNG — 25 × 25 hörn över skanningens ruta, som terrängrutorna:
// i världen och utan normaler.
glaze::Mesh kulleTerrang()
{
    constexpr int              n = 25;
    std::vector<glaze::Point3> lagen;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = kKulleHalv * (2.0 * i / (n - 1) - 1.0);
            const double y = kKulleHalv * (2.0 * j / (n - 1) - 1.0);
            lagen.push_back(Q(kKulleX + x, kKulleY + y, kulleMark(x, y)));
        }
    std::vector<std::uint32_t> index;
    for (int j = 0; j + 1 < n; ++j)
        for (int i = 0; i + 1 < n; ++i) {
            const auto a = static_cast<std::uint32_t>(j * n + i);
            index.insert(index.end(), {a, a + 1, a + n + 1, a, a + n + 1, a + n});
        }
    return glaze::Mesh::fromWorld(lagen, std::move(index));
}

// LASERSKANNINGEN — lägena relativt kullens fot och HÖJDEN ÖVER MARK per
// punkt: marken (ett skakat rutnät på kullen, en bit ovanför terrängen, som
// är rak mellan hörnen där kullen är rund) och träden på den (kronor av
// punkter). Höjden över mark är punktens höjd minus markens rakt under,
// aldrig under noll.
struct Skanning {
    std::vector<glaze::Float3> lagen;
    std::vector<float>         hag;
};

Skanning laserskanning()
{
    Skanning   s;
    const auto punkt = [&s](double x, double y, double z) {
        s.lagen.push_back({static_cast<float>(x), static_cast<float>(y),
                           static_cast<float>(z)});
        s.hag.push_back(static_cast<float>(std::max(0.0, z - kulleMark(x, y))));
    };
    constexpr int kSida = 110;
    for (int j = 0; j < kSida; ++j)
        for (int i = 0; i < kSida; ++i) {
            const auto   n = static_cast<std::uint32_t>(j * kSida + i);
            const double x = kKulleHalv * (2.0 * (i + brus(n, 9)) / kSida - 1.0);
            const double y = kKulleHalv * (2.0 * (j + brus(n, 10)) / kSida - 1.0);
            punkt(x, y, kulleMark(x, y) + 0.12 + 0.1 * brus(n, 11));
        }
    // Ett träd är en kon av punkter från kronans fot (en tredjedel upp) till
    // toppen, glesast vid toppen.
    for (std::uint32_t t = 0; t < static_cast<std::uint32_t>(kSkanTrad); ++t) {
        const double tx  = 0.8 * kKulleHalv * (2.0 * brus(t, 20) - 1.0);
        const double ty  = 0.8 * kKulleHalv * (2.0 * brus(t, 21) - 1.0);
        const double h   = kSkanMinH + (kSkanMaxH - kSkanMinH) * brus(t, 22);
        const double fot = kulleMark(tx, ty);
        for (std::uint32_t p = 0; p < static_cast<std::uint32_t>(kKronPunkt); ++p) {
            const std::uint32_t n = t * kKronPunkt + p;
            const double        u = brus(n, 23);   // 0 = kronans fot, 1 = toppen
            const double r = 0.22 * h * (1.0 - u) * std::sqrt(brus(n, 24));
            const double v = 2.0 * kPi * brus(n, 25);
            punkt(tx + r * std::cos(v), ty + r * std::sin(v), fot + h * (0.35 + 0.65 * u));
        }
    }
    return s;
}

// HÖJDRAMPEN (CloudCompare-lik) över 0–40 m: marinblå mark,
// blått, cyan, gult vid 14 m, rött vid 20 m och därefter mörkrött, magenta,
// violett och vitt — det som sticker upp över skogen får egna färger.
glaze::ColorRamp hojdRamp()
{
    return {{{0.000f, gfx::Color{0, 0, 128, 255}},
             {0.075f, gfx::Color{0, 0, 255, 255}},
             {0.200f, gfx::Color{0, 255, 255, 255}},
             {0.350f, gfx::Color{255, 255, 0, 255}},
             {0.500f, gfx::Color{255, 0, 0, 255}},
             {0.625f, gfx::Color{128, 0, 0, 255}},
             {0.750f, gfx::Color{255, 0, 255, 255}},
             {0.875f, gfx::Color{153, 102, 255, 255}},
             {1.000f, gfx::Color{255, 255, 255, 255}}},
            0.0,
            40.0};
}

// HIMLEN — ett EGET program: helskärmstriangeln och, per
// pixel, strålen ur GlazeFrame:s bas (forward + x · right + y · up) tonad
// från horisonten mot zenit — och under horisonten mot markens dis. Färgerna
// är GlazeUser: horisonten, zenit och diset. Appens kod i tre språk: HLSL
// (D3D11), GLSL (GL) och MSL (Metal).
glaze::Program himmelProgram()
{
    glaze::Program p;
    p.name = "himmel";
    p.hlsl = R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uHorisont; float4 uZenit; float4 uDis; };
struct VsOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VsOut vsMain(uint id : SV_VertexID)
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float4 fsMain(VsOut i) : SV_Target
{
    float3 dir = normalize(glaze.forward.xyz + i.ndc.x * glaze.right.xyz +
                           i.ndc.y * glaze.up.xyz);
    float3 c   = dir.z >= 0.0
                     ? lerp(uHorisont.rgb, uZenit.rgb, pow(saturate(dir.z), 0.55))
                     : lerp(uHorisont.rgb, uDis.rgb, saturate(-dir.z * 5.0));
    return float4(c, glaze.scale.z);
}
)hlsl";
    p.glslVertex = R"glsl(
out vec2 vNdc;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc        = p * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
)glsl";
    p.glslFragment = R"glsl(
layout(std140) uniform GlazeUser { vec4 uHorisont; vec4 uZenit; vec4 uDis; };
in vec2 vNdc;
out vec4 frag;
void main()
{
    vec3 dir = normalize(glaze.forward.xyz + vNdc.x * glaze.right.xyz +
                         vNdc.y * glaze.up.xyz);
    vec3 c   = dir.z >= 0.0
                   ? mix(uHorisont.rgb, uZenit.rgb, pow(clamp(dir.z, 0.0, 1.0), 0.55))
                   : mix(uHorisont.rgb, uDis.rgb, clamp(-dir.z * 5.0, 0.0, 1.0));
    frag = vec4(c, glaze.scale.z);
}
)glsl";
    p.msl = R"msl(
struct User  { float4 horisont; float4 zenit; float4 dis; };
struct VsOut { float4 pos [[position]]; float2 ndc; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.ndc = p * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant User& user [[buffer(3)]])
{
    float3 dir = normalize(glaze.forward.xyz + i.ndc.x * glaze.right.xyz +
                           i.ndc.y * glaze.up.xyz);
    float3 c   = dir.z >= 0.0
                     ? mix(user.horisont.rgb, user.zenit.rgb, pow(saturate(dir.z), 0.55))
                     : mix(user.horisont.rgb, user.dis.rgb, saturate(-dir.z * 5.0));
    return float4(c, glaze.scale.z);
}
)msl";
    p.uniformFloats = 12;
    return p;
}

// Himlens färger — scenens data, som allt annat här (temat styr ytan bakom,
// som himlen täcker).
const gfx::Color kHorisont{214, 226, 238, 255};
const gfx::Color kZenit{92, 140, 206, 255};
const gfx::Color kDis{176, 186, 180, 255};

std::vector<float> farg4(gfx::Color c)
{
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f};
}

} // namespace

void WidgetGallery::tredeFlik(glaze::TabPage* sida)
{
    // === 27 · Canvas — scen i 3D =========================================
    auto* g = grupp(sida, demo::str::G27Scen, "g27", 12, 12, 1252, 484);

    auto* k      = g->add<glaze::Canvas>();
    k->x         = 8;
    k->anchorTop = {"g27Sep", 10};
    k->anchors   = {true, true, true, false};
    k->right     = 8;
    k->height    = 340;
    k->name      = "scen3d";
    scen3d_      = k;
    auto* fotRad = fot(g, demo::str::Fot3d, "scen3d", 8);

    // FÖRSTAPERSONENS VÄXEL och vad ett klick och tangenterna gör — under
    // ytan och inte på den: på himlen hade en ljus text försvunnit i mörkt
    // läge.
    auto* fp      = g->add<glaze::Checkbox>(glaze::tr(demo::str::Forstaperson));
    fp->x         = 8;
    fp->anchorTop = {fotRad->name, 6};
    fp->name      = "scen3dGa";
    forstaperson_ = fp;
    fp->onChange  = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
        forstapersonAndrad(s, e);
    };
    auto* hjalp       = g->add<glaze::Label>(glaze::tr(demo::str::Fot3dKlick));
    hjalp->anchorLeft = {"scen3dGa", 16};
    hjalp->anchorTop  = {fotRad->name, 6};
    hjalp->setRole(glaze::Label::Role::Dim);
    hjalp->name = "scen3dHjalp";

    // INMATNINGEN: läget ritytans bindningar läses i och tre
    // vändningar. "mäta" är DEMONS eget läge och bär bara sina avvikelser —
    // vänsterdraget gör ingenting (klicket plockar fortfarande punkter), och
    // allt annat faller tillbaka på det automatiska.
    k->setBinding("mäta", glaze::InputSource::LeftDragX, 0, glaze::InputAction::None);
    k->setBinding("mäta", glaze::InputSource::LeftDragY, 0, glaze::InputAction::None);
    const auto inmatning = [this](glaze::Widget* s, glaze::ChangeArgs& e) {
        inmatningAndrad(s, e);
    };
    auto* lage      = g->add<glaze::Dropdown>();
    lage->x         = 8;
    lage->anchorTop = {"scen3dGa", 6};
    lage->width     = 220;
    lage->name      = "scen3dLage";
    lage->setItems({trs(demo::str::InmLageAuto), trs(demo::str::InmLageKarta),
                    trs(demo::str::InmLageOrbit), trs(demo::str::InmLageForsta),
                    trs(demo::str::InmLageMata)});
    lage->setSelectedIndex(0);
    lage->onChange = inmatning;
    inmLage_       = lage;
    const std::pair<std::uint32_t, glaze::Checkbox**> vandningar[] = {
        {demo::str::InmVandHjul, &inmHjul_},
        {demo::str::InmVandPlatta, &inmPlatta_},
        {demo::str::InmVandZoom, &inmZoom_}};
    std::string forra = "scen3dLage";   // raden växer åt höger, granne för granne
    for (const auto& [text, ut] : vandningar) {
        auto* v       = g->add<glaze::Checkbox>(glaze::tr(text));
        v->anchorLeft = {forra, 16};
        v->anchorTop  = {"scen3dGa", 10};
        v->name       = "scen3dVand" + std::to_string(text);
        v->onChange   = inmatning;
        *ut           = v;
        forra         = v->name;
    }

    // KAMERAN: orbit kring ledningens mitt, snett från sydväst. Med en
    // Camera3D betyder gesterna orbiten — dra vrider och lutar, mittendrag
    // panorerar, hjulet går närmare.
    k->setCamera(glaze::Camera3D{.target    = Q(2, 16, 5),
                                 .distance  = kOversiktAvst,
                                 .azimuth   = kOversiktAz,
                                 .elevation = kOversiktEl,
                                 .fov       = kOversiktFov});
    k->setPannable(true);
    k->setZoomable(true);
    k->setRotatable(true);
    k->setPitchable(true);
    // LJUSET: snett ovanifrån, en mjuk exponent och ett svagt rimljus mot
    // norr.
    k->setLighting({.direction    = {-0.35, -0.45, -0.82},
                    .ambient      = 0.45f,
                    .diffuse      = 0.65f,
                    .exponent     = 1.5f,
                    .rimDirection = {0, 1, 0},
                    .rim          = 0.12f});

    k->setLayers({glaze::tr(demo::str::Lager3dRutnat),
                  glaze::tr(demo::str::Lager3dStolpar),
                  glaze::tr(demo::str::Lager3dLinor),
                  glaze::tr(demo::str::Lager3dByggnader),
                  glaze::tr(demo::str::Lager3dTrad),
                  glaze::tr(demo::str::Lager3dPunkter),
                  glaze::tr(demo::str::LagerEtiketter),
                  glaze::tr(demo::str::Lager3dTerrang),
                  glaze::tr(demo::str::Lager3dVagar),
                  glaze::tr(demo::str::Lager3dMarkering)});
    // Etiketterna och den plockade punkten på MARKEN: de ritas efter 3D:n och
    // skyms inte av den.
    k->setLayerPlacement(kEtiketter, glaze::LayerPlacement::Ground);
    k->setLayerPlacement(kMarkering, glaze::LayerPlacement::Ground);

    // INTERAKTIONEN ÄR APPENS: klicket plockar en punkt, och förstapersonen
    // tar musen och tangenterna (handlers nedan).
    k->onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
        scenNed(s, e);
    };
    k->onMouseMove = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
        scenRor(s, e);
    };
    k->onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
        scenSlapp(s, e);
    };
    k->onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) { scenTangent(s, e); };
    k->onTick    = [this](glaze::Widget* s, glaze::TickArgs& e) { scenTick(s, e); };

    // HIMLEN — appens eget program som helskärmspass först i bildrutan (det
    // ligger i rutnätets lager; passet ändrar inte lagrets placering).
    // Reservfärgen är horisontens: den syns där programmet inte kan köras.
    k->addProgram(himmelProgram());
    {
        std::vector<float> farger = farg4(kHorisont);
        for (const gfx::Color c : {kZenit, kDis}) {
            const std::vector<float> f = farg4(c);
            farger.insert(farger.end(), f.begin(), f.end());
        }
        k->addShape(kRutnat, glaze::Shape::screenPass(
                                 "himmel", {.uniforms = std::move(farger),
                                            .fallback = kHorisont}));
    }

    // RUTNÄTET — appens, av linjer i rymden.
    const glaze::Stroke linje{.paint = gfx::Color{128, 128, 128, 110},
                              .width = glaze::px(1.0f)};
    const double        kant = kRutor * kRutSteg;
    for (int i = -kRutor; i <= kRutor; ++i) {
        k->addShape(kRutnat, glaze::Shape::line3(Q(i * kRutSteg, -kant, 0),
                                                 Q(i * kRutSteg, kant, 0), linje));
        k->addShape(kRutnat, glaze::Shape::line3(Q(-kant, i * kRutSteg, 0),
                                                 Q(kant, i * kRutSteg, 0), linje));
    }

    // STOLPARNA — primitiver: stolpen, traversen och isolatorerna. Ljusa
    // toner, så de syns mot både den ljusa och den mörka ytan.
    const glaze::MeshStyle tra{.color = gfx::Color{180, 142, 100, 255}};
    const glaze::MeshStyle stal{.color = gfx::Color{198, 200, 206, 255}};
    const glaze::MeshStyle glas{.color = gfx::Color{120, 172, 224, 255}};
    for (const double x : kStolpeX) {
        k->addShape(kStolpar, glaze::Shape::cylinder(Q(x, 0, 0), kStolpeR,
                                                     kStolpeH, tra));
        k->addShape(kStolpar, glaze::Shape::box({Q(x - 0.1, -2.3, kStolpeH - 0.55),
                                                 Q(x + 0.1, 2.3, kStolpeH - 0.3)},
                                                stal));
        for (const double fas : kFas)
            k->addShape(kStolpar, glaze::Shape::cylinder(Q(x, fas, kStolpeH - 0.3),
                                                         0.06, kIsolatorH, glas, 12));
    }

    // LINORNA — rör längs en parabel mellan isolatorernas toppar.
    const glaze::MeshStyle lina{.color = gfx::Color{172, 174, 184, 255}};
    const double           fäste = kStolpeH - 0.3 + kIsolatorH;
    for (std::size_t s = 0; s + 1 < std::size(kStolpeX); ++s)
        for (const double fas : kFas) {
            std::vector<glaze::Point3> väg;
            for (int i = 0; i < kLinaPunkter; ++i) {
                const double t = i / double(kLinaPunkter - 1);
                väg.push_back(Q(kStolpeX[s] + (kStolpeX[s + 1] - kStolpeX[s]) * t, fas,
                                fäste - 4.0 * kNedhang * t * (1.0 - t)));
            }
            k->addShape(kLinor, glaze::Shape::tube(std::move(väg), kLinaD, lina));
        }

    // NÄTSTATIONEN — en egen mesh, vriden 25° och förstorad med placeringen.
    k->addShape(kByggnader,
                glaze::Shape::mesh(natstation(), {.at    = Q(20, -20, 0),
                                                  .yaw   = 25,
                                                  .scale = {1.6f, 1.6f, 1.6f}}));

    // SKOGEN — konen som instanser i nordväst, i vart och ett eget läge,
    // gir, storlek och grönska.
    std::vector<glaze::Instance> skog;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(kSkogTrad); ++i) {
        const double s = 0.75 + 0.55 * brus(i, 3);
        skog.push_back(
            {.at    = Q(kSkogX + kSkogHalv * (2.0 * brus(i, 1) - 1.0),
                        kSkogY + kSkogHalv * (2.0 * brus(i, 2) - 1.0), 0),
             .yaw   = static_cast<float>(360.0 * brus(i, 4)),
             .scale = {static_cast<float>(s), static_cast<float>(s),
                       static_cast<float>(s * (0.9 + 0.3 * brus(i, 5)))},
             .color = gfx::Color{static_cast<std::uint8_t>(40 + 40 * brus(i, 6)),
                                 static_cast<std::uint8_t>(110 + 60 * brus(i, 7)),
                                 static_cast<std::uint8_t>(45 + 20 * brus(i, 8)),
                                 255}});
    }
    k->addShape(kTrad, glaze::Shape::instances(glaze::Shape::mesh(tradkon()),
                                               std::move(skog)));

    // KULLEN — en laserskanning i nordost, med träd. Skalären är HÖJDEN ÖVER
    // MARK, och höjdrampen färgar den: marken är marinblå också på
    // kullens topp, och träden får färg efter sin egen höjd. Statusraden
    // läser höjden igen vid ett klick (`punktHag_`).
    Skanning skan = laserskanning();
    punktHag_     = skan.hag;
    k->addShape(kPunkter,
                glaze::Shape::pointCloud(
                    glaze::PointCloud(std::move(skan.lagen), Q(kKulleX, kKulleY, 0), {},
                                      std::move(skan.hag)),
                    {.size  = glaze::px(3.0f),
                     .color = gfx::Color{255, 255, 255, 255},
                     .ramp  = hojdRamp()}));

    // ETIKETTERNA i rymden — översatta, med gloria så de läses mot allt.
    const glaze::TextStyle text{.size  = glaze::mm(2.6f),
                                .color = gfx::Color{30, 30, 34, 255},
                                .halo  = gfx::Color{255, 255, 255, 255},
                                .anchor = glaze::TextAnchor::Bottom};
    k->addShape(kEtiketter, glaze::Shape::text3(Q(0, 0, kStolpeH + 1.0),
                                                glaze::tr(demo::str::Etikett3dStolpe),
                                                text));
    k->addShape(kEtiketter, glaze::Shape::text3(Q(20, -20, 6.5),
                                                glaze::tr(demo::str::Etikett3dStation),
                                                text));
    k->addShape(kEtiketter, glaze::Shape::text3(Q(kSkogX, kSkogY, 11.0),
                                                glaze::tr(demo::str::Etikett3dSkog),
                                                text));
    k->addShape(kEtiketter,
                glaze::Shape::text3(Q(kKulleX, kKulleY, kKulleH + kSkanMaxH + 1.5),
                                    glaze::tr(demo::str::Etikett3dSkanning), text));

    // TERRÄNGEN — rutorna ÄR marken (Terrain), och Ground-lagret med vägarna
    // DRAPERAS på dem: vägarna följer kullen, och dammen (hålet) visar det
    // som ligger bakom.
    k->setLayerPlacement(kTerrang, glaze::LayerPlacement::Terrain);
    const glaze::MeshStyle mark{.color = gfx::Color{104, 146, 78, 255}};
    for (int tj = 0; tj < kRutor3d; ++tj)
        for (int ti = 0; ti < kRutor3d; ++ti)
            k->addShape(kTerrang, glaze::Shape::mesh(terrangRuta(ti, tj), {}, mark));
    // Kullen under laserskanningen är också mark — förstapersonen går på den.
    k->addShape(kTerrang, glaze::Shape::mesh(kulleTerrang(), {}, mark));
    // Skogsvägen i en båge över kullen, och en streckad stig tvärs över.
    std::vector<glaze::Point> vag;
    for (int i = 0; i <= 24; ++i) {
        const double t = i / 24.0;
        vag.push_back({kMittX + kTerrangX0 + 1.0 + 34.0 * t,
                       kMittY + kTerrangY0 + 9.0 + 16.0 * t + 6.0 * std::sin(t * kPi)});
    }
    k->addShape(kVagar, glaze::Shape::polyline(
                            std::move(vag), {.paint = gfx::Color{176, 152, 118, 255},
                                             .width = glaze::mm(1.4f),
                                             .cap   = glaze::LineCap::Round}));
    const glaze::Point stigA{kMittX + 36.0, kMittY - 26.0};
    const glaze::Point stigB{kMittX + 60.0, kMittY - 56.0};
    k->addShape(kVagar, glaze::Shape::line(stigA, stigB,
                                           {.paint    = gfx::Color{236, 226, 204, 255},
                                            .width    = glaze::mm(0.7f),
                                            .dash     = {2.0f, 1.2f},
                                            .dashUnit = glaze::Unit::Mm}));
    // En STÅENDE etikett vid markhöjd (ankaret står på kullen), och en
    // LIGGANDE längs stigen — draperad, som en vägmarkering.
    k->addShape(kVagar, glaze::Shape::text({kMittX + 52.0, kMittY - 37.0},
                                           glaze::tr(demo::str::Etikett3dVag), text));
    const double stigVinkel =
        std::atan2(stigB.y - stigA.y, stigB.x - stigA.x) * 180.0 / kPi;
    k->addShape(kVagar, glaze::Shape::text(
                            {kMittX + 44.0, kMittY - 37.5},
                            glaze::tr(demo::str::Etikett3dGrusvag),
                            {.size     = glaze::mm(2.2f),
                             .color    = gfx::Color{70, 58, 44, 255},
                             .angle    = static_cast<float>(stigVinkel),
                             .rotation = glaze::Alignment::Ground}));
}

// ---------------------------------------------------------------- handlers

void WidgetGallery::scenNed(glaze::Widget*, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Left)
        return;
    scenNed_ = scenTitt_ = e.local;
    // I förstapersonen är draget BLICKEN: trycket tas, så ingen orbit startar.
    e.handled = forstaperson_->isChecked();
}

void WidgetGallery::scenRor(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    if (e.button != gfx::MouseButton::Left || !forstaperson_->isChecked())
        return;
    // MUSBLICKEN: åt höger vrider blicken medurs, nedåt sänker
    // den — ögat står kvar.
    auto*                 k   = static_cast<glaze::Canvas*>(sender);
    const glaze::Camera3D c   = k->camera3D();
    const double          dx  = e.local.x - scenTitt_.x, dy = e.local.y - scenTitt_.y;
    const float           yaw = c.azimuth + static_cast<float>(dx * kTitt);
    const float pitch = std::clamp(-c.elevation - static_cast<float>(dy * kTitt), -kTittMax,
                                   kTittMax);
    scenTitt_ = e.local;
    k->setCamera(glaze::Camera3D::lookFrom(c.eye(), yaw, pitch, c.fov));
    e.handled = true;
}

void WidgetGallery::scenSlapp(glaze::Widget* sender, glaze::CanvasMouseArgs& e)
{
    // Ett KLICK och inte ett drag (kartans regel): släppet inom stilens
    // dragtröskel från trycket — ett drag har redan orbitat eller tittat.
    const double dx = e.local.x - scenNed_.x, dy = e.local.y - scenNed_.y;
    const double grans = style().dragThreshold;
    if (e.button != gfx::MouseButton::Left || dx * dx + dy * dy > grans * grans)
        return;
    // PUNKTPLOCKNINGEN: den närmaste träffen i punktmolnet — ritytan svarar
    // med punktens plats i INDATAN, och höjden över mark slås upp där.
    auto* k = static_cast<glaze::Canvas*>(sender);
    for (const glaze::HitResult& h :
         k->hitTest(e.local, glaze::HitOptions{.pointTolerance = glaze::px(kPlockPx),
                                               .maxDistance    = kPlockM})) {
        if (h.layer != kPunkter || h.kind != glaze::HitKind::Point || !h.point ||
            h.element < 0 || static_cast<std::size_t>(h.element) >= punktHag_.size())
            continue;
        const glaze::MarkerStyle ring{.size         = glaze::mm(2.4f),
                                      .fill         = gfx::Color{255, 255, 255, 255},
                                      .outline      = gfx::Color{0, 0, 0, 255},
                                      .outlineWidth = glaze::px(1.5f)};
        k->setShapes(kMarkering, {glaze::Shape::marker3(*h.point, ring)});
        statusTxt_->setText(trs(demo::str::PunktPrefix) + std::to_string(h.element) +
                            trs(demo::str::PunktHag) +
                            tal(punktHag_[static_cast<std::size_t>(h.element)], 1) + " m");
        return;
    }
    k->setShapes(kMarkering, {});
    statusTxt_->setText(glaze::tr(demo::str::PunktIngen));
}

void WidgetGallery::scenTangent(glaze::Widget*, glaze::KeyArgs& e)
{
    if (!forstaperson_->isChecked())
        return;
    switch (e.key) {
    case gfx::Key::W:
    case gfx::Key::A:
    case gfx::Key::S:
    case gfx::Key::D:
    case gfx::Key::Q:
    case gfx::Key::E:
    case gfx::Key::Up:
    case gfx::Key::Down:
    case gfx::Key::Left:
    case gfx::Key::Right:
        e.handled = true;   // tagen ⇒ ritytan tickar medan den hålls (scenTick)
        break;
    default:
        break;
    }
}

void WidgetGallery::scenTick(glaze::Widget* sender, glaze::TickArgs& e)
{
    if (!forstaperson_->isChecked())
        return;
    // De HÅLLNA tangenterna (pilarna som W/A/S/D) — fram, i
    // sidled och vridningen.
    const auto hall = [this](gfx::Key t) { return isKeyHeld(t) ? 1.0 : 0.0; };
    const double fram = std::max(hall(gfx::Key::W), hall(gfx::Key::Up)) -
                        std::max(hall(gfx::Key::S), hall(gfx::Key::Down));
    const double sida = std::max(hall(gfx::Key::D), hall(gfx::Key::Right)) -
                        std::max(hall(gfx::Key::A), hall(gfx::Key::Left));
    const double vrid = hall(gfx::Key::E) - hall(gfx::Key::Q);
    auto*                 k = static_cast<glaze::Canvas*>(sender);
    const glaze::Camera3D c = k->camera3D();
    const float yaw = c.azimuth + static_cast<float>(vrid * kVridFart * e.seconds);
    // Framåt längs blickens kompassriktning (0 = norr, medurs), i sidled med
    // halva farten.
    const double  az  = yaw * kPi / 180.0;
    const double  v   = kGang * e.seconds;
    glaze::Point3 oga = c.eye();
    oga.x += (std::sin(az) * fram + 0.5 * std::cos(az) * sida) * v;
    oga.y += (std::cos(az) * fram - 0.5 * std::sin(az) * sida) * v;
    // MARKFÖLJNINGEN: terrängens höjd under ögat — utanför terrängen (och i
    // dammen) är marken planet z = 0.
    oga.z = k->heightAt({oga.x, oga.y}).value_or(0.0) + kOgonhojd;
    k->setCamera(glaze::Camera3D::lookFrom(oga, yaw, -c.elevation, c.fov));
}

// INMATNINGEN — läget och vändningarna på RITYTAN: appens
// globala tabell rörs inte (den ärvs av allt annat). "Vänd zoomen" vänder
// handlingen för alla tre enheterna, alltså hjulet, nypningen och + och −.
void WidgetGallery::inmatningAndrad(glaze::Widget*, glaze::ChangeArgs&)
{
    // Valets rader i ordning: automatiskt, ramverkets tre och demons eget.
    static const char* const kLagen[] = {"", "map", "orbit", "firstPerson", "mäta"};
    glaze::Canvas*           k        = scen3d_;
    const int                i        = inmLage_->selectedIndex();
    k->setInputMode(i >= 0 && i < static_cast<int>(std::size(kLagen)) ? kLagen[i] : "");
    const bool hjul = inmHjul_->isChecked(), platta = inmPlatta_->isChecked();
    k->setSourceInvert(glaze::InputSource::WheelX, hjul);
    k->setSourceInvert(glaze::InputSource::WheelY, hjul);
    k->setSourceInvert(glaze::InputSource::ScrollX, platta);
    k->setSourceInvert(glaze::InputSource::ScrollY, platta);
    for (const glaze::InputDevice d : {glaze::InputDevice::Mouse,
                                       glaze::InputDevice::Touchpad,
                                       glaze::InputDevice::Keyboard})
        k->setActionInvert(d, glaze::InputAction::Zoom, inmZoom_->isChecked());
    k->focus();   // hjulet och tangenterna till ritytan, inte till valet
}

void WidgetGallery::forstapersonAndrad(glaze::Widget*, glaze::ChangeArgs&)
{
    glaze::Canvas*        k   = scen3d_;
    const bool            gar = forstaperson_->isChecked();
    const glaze::Camera3D c   = k->camera3D();
    if (gar) {
        // Ögat där orbitens mål står, på marken, och blicken åt samma håll.
        const double z = k->heightAt({c.target.x, c.target.y}).value_or(0.0);
        k->setCamera(glaze::Camera3D::lookFrom({c.target.x, c.target.y, z + kOgonhojd},
                                               c.azimuth, 0.0f, kFovGang));
    } else {
        // Översikten igen, kring där ögat stod.
        const glaze::Point3 oga = c.eye();
        const double        z   = k->heightAt({oga.x, oga.y}).value_or(0.0);
        k->setCamera(glaze::Camera3D{.target    = {oga.x, oga.y, z},
                                     .distance  = kOversiktAvst,
                                     .azimuth   = c.azimuth,
                                     .elevation = kOversiktEl,
                                     .fov       = kOversiktFov});
    }
    // GESTERNA är orbitens — i förstapersonen är draget blicken (scenRor).
    k->setPannable(!gar);
    k->setZoomable(!gar);
    k->setRotatable(!gar);
    k->setPitchable(!gar);
    k->focus();   // tangenterna till ritytan, inte till växeln
}
