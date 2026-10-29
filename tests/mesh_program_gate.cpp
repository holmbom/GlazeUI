// mesh_program_gate — de INBYGGDA
// Mesh- och Points-programmen (src/canvas/programs3d.cpp) ritade
// genom FASADEN direkt — `createProgram` med fabrikens beskrivning och
// `drawMesh` (MeshProgram::Custom) i offscreen-mål med djup, utan Canvas.
// Vägen är scene3d_pixel_gates: ett DOLT fönster, en testwidget som binder
// render-mål i sin ritning, och målen lästa med `readTarget` efter framen.
//
// FÖRVÄNTAT VÄRDE är grindens egen räkning, aldrig programmets: ljusmodellens
// DEFINITION (ambient + diffuse · max(N·L, 0)^exponent +
// rim · max(N·R, 0); dimman ((d − start)/(end − start))^power mot dimfärgen;
// mättnaden mot luminansen, Rec. 709) med normalerna ur grindens EGEN
// geometri; hålkameran (px = 64 + 128·x/y, py = 64 − 128·z/y) och den
// ortografiska vyn ovanifrån (px = 64 + 4x, py = 64 − 4y) för lägena; och
// ytorna ur geometrin (en kvadrat om 6 px är 36 px², en cirkel π·3²).
//
// VAD GRINDEN PÅSTÅR:
//   V1  ALLA varianter kompilerar i backendens språk — 128 Mesh och 4 Points —
//       och reflektionen läser EXAKT layoutens platser, som flyttal; blockets
//       storlek och texturantalet är fabrikens.
//   E1  en yta rakt mot ljuset = färg × (ambient + diffuse); exponenten (N·L =
//       0,6 med exponent 2); rimljuset mot och bort; normalen genom MODELLEN
//       (en roterad kvadrat) och genom modellens INVERS-TRANSPONAT (en lutad
//       yta sträckt olikformigt).
//   E2  baksidan: tvåsidigt belyses den som framsidan, ensidigt bara med
//       ambient — med normaler och med den platta, med djuptest (Less) och
//       UTAN (Off, som på D3D11 tar 2D-vägens rasteriserare).
//   E3  den platta normalen ur derivatorna: en lutad yta utan normaler under
//       två ljus ger YTANS normal (tecknet och riktningen); baksidan ensidigt
//       mörk, tvåsidigt ljus; Less och Off.
//   E4  texturen rättvänd (2 × 2, rad 0 överst); ett render-måls bild
//       (premultiplicerad, på GL speglad) likadant, och dess halvgenomskinliga
//       texel av-premultiplicerad.
//   E5  rampens tre stopp (min, mitt, max) med 2D-gradientens bild; skalären
//       interpolerad över ytan; en ramp om tre texlar LINJÄR mellan texlarna
//       (t = 0,2 ger 60 % / 40 %); textur och ramp i ett
//       anrop på `meshSlots` platser (texturen 0, rampen 1).
//   E6  mättnad 0 ger luminansens grå, 0,5 halvvägs;
//       vertexfärgen multiplicerar stilens; FÄRGJUSTERINGEN i
//       blockets `adjust` — kontrasten 0 ger mittgrått och ljusheten 0,5
//       halva färgen. Sabotage som SKA fälla dem:
//       mättnaden läst ur `surface.y` i stället för `adjust.x`,
//       kontrasten kring 0 i stället för mittgrått, och `adjust.y`/`adjust.z`
//       förväxlade i `glaze_colorAdjust` (förspelet, alla tre backends).
//   E7  dimman vid fogEnd ger dimfärgen; halvvägs med power 2 en fjärdedel.
//   E8  RTC: samma mesh (ljus, dimma, platt normal) vid origo och vid SWEREF
//       ger samma bild ±1 px.
//   E9  instanserna: tre i ett anrop på kända lägen med gir och olikformig
//       skala, i sina färger; delen från instans 1; en färg med alfa 0 är
//       basens; instansens normal (rotation och olikformig skala).
//   E10 SCREEN_SIZED: en sfär om 4 mm (injicerad kalibrering 4 px/mm) är 16 px
//       nära och fjärran; 10 px med uiScale 1,5 är 15 px (diametern ur ytan).
//   E11 DRAPED, den reserverade varianten: bildens rad 0 vid
//       lådans norra kant, ytans färg under det genomskinliga — också med ett
//       render-måls bild.
//   F1  en kvadratisk punkt om 6 px täcker 36 px² (±1,5); F2 en rund π·3².
//   F3  rampens min/mitt/max och en ramp om tre texlar linjärt mellan dem.
//   F4  djuptestet mot en mesh: punkten framför syns, den bakom inte.
//   F5  färgen per punkt.
//   F6  storleken i mm × pxPerMm (1,5 mm vid 4 px/mm = 36 px²).
//   F7  storleken i världen genom djupet (0,5 på djupet 10, brännvidden 128 px:
//       6,4 px).
//   F8  en punkt bakom kameran och en bortom det bortre planet
//       ritas inte; punkterna överlever Cull::Back (quaden är moturs).
//
// INTE MÄTT här: Metal (MSL:en provas på macOS — raden i
// doc/os/macos/TESTS.md); DRAPED i ritytan (draperingsmålen, belysningen av
// draperingen — E11 mäter bara uv-mappningen
// och kompositionen); ritytans väg (mesh-posterna, residensen, gallringen —
// scene3d_pixel_gate och canvas_perf3d); dimman ortografiskt och punkternas dimma; en
// speglande placering (determinanten < 0); slopeBias och blandningar utöver
// Alpha; MSAA (alla mål har ett prov); och skärmstorleken under ortografisk
// projektion.
//
// SABOTAGEN — alla fäller grinden, antalet fel i parentes (D3D11 / GL, samma på
// båda drivrutinerna):
// normalen inte vänd på baksidan (6 / 6 — E2 tvåsidigt, E3), dimman
// linjär när fogPower ≠ 1 (1 / 1 — E7), mättnaden mot medelvärdet i stället
// för luminansen (2 / 2 — E6), instansmatrisen transponerad (4 / 4 — E9;
// instans 0 utan gir och skala är symmetrisk och står kvar), punktstorleken
// utan pxPerMm (1 / 1 — F6) och SCREEN_SIZED utan djup (2 / 2 — E10 fjärran).
// D3D11:s 2D-rasteriserare med framsidan medurs, alltså
// rättelsen i d3d11_renderer.cpp borttagen (5 — E2/E3 utan djup), den platta
// normalens tecken vänt (12 / 12), normalen genom modellen själv i stället för
// invers-transponatet (1 — E1, olikformig skala; rotationen är densamma),
// rampen utan halva texeln (2 / 2 — E5 och F3 tre texlar), texturen utan
// av-premultiplicering (1 / 1 — E4), punktens quad medurs (1 / 1 — F8
// Cull::Back), GL:s spegling ignorerad (— / 1 — E4; D3D11:s mål är inte
// speglade), DRAPED:s v ur lådans södra kant (2 / 2 — E11) och lang_gate-
// blocket kring shaderkällorna borttaget (lang_gate: 24 fynd).
//
// INGEN TYST SKIP (som i clip_gate): utan headless-yta FALLER grinden.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/geom3d.h>
#include <gfx/mat4.h>
#include <glaze/glaze.h>

#include "canvas/programs3d.h"   // intern — programmen och blocken

namespace {

namespace dt = glaze::detail;

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;
bool g_ritfel  = false;   // en frame gick inte att läsa — ALDRIG en skip

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// Tal i en TESTFIL är tillåtna (regel 3 gäller widget-filer).
const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kSvart{0, 0, 0, 255};
const gfx::Color kRöd{255, 0, 0, 255};
const gfx::Color kGrön{0, 200, 0, 255};
const gfx::Color kBlå{0, 0, 255, 255};
const gfx::Color kYta{200, 100, 50, 255};   // ljusprovens färg

// --------------------------------------------------------------- bilderna

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    // Utanför bilden (eller en bild som inte gick att läsa) svarar den med en
    // pixel som ingen förväntan matchar — ett fel i MÄTNINGEN.
    const std::uint8_t* at(int x, int y) const
    {
        static const std::uint8_t ingen[4] = {1, 2, 3, 4};
        if (x < 0 || y < 0 || x >= w || y >= h ||
            px.size() < static_cast<std::size_t>(w) * h * 4)
            return ingen;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

bool ärFärg(const std::uint8_t* p, gfx::Color c, int tol = 3)
{
    return std::abs(p[0] - c.r) <= tol && std::abs(p[1] - c.g) <= tol &&
           std::abs(p[2] - c.b) <= tol;
}

std::string px(const std::uint8_t* p)
{
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]) + "," + std::to_string(p[3]);
}

std::string färg(gfx::Color c)
{
    return std::to_string(c.r) + "," + std::to_string(c.g) + "," +
           std::to_string(c.b);
}

std::string tal(double v)
{
    char s[32];
    std::snprintf(s, sizeof s, "%.2f", v);
    return s;
}

std::string kort(const std::string& s)
{
    std::string ut = s.substr(0, 200);
    for (char& c : ut)
        if (c == '\n' || c == '\r')
            c = ' ';
    return ut;
}

// ------------------------------------------------------ modellens förväntade värden
//
// Ljusmodellens, dimmans och mättnadens DEFINITION — räknad
// här i double på färgens 0–255, oberoende av programmets formler.

std::uint8_t kanal(double v)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
}

// Färgen × ljusets intensitet, klampad (modellen klampar den belysta färgen).
gfx::Color belyst(gfx::Color c, double intensitet)
{
    const double k = std::max(intensitet, 0.0);
    return {kanal(std::min(c.r * k, 255.0)), kanal(std::min(c.g * k, 255.0)),
            kanal(std::min(c.b * k, 255.0)), c.a};
}

double intensitet(double ambient, double diffuse, double exponent, double nl,
                  double rim = 0.0, double nr = 0.0)
{
    return ambient + diffuse * std::pow(std::max(nl, 0.0), exponent) +
           rim * std::max(nr, 0.0);
}

gfx::Color blanda(gfx::Color a, gfx::Color b, double t)
{
    return {kanal(a.r + (b.r - a.r) * t), kanal(a.g + (b.g - a.g) * t),
            kanal(a.b + (b.b - a.b) * t), 255};
}

// Luminansen, Rec. 709 på de gammakodade talen.
double luminans(gfx::Color c)
{
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

gfx::Color mättad(gfx::Color c, double s)
{
    const double l = luminans(c);
    return {kanal(l + (c.r - l) * s), kanal(l + (c.g - l) * s),
            kanal(l + (c.b - l) * s), 255};
}

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 enhet(V3 a)
{
    const double l = std::sqrt(dot(a, a));
    return l > 0.0 ? a * (1.0 / l) : a;
}
glaze::Vec3 vec(V3 a) { return {a.x, a.y, a.z}; }
glaze::Float3 f3(V3 a)
{
    return {static_cast<float>(a.x), static_cast<float>(a.y),
            static_cast<float>(a.z)};
}

// ----------------------------------------------------------------- fönstret

using Ritning = std::function<void(gfx::Renderer&)>;

class Yta : public glaze::Widget {
public:
    Ritning rita;
    void    draw(const glaze::Ctx& c) override
    {
        c.r->fillRect(c.box, kVit);
        if (rita)
            rita(*c.r);
    }
};

class Fönster : public glaze::AppWindow {
public:
    Fönster()
    {
        setTitle("mesh_program_gate");
        setClientSize(320, 240);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        root().adopt(yta);
    }
    Yta yta;
};

// En frame där `rita` binder render-mål och ritar i dem.
void iMål(Fönster& f, Ritning rita)
{
    f.yta.rita = std::move(rita);
    f.yta.invalidate();
    f.window().renderFrame();
    f.yta.rita = nullptr;
    Bild b;
    if (!f.window().captureFrame(b.px, b.w, b.h)) {
        std::printf("  FEL  kunde inte rendera/läsa en frame — ingen "
                    "headless-yta?\n");
        g_ritfel = true;
        ++g_fel;
    }
}

Bild läs(gfx::Renderer& r, gfx::RenderTarget& t, const std::string& vad)
{
    Bild b;
    if (r.readTarget(t, b.px)) {
        b.w = t.image().width();
        b.h = t.image().height();
    } else {
        b.px.clear();
    }
    check(!b.px.empty(), vad + ": readTarget läser målet");
    return b;
}

std::unique_ptr<gfx::RenderTarget> mål(gfx::Renderer& r, int w, int h,
                                       bool djup = true)
{
    gfx::TargetDesc d;
    d.depth = djup;
    auto t  = r.createRenderTarget(w, h, d);
    t->image().setFilter(gfx::ImageFilter::Nearest);
    return t;
}

// ------------------------------------------------------------- kamerorna

constexpr int    kSida  = 128;     // målets sida
constexpr double kBränn = 128.0;   // hålkamerans brännvidd i px
constexpr double kSkala = 4.0;     // vyn ovanifrån: px per världsenhet

// HÅLKAMERAN: ögat i origo (eller i `öga`), blicken längs +y, upp längs +z.
gfx::View3D hålkamera(V3 öga = {})
{
    gfx::View3D v;
    v.eye        = gfx::Vec3d{öga.x, öga.y, öga.z};
    v.forward    = gfx::Vec3d{0.0, 1.0, 0.0};
    v.up         = gfx::Vec3d{0.0, 0.0, 1.0};
    v.projection = gfx::Projection3D::Perspective;
    v.fovY       = 2.0 * std::atan(0.5 * kSida / kBränn);
    v.nearZ      = 0.5;
    v.farZ       = 100.0;
    v.box        = gfx::Rectf{0.0f, 0.0f, static_cast<float>(kSida),
                              static_cast<float>(kSida)};
    return v;
}
double hålX(V3 p) { return kSida / 2.0 + kBränn * p.x / p.y; }
double hålY(V3 p) { return kSida / 2.0 - kBränn * p.z / p.y; }

// VYN OVANIFRÅN (ortografisk): ögat på z = 10, blicken nedåt, norr uppåt.
gfx::View3D ovanifrån()
{
    gfx::View3D v;
    v.eye             = gfx::Vec3d{0.0, 0.0, 10.0};
    v.forward         = gfx::Vec3d{0.0, 0.0, -1.0};
    v.up              = gfx::Vec3d{0.0, 1.0, 0.0};
    v.projection      = gfx::Projection3D::Orthographic;
    v.orthoHalfHeight = kSida / (2.0 * kSkala);
    v.nearZ           = 1.0;
    v.farZ            = 20.0;
    v.box             = gfx::Rectf{0.0f, 0.0f, static_cast<float>(kSida),
                                   static_cast<float>(kSida)};
    return v;
}
int ovX(double x) { return static_cast<int>(std::floor(kSida / 2.0 + kSkala * x)); }
int ovY(double y) { return static_cast<int>(std::floor(kSida / 2.0 - kSkala * y)); }

// GlazeFrame för vyn och anropets origo, med ritytans del (`scale`).
gfx::FrameUniforms ram(const gfx::View3D& v, V3 origo = {},
                       float pxPerMm = 0.0f, float uiScale = 1.0f)
{
    gfx::FrameUniforms f = gfx::frameUniforms(v, origo.x, origo.y, origo.z);
    f.scale[0]           = pxPerMm;
    f.scale[1]           = uiScale;
    f.scale[2]           = 1.0f;
    return f;
}

gfx::RenderState läge(gfx::DepthTest d = gfx::DepthTest::Less,
                      gfx::Cull c = gfx::Cull::None)
{
    gfx::RenderState s;
    s.depth      = d;
    s.depthWrite = d != gfx::DepthTest::Off;
    s.cull       = c;
    return s;
}

// -------------------------------------------------------------- programmen

// Programmen per variant, kompilerade en gång (V1) — rivs före fönstret.
struct Program {
    std::map<std::uint32_t, std::unique_ptr<gfx::ShaderProgram>> mesh, points;

    const gfx::ShaderProgram* meshP(gfx::Renderer& r, const dt::MeshVariant& v)
    {
        auto& p = mesh[v.key()];
        if (!p)
            p = r.createProgram(dt::meshProgramDesc(v));
        return p->valid() ? p.get() : nullptr;
    }
    const gfx::ShaderProgram* pointP(gfx::Renderer& r, const dt::PointVariant& v)
    {
        auto& p = points[v.key()];
        if (!p)
            p = r.createProgram(dt::pointProgramDesc(v));
        return p->valid() ? p.get() : nullptr;
    }
};
Program* g_prog = nullptr;

// ------------------------------------------------------------ geometrierna

// En mesh på GPU:n i en variants form.
struct Gpu {
    dt::MeshVariant                 variant;
    gfx::VertexLayout               layout;
    std::unique_ptr<gfx::GpuBuffer> vb, ib;
    std::uint32_t                   count = 0;
};

Gpu ladda(gfx::Renderer& r, const dt::MeshVariant& v,
          const std::vector<dt::MeshVertex>& verts,
          const std::vector<std::uint32_t>& idx)
{
    Gpu g;
    g.variant         = v;
    g.layout          = dt::meshLayout(v);
    const auto stride = g.layout.stride[0];
    std::vector<std::uint8_t> bytes(verts.size() * stride);
    for (std::size_t i = 0; i < verts.size(); ++i)
        dt::packMeshVertex(v, verts[i], bytes.data() + i * stride);
    g.vb    = r.createBuffer(gfx::BufferKind::Vertex, bytes.size(), bytes.data());
    g.ib    = r.createBuffer(gfx::BufferKind::Index,
                             idx.size() * sizeof(std::uint32_t), idx.data());
    g.count = static_cast<std::uint32_t>(idx.size());
    return g;
}

// En KVADRAT med mitten c och halvaxlarna e1, e2: hörnen c − e1 − e2, c + e1 −
// e2, c − e1 + e2, c + e1 + e2 och trianglarna (0, 1, 2), (1, 3, 2) — MOTURS
// kring e1 × e2, som alltså är FRAMSIDANS normal (den varje vertex bär).
// uv: hörn 2 är bildens övre vänstra (0, 0), hörn 1 dess nedre högra (1, 1).
struct Kvadrat {
    std::vector<dt::MeshVertex> v;
    std::vector<std::uint32_t>  idx = {0, 1, 2, 1, 3, 2};
};
Kvadrat kvadrat(V3 c, V3 e1, V3 e2, float skalär = 0.0f,
                gfx::Color col = {255, 255, 255, 255})
{
    const V3    n     = enhet(cross(e1, e2));
    const V3    p[4]  = {c - e1 - e2, c + e1 - e2, c - e1 + e2, c + e1 + e2};
    const float uv[4][2] = {{0, 1}, {1, 1}, {0, 0}, {1, 0}};
    Kvadrat     k;
    for (int i = 0; i < 4; ++i) {
        dt::MeshVertex m;
        m.position = f3(p[i]);
        m.normal   = f3(n);
        m.color    = col;
        m.u        = uv[i][0];
        m.v        = uv[i][1];
        m.scalar   = skalär;
        k.v.push_back(m);
    }
    return k;
}

Gpu ladda(gfx::Renderer& r, const dt::MeshVariant& v, const Kvadrat& k)
{
    return ladda(r, v, k.v, k.idx);
}

// Ett rit-anrop med Mesh-programmet.
struct Anrop {
    const gfx::FrameUniforms*                          frame = nullptr;
    gfx::RenderState                                   state;
    std::array<const gfx::Image*, gfx::kMaxProgramTextures> tex{};
    const gfx::GpuBuffer*                              inst      = nullptr;
    std::size_t                                        instFörst = 0;
    std::uint32_t                                      instAntal = 0;
};

void rita(gfx::Renderer& r, const Gpu& g, const dt::MeshUniforms& u,
          const Anrop& a)
{
    const gfx::ShaderProgram* p = g_prog->meshP(r, g.variant);
    if (!p || !g.vb || !g.ib)
        return;
    gfx::MeshDraw d;
    d.program    = gfx::MeshProgram::Custom;
    d.shader     = p;
    d.layout     = &g.layout;
    d.vertices   = gfx::slice(*g.vb);
    d.indices    = gfx::slice(*g.ib);
    d.indexCount = g.count;
    d.frame      = *a.frame;
    d.user       = &u;
    d.userBytes  = sizeof u;
    d.textures   = a.tex;
    d.state      = a.state;
    if (a.inst) {
        d.instances     = gfx::BufferSlice{a.inst,
                                       a.instFörst * sizeof(dt::MeshInstance),
                                       a.instAntal * sizeof(dt::MeshInstance)};
        d.instanceCount = a.instAntal;
    }
    r.drawMesh(d);
}

// Ett nytt mål (svart, med djup), formerna ritade i det med `ritning`.
std::unique_ptr<gfx::RenderTarget>
målMed(Fönster& f, const std::function<void(gfx::Renderer&)>& ritning,
       bool djup = true, gfx::Color bakgrund = kSvart)
{
    gfx::Renderer& r = f.window().renderer();
    auto           t = mål(r, kSida, kSida, djup);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*t, bakgrund);
        ritning(rr);
        rr.popTarget();
    });
    return t;
}

// En bild ur pixlar (RGBA8, rad 0 överst).
std::unique_ptr<gfx::Image> bild(gfx::Renderer& r, int w, int h,
                                 const std::vector<gfx::Color>& c,
                                 gfx::ImageFilter filter)
{
    std::vector<std::uint8_t> px;
    for (const gfx::Color& k : c) {
        px.push_back(k.r);
        px.push_back(k.g);
        px.push_back(k.b);
        px.push_back(k.a);
    }
    auto img = r.createImage(w, h, gfx::ImageFormat::Rgba8, px.data(), 0);
    img->setFilter(filter);
    return img;
}

// 2D-gradientens ramp (256 texlar, Linear — bildens förval), som ritytan gör.
std::unique_ptr<gfx::Image> ramp256(gfx::Renderer& r,
                                    const std::vector<gfx::GradientStop>& s)
{
    gfx::GradientRamp px{};
    gfx::buildGradientRamp(s, px);
    return r.createImage(gfx::kGradientRampSize, 1, gfx::ImageFormat::Rgba8,
                         px.data(), 0);
}

const std::vector<gfx::GradientStop> kTreStopp = {
    {0.0f, kBlå}, {0.5f, kGrön}, {1.0f, kRöd}};

// Rampens förväntade värde: de tre stoppen, linjärt mellan dem (alla ogenomskinliga).
gfx::Color rampFärg(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? blanda(kBlå, kGrön, t / 0.5)
                   : blanda(kGrön, kRöd, (t - 0.5) / 0.5);
}

// --------------------------------------------------------------------- V1
void fallV1(Fönster& f)
{
    gfx::Renderer& r       = f.window().renderer();
    int            giltiga = 0, passar = 0;
    std::string    förstaFel, förstaPassar;
    for (std::uint32_t k = 0; k < dt::kMeshVariantCount; ++k) {
        const dt::MeshVariant     v = dt::MeshVariant::fromKey(k);
        const gfx::ShaderProgram* p = g_prog->meshP(r, v);
        if (!p) {
            if (förstaFel.empty())
                förstaFel = kort(g_prog->mesh[k]->error());
            continue;
        }
        ++giltiga;
        const gfx::VertexLayout l    = dt::meshLayout(v);
        std::uint32_t           mask = 0;
        for (const gfx::VertexAttrib& a : l.attribs)
            mask |= 1u << a.location;
        const bool ok = p->inputs() == mask && p->integerInputs() == 0 &&
                        p->userBytes() == sizeof(dt::MeshUniforms) &&
                        p->textures() == dt::meshSlots(v).count;
        passar += ok ? 1 : 0;
        if (!ok && förstaPassar.empty())
            förstaPassar = p->name() + ": läser " + std::to_string(p->inputs()) +
                           ", layouten " + std::to_string(mask);
    }
    check(giltiga == static_cast<int>(dt::kMeshVariantCount),
          "V1 alla 128 Mesh-varianter kompilerar",
          std::to_string(giltiga) + " giltiga — " + förstaFel);
    check(passar == giltiga,
          "V1 Mesh: reflektionen läser exakt layoutens platser (flyttal), "
          "blocket och texturerna är fabrikens",
          std::to_string(passar) + " av " + std::to_string(giltiga) + " — " +
              förstaPassar);

    giltiga = passar = 0;
    förstaFel.clear();
    for (std::uint32_t k = 0; k < dt::kPointVariantCount; ++k) {
        const dt::PointVariant    v = dt::PointVariant::fromKey(k);
        const gfx::ShaderProgram* p = g_prog->pointP(r, v);
        if (!p) {
            if (förstaFel.empty())
                förstaFel = kort(g_prog->points[k]->error());
            continue;
        }
        ++giltiga;
        std::uint32_t mask = 0;
        for (const gfx::VertexAttrib& a : dt::pointLayout(v).attribs)
            mask |= 1u << a.location;
        passar += p->inputs() == mask && p->integerInputs() == 0 &&
                          p->userBytes() == sizeof(dt::PointUniforms) &&
                          p->textures() == dt::pointSlots(v).count
                      ? 1
                      : 0;
    }
    check(giltiga == static_cast<int>(dt::kPointVariantCount),
          "V1 alla 4 Points-varianter kompilerar",
          std::to_string(giltiga) + " giltiga — " + förstaFel);
    check(passar == giltiga,
          "V1 Points: reflektionen läser exakt layoutens platser",
          std::to_string(passar) + " av " + std::to_string(giltiga));
}

// --------------------------------------------------------------------- E1
//
// Kvadraten på djupet 4 rakt mot kameran (normalen −y), ljusets färdriktning
// +y — alltså MOT ljuset: N·L = 1.
void fallE1(Fönster& f)
{
    gfx::Renderer&           r = f.window().renderer();
    const gfx::View3D        v = hålkamera();
    const gfx::FrameUniforms fr = ram(v);
    dt::MeshVariant          mv;
    mv.normals = true;
    const Gpu kv = ladda(r, mv, kvadrat({0, 4, 0}, {2, 0, 0}, {0, 0, 2}));
    const V3  N{0, -1, 0};

    struct Prov {
        const char* namn;
        V3          ljus;   // färdriktningen
        double      ambient, diffuse, exponent;
        V3          rimRikt;
        double      rim;
    };
    const Prov prov[] = {
        {"rakt mot ljuset = färg × (ambient + diffuse)", {0, 1, 0}, 0.3, 0.5,
         1.0, {0, 1, 0}, 0.0},
        {"exponenten: N·L = 0,6 med exponent 2 ger 0,36", {-0.8, 0.6, 0}, 0.3,
         0.5, 2.0, {0, 1, 0}, 0.0},
        {"rimljuset MOT normalen (N·R = 1) läggs till", {1, 0, 0}, 0.3, 0.5,
         1.0, {0, -1, 0}, 0.4},
        {"rimljuset BORT från normalen bidrar inte", {1, 0, 0}, 0.3, 0.5, 1.0,
         {0, 1, 0}, 0.4},
    };
    for (const Prov& p : prov) {
        dt::MeshUniforms u;
        dt::setColor(u, kYta);
        dt::setLight(u, vec(p.ljus), static_cast<float>(p.ambient),
                     static_cast<float>(p.diffuse),
                     static_cast<float>(p.exponent), false);
        dt::setRim(u, vec(p.rimRikt), static_cast<float>(p.rim));
        Anrop a;
        a.frame      = &fr;
        a.state      = läge();
        auto t       = målMed(f, [&](gfx::Renderer& rr) { rita(rr, kv, u, a); });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "E1");
        const double nl = dot(N, enhet(V3{} - p.ljus));
        const double nr = dot(N, enhet(p.rimRikt));
        const gfx::Color väntat =
            belyst(kYta, intensitet(p.ambient, p.diffuse, p.exponent, nl,
                                    p.rim, nr));
        check(ärFärg(b.at(64, 64), väntat) && ärFärg(b.at(40, 90), väntat),
              std::string("E1 ") + p.namn,
              px(b.at(64, 64)) + " väntat " + färg(väntat));
    }

    // NORMALEN GENOM MODELLEN: samma kvadrat modellerad i planet z = 0 med
    // normalen +z, och modellen vrider +z till −y och +y till +z.
    {
        const Gpu   lokal = ladda(r, mv, kvadrat({0, 0, 0}, {2, 0, 0}, {0, 2, 0}));
        const float rader[12] = {1, 0, 0, 0, 0, 0, -1, 4, 0, 1, 0, 0};
        dt::MeshUniforms u;
        dt::setColor(u, kYta);
        dt::setModel(u, rader);
        dt::setLight(u, {0, 1, 0}, 0.3f, 0.5f, 1.0f, false);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, lokal, u, a); });
        if (g_ritfel)
            return;
        const Bild       b      = läs(r, *t, "E1 modellen");
        const gfx::Color väntat = belyst(kYta, intensitet(0.3, 0.5, 1.0, 1.0));
        check(ärFärg(b.at(64, 64), väntat),
              "E1 normalen genom modellen: en kvadrat med normalen +z vriden "
              "mot kameran är belyst som den vridna",
              px(b.at(64, 64)) + " väntat " + färg(väntat));
    }

    // INVERS-TRANSPONATET: en yta i planet y = z (normalen (0, −1, 1)/√2)
    // sträckt × 2 längs z blir planet z = 2y med normalen (0, −2, 1)/√5 —
    // modellmatrisen själv hade gett (0, −1, 2)/√5.
    {
        const Gpu   lutad = ladda(r, mv, kvadrat({0, 0, 0}, {1, 0, 0}, {0, 1, 1}));
        const float rader[12] = {1, 0, 0, 0, 0, 1, 0, 6, 0, 0, 2, 0};
        dt::MeshUniforms u;
        dt::setColor(u, kYta);
        dt::setModel(u, rader);
        dt::setLight(u, {0, 1, 0}, 0.0f, 1.0f, 1.0f, false);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, lutad, u, a); });
        if (g_ritfel)
            return;
        const Bild       b      = läs(r, *t, "E1 invers-transponatet");
        const double     nl     = dot(enhet({0, -2, 1}), {0, -1, 0});
        const gfx::Color väntat = belyst(kYta, intensitet(0.0, 1.0, 1.0, nl));
        check(ärFärg(b.at(64, 64), väntat),
              "E1 normalen genom modellens INVERS-TRANSPONAT (olikformig skala)",
              px(b.at(64, 64)) + " väntat " + färg(väntat));
    }
}

// --------------------------------------------------------------------- E2
//
// BAKSIDAN: kvadraten på djupet 4 med framsidan BORT från kameran (normalen
// +y, trianglarna medurs sett från kameran), ljuset från kamerans sida.
void fallE2(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    const Kvadrat            k  = kvadrat({0, 4, 0}, {0, 0, 2}, {2, 0, 0});
    for (bool normaler : {true, false}) {
        dt::MeshVariant mv;
        mv.normals  = normaler;
        const Gpu g = ladda(r, mv, k);
        for (gfx::DepthTest d : {gfx::DepthTest::Less, gfx::DepthTest::Off}) {
            for (bool tvåsidigt : {true, false}) {
                dt::MeshUniforms u;
                dt::setColor(u, kYta);
                dt::setLight(u, {0, 1, 0}, 0.3f, 0.5f, 1.0f, tvåsidigt);
                Anrop a;
                a.frame = &fr;
                a.state = läge(d);
                auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, g, u, a); });
                if (g_ritfel)
                    return;
                const Bild       b = läs(r, *t, "E2");
                const gfx::Color väntat =
                    belyst(kYta, intensitet(0.3, 0.5, 1.0, tvåsidigt ? 1.0 : -1.0));
                check(ärFärg(b.at(64, 64), väntat),
                      std::string("E2 baksidan ") +
                          (tvåsidigt ? "TVÅSIDIGT belyst som framsidan"
                                     : "ensidigt bara ambient") +
                          (normaler ? " (normaler" : " (platt normal") +
                          (d == gfx::DepthTest::Off ? ", utan djup)" : ", Less)"),
                      px(b.at(64, 64)) + " väntat " + färg(väntat));
            }
        }
    }
}

// --------------------------------------------------------------------- E3
//
// DEN PLATTA NORMALEN: en lutad kvadrat utan normaler med ytans normal
// n = (−0,6, −0,8, 0) — mot kameran och åt −x. Två ljus mäter normalens två
// komponenter: L = −y ger N·L = 0,8 och L = −x ger 0,6.
void fallE3(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    const V3                 n{-0.6, -0.8, 0.0};
    const V3                 e2{0.8, -0.6, 0.0};   // i planet, e2 × e1 = n
    const V3                 e1{0.0, 0.0, 1.0};
    dt::MeshVariant          mv;   // inga normaler
    const Gpu fram = ladda(r, mv, kvadrat({0, 6, 0}, e2 * 2.0, e1 * 2.0));
    const Gpu bak  = ladda(r, mv, kvadrat({0, 6, 0}, e1 * 2.0, e2 * 2.0));
    struct Prov {
        const char* namn;
        const Gpu*  g;
        V3          ljus;   // färdriktningen
        bool        tvåsidigt;
        double      nl;     // förväntat värde: N·L med ytans normal, vänd efter sidan
    };
    const Prov prov[] = {
        {"ljus längs blicken: N·L = 0,8", &fram, {0, 1, 0}, false,
         dot(n, {0, -1, 0})},
        {"ljus från sidan: N·L = 0,6", &fram, {1, 0, 0}, false,
         dot(n, {-1, 0, 0})},
        {"baksidan ensidigt: N·L = −0,8 (bara ambient)", &bak, {0, 1, 0}, false,
         -dot(n, {0, -1, 0})},
        {"baksidan tvåsidigt: N·L = 0,8", &bak, {0, 1, 0}, true,
         dot(n, {0, -1, 0})},
    };
    for (gfx::DepthTest d : {gfx::DepthTest::Less, gfx::DepthTest::Off}) {
        for (const Prov& p : prov) {
            dt::MeshUniforms u;
            dt::setColor(u, kYta);
            dt::setLight(u, vec(p.ljus), 0.2f, 0.6f, 1.0f, p.tvåsidigt);
            Anrop a;
            a.frame = &fr;
            a.state = läge(d);
            auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, *p.g, u, a); });
            if (g_ritfel)
                return;
            const Bild       b      = läs(r, *t, "E3");
            const gfx::Color väntat = belyst(kYta, intensitet(0.2, 0.6, 1.0, p.nl));
            check(ärFärg(b.at(64, 64), väntat) && ärFärg(b.at(60, 50), väntat),
                  std::string("E3 platt normal ur derivatorna, ") + p.namn +
                      (d == gfx::DepthTest::Off ? " (utan djup)" : " (Less)"),
                  px(b.at(64, 64)) + " väntat " + färg(väntat));
        }
    }
}

// --------------------------------------------------------------------- E4
//
// TEXTUREN: kvadraten på djupet 8 täcker px 32–96; bildens fyra texlar ska stå
// i sina hörn, rad 0 överst.
void fallE4(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    dt::MeshVariant          mv;
    mv.uvs      = true;
    const Gpu k = ladda(r, mv, kvadrat({0, 8, 0}, {2, 0, 0}, {0, 0, 2}));
    auto tex = bild(r, 2, 2, {kRöd, kGrön, kBlå, kVit}, gfx::ImageFilter::Nearest);

    // Samma mönster i ett 2 × 2-mål (premultiplicerat, på GL speglat), med en
    // HALVGENOMSKINLIG röd texel: av-premultiplicerad blir den röd med alfa ½.
    auto mönster = mål(r, 2, 2, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*mönster, gfx::Color{0, 0, 0, 0});
        rr.fillRect(gfx::Rectf{0, 0, 1, 1}, gfx::Color{255, 0, 0, 128});
        rr.fillRect(gfx::Rectf{1, 0, 1, 1}, kGrön);
        rr.fillRect(gfx::Rectf{0, 1, 1, 1}, kBlå);
        rr.fillRect(gfx::Rectf{1, 1, 1, 1}, kVit);
        rr.popTarget();
    });
    if (g_ritfel)
        return;

    for (int vilken = 0; vilken < 2; ++vilken) {
        const gfx::Image& img = vilken == 0 ? *tex : mönster->image();
        dt::MeshUniforms  u;
        dt::setTexture(u, img);
        Anrop a;
        a.frame  = &fr;
        a.state  = läge();
        a.tex[0] = &img;
        auto t   = målMed(f, [&](gfx::Renderer& rr) { rita(rr, k, u, a); });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "E4");
        // Den halvgenomskinliga röda över svart: 255 · ½.
        const gfx::Color övre = vilken == 0 ? kRöd : gfx::Color{128, 0, 0, 255};
        check(ärFärg(b.at(40, 40), övre) && ärFärg(b.at(88, 40), kGrön) &&
                  ärFärg(b.at(40, 88), kBlå) && ärFärg(b.at(88, 88), kVit),
              vilken == 0 ? "E4 texturen rättvänd: rad 0 överst, kolumn 0 till "
                            "vänster"
                          : "E4 ett render-måls bild (premultiplicerad, på GL "
                            "speglad) rättvänd, den halvgenomskinliga texeln "
                            "av-premultiplicerad",
              px(b.at(40, 40)) + " / " + px(b.at(88, 40)) + " / " +
                  px(b.at(40, 88)) + " / " + px(b.at(88, 88)));
    }
}

// --------------------------------------------------------------------- E5
//
// RAMPEN: tre kvadrater med skalären 10, 20 och 30 mot rampen 10–30 (blå, grön,
// röd vid 0, ½ och 1); en kvadrat med skalären 10 till vänster och 30 till
// höger; och en ramp om TRE texlar, där t = 0,2 ligger 40 % in mellan texel 0
// och 1.
void fallE5(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    dt::MeshVariant          mv;
    mv.scalars = true;
    auto rampen = ramp256(r, kTreStopp);

    Gpu stopp[3];
    for (int i = 0; i < 3; ++i)
        stopp[i] = ladda(r, mv,
                         kvadrat({-3.0 + 3.0 * i, 8, 0}, {1, 0, 0}, {0, 0, 1},
                                 10.0f + 10.0f * static_cast<float>(i)));
    Kvadrat lutning = kvadrat({0, 8, -2.5}, {2, 0, 0}, {0, 0, 1});
    for (dt::MeshVertex& m : lutning.v)
        m.scalar = m.position.x < 0.0f ? 10.0f : 30.0f;
    const Gpu över = ladda(r, mv, lutning);

    dt::MeshUniforms u;
    dt::setRamp(u, 10.0, 30.0, *rampen);
    Anrop a;
    a.frame  = &fr;
    a.state  = läge();
    a.tex[0] = rampen.get();
    auto t   = målMed(f, [&](gfx::Renderer& rr) {
        for (const Gpu& g : stopp)
            rita(rr, g, u, a);
        rita(rr, över, u, a);
    });
    if (g_ritfel)
        return;
    const Bild b = läs(r, *t, "E5");
    // Kvadraternas mitt: x = −3, 0, 3 på djupet 8 → px 16, 64, 112, rad 64.
    check(ärFärg(b.at(16, 64), kBlå) && ärFärg(b.at(64, 64), kGrön) &&
              ärFärg(b.at(112, 64), kRöd),
          "E5 rampens tre stopp: min blå, mitten grön, max röd",
          px(b.at(16, 64)) + " / " + px(b.at(64, 64)) + " / " +
              px(b.at(112, 64)));
    // Lutningen: skalären 20 + 5x över x ∈ [−2, 2] (10 till vänster, 30 till
    // höger), räknad i pixelns MITT — kolumn 48 (t ≈ ¼) och 64 (t ≈ ½), rad 104.
    const auto lutningensFärg = [](int kol) {
        const double x = (kol + 0.5 - kSida / 2.0) * 8.0 / kBränn;
        return rampFärg((20.0 + 5.0 * x - 10.0) / 20.0);
    };
    const gfx::Color fjärdedel = lutningensFärg(48), halva = lutningensFärg(64);
    check(ärFärg(b.at(48, 104), fjärdedel) && ärFärg(b.at(64, 104), halva),
          "E5 skalären interpolerad över ytan: t ≈ ¼ halvvägs blå–grön, t ≈ ½ "
          "grön",
          px(b.at(48, 104)) + " väntat " + färg(fjärdedel) + " / " +
              px(b.at(64, 104)) + " väntat " + färg(halva));

    // TRE TEXLAR: skalären 14 → t = 0,2 → 60 % blå + 40 % grön.
    auto tre = bild(r, 3, 1, {kBlå, kGrön, kRöd}, gfx::ImageFilter::Linear);
    dt::MeshUniforms u3;
    dt::setRamp(u3, 10.0, 30.0, *tre);
    const Gpu fjorton =
        ladda(r, mv, kvadrat({0, 8, 0}, {2, 0, 0}, {0, 0, 2}, 14.0f));
    Anrop a3  = a;
    a3.tex[0] = tre.get();
    auto t3   = målMed(f, [&](gfx::Renderer& rr) { rita(rr, fjorton, u3, a3); });
    if (g_ritfel)
        return;
    const Bild       b3     = läs(r, *t3, "E5 tre texlar");
    const gfx::Color väntat = blanda(kBlå, kGrön, 0.4);
    check(ärFärg(b3.at(64, 64), väntat),
          "E5 en ramp om tre texlar samplas LINJÄRT mellan texlarna (t = 0,2 → "
          "60 % blå, 40 % grön)",
          px(b3.at(64, 64)) + " väntat " + färg(väntat));

    // TEXTUR OCH RAMP I ETT ANROP: platserna är `meshSlots` —
    // texturen 0, rampen 1. Texturen grå till vänster och vit till höger,
    // skalären 20 (grön): vänster grått × grönt, höger grönt.
    dt::MeshVariant tv;
    tv.uvs     = true;
    tv.scalars = true;
    const dt::ProgramSlots s = dt::meshSlots(tv);
    const gfx::Color       grå{128, 128, 128, 255};
    auto två = bild(r, 2, 2, {grå, kVit, grå, kVit}, gfx::ImageFilter::Nearest);
    const Gpu tk = ladda(r, tv, kvadrat({0, 8, 0}, {2, 0, 0}, {0, 0, 2}, 20.0f));
    dt::MeshUniforms ut;
    dt::setTexture(ut, *två);
    dt::setRamp(ut, 10.0, 30.0, *rampen);
    Anrop at;
    at.frame          = &fr;
    at.state          = läge();
    at.tex[s.texture] = två.get();
    at.tex[s.ramp]    = rampen.get();
    auto tt = målMed(f, [&](gfx::Renderer& rr) { rita(rr, tk, ut, at); });
    if (g_ritfel)
        return;
    const Bild       bt = läs(r, *tt, "E5 textur och ramp");
    const gfx::Color vänster{0, kanal(grå.g * kGrön.g / 255.0), 0, 255};
    check(s.texture == 0 && s.ramp == 1 && ärFärg(bt.at(40, 64), vänster) &&
              ärFärg(bt.at(88, 64), kGrön),
          "E5 textur och ramp i ett anrop: texturen på plats 0, rampen på 1 "
          "(grått × grönt, vitt × grönt)",
          px(bt.at(40, 64)) + " väntat " + färg(vänster) + " / " +
              px(bt.at(88, 64)));
}

// --------------------------------------------------------------------- E6
void fallE6(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    const gfx::Color         c{40, 220, 30, 255};   // luminans 168, medel 97
    const Gpu k = ladda(r, dt::MeshVariant{}, kvadrat({0, 8, 0}, {2, 0, 0}, {0, 0, 2}));
    for (double s : {0.0, 0.5}) {
        dt::MeshUniforms u;
        dt::setColor(u, c);
        u.adjust[0] = static_cast<float>(s);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, k, u, a); });
        if (g_ritfel)
            return;
        const Bild       b      = läs(r, *t, "E6");
        const gfx::Color väntat = mättad(c, s);
        check(ärFärg(b.at(64, 64), väntat),
              s == 0.0 ? "E6 mättnad 0 ger grått — luminansen, inte medelvärdet"
                       : "E6 mättnad 0,5 ligger halvvägs mot luminansen",
              px(b.at(64, 64)) + " väntat " + färg(väntat));
    }

    // FÄRGJUSTERINGENS andra två tal i blockets `adjust` — förväntat värde
    // definitionen: kontrasten 0 är mittgrått (127,5 av 255), ljusheten
    // multiplicerar färgen.
    struct Justering {
        float       kontrast, ljushet;
        gfx::Color  väntat;
        const char* vad;
    };
    const Justering justeringar[] = {
        {0.0f, 1.0f, {128, 128, 128, 255}, "E6 kontrasten 0 ger mittgrått"},
        {1.0f, 0.5f, {kanal(c.r * 0.5), kanal(c.g * 0.5), kanal(c.b * 0.5), 255},
         "E6 ljusheten 0,5 halverar färgen"}};
    for (const Justering& j : justeringar) {
        dt::MeshUniforms u;
        dt::setColor(u, c);
        u.adjust[1] = j.kontrast;
        u.adjust[2] = j.ljushet;
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, k, u, a); });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "E6 justeringen");
        check(ärFärg(b.at(64, 64), j.väntat), j.vad,
              px(b.at(64, 64)) + " väntat " + färg(j.väntat));
    }

    // VERTEXFÄRGEN (HAS_COLOR) multiplicerar stilens färg.
    dt::MeshVariant cv;
    cv.colors = true;
    const Gpu        kc = ladda(r, cv, kvadrat({0, 8, 0}, {2, 0, 0}, {0, 0, 2}, 0.0f, c));
    const gfx::Color stil{255, 128, 255, 255};
    dt::MeshUniforms u;
    dt::setColor(u, stil);
    Anrop a;
    a.frame = &fr;
    a.state = läge();
    auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, kc, u, a); });
    if (g_ritfel)
        return;
    const Bild       b = läs(r, *t, "E6 vertexfärgen");
    const gfx::Color väntat{kanal(c.r * stil.r / 255.0), kanal(c.g * stil.g / 255.0),
                            kanal(c.b * stil.b / 255.0), 255};
    check(ärFärg(b.at(64, 64), väntat),
          "E6 vertexfärgen multiplicerar stilens färg",
          px(b.at(64, 64)) + " väntat " + färg(väntat));
}

// --------------------------------------------------------------------- E7
//
// DIMMAN 10–20 med power 2 mot en blå dimfärg: en yta på djupet 20 (fogEnd)
// och en på djupet 15 (halvvägs: (½)² = ¼). Mittpixelns avstånd är djupet.
void fallE7(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(hålkamera());
    const gfx::Color         dimma{20, 40, 220, 255};
    for (double djup : {20.0, 15.0, 30.0}) {
        const Gpu k = ladda(r, dt::MeshVariant{},
                            kvadrat({0, djup, 0}, {djup / 4, 0, 0}, {0, 0, djup / 4}));
        dt::MeshUniforms u;
        dt::setColor(u, kYta);
        dt::setFog(u, dimma, 10.0, 20.0, 2.0f);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, k, u, a); });
        if (g_ritfel)
            return;
        const Bild   b = läs(r, *t, "E7");
        const double andel =
            std::pow(std::clamp((djup - 10.0) / (20.0 - 10.0), 0.0, 1.0), 2.0);
        const gfx::Color väntat = blanda(kYta, dimma, andel);
        check(ärFärg(b.at(64, 64), väntat),
              djup == 20.0 ? "E7 dimman vid fogEnd ger dimfärgen"
              : djup == 15.0
                  ? "E7 dimman halvvägs med power 2 ger en fjärdedel"
                  : "E7 bortom fogEnd dimfärgen",
              px(b.at(64, 64)) + " väntat " + färg(väntat));
    }
}

// --------------------------------------------------------------------- E8
//
// RTC: en mesh — två lutade ytor utan normaler, belysta och i dimma som
// varierar över dem — med origot (0, 6, 0) och ögat i origo, och SAMMA lokala
// mesh med origot vid SWEREF och ögat lika långt därifrån.
bool sammaBild(const Bild& a, const Bild& b, int& olika)
{
    // Varje pixel i den ena har en pixel inom 1 px i den andra med samma färg
    // (±3) — åt båda hållen.
    const auto täckt = [](const Bild& p, const Bild& q, int x, int y) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint8_t* s = q.at(x + dx, y + dy);
                const std::uint8_t* t = p.at(x, y);
                if (std::abs(s[0] - t[0]) <= 3 && std::abs(s[1] - t[1]) <= 3 &&
                    std::abs(s[2] - t[2]) <= 3)
                    return true;
            }
        return false;
    };
    olika = 0;
    if (a.px.empty() || b.px.empty() || a.w != b.w || a.h != b.h)
        return false;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            if (!täckt(a, b, x, y) || !täckt(b, a, x, y))
                ++olika;
    return olika == 0;
}

void fallE8(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    // Två ytor runt origot: en lutad åt −x och en åt +x.
    Kvadrat ytor = kvadrat({-1.2, 0, 0}, V3{0.8, -0.6, 0} * 1.2, {0, 0, 1.5});
    const Kvadrat andra = kvadrat({1.2, 0, 0}, V3{0.8, 0.6, 0} * 1.2, {0, 0, 1.5});
    for (const dt::MeshVertex& m : andra.v)
        ytor.v.push_back(m);
    for (std::uint32_t i : andra.idx)
        ytor.idx.push_back(i + 4);
    const Gpu g = ladda(r, dt::MeshVariant{}, ytor);

    dt::MeshUniforms u;
    dt::setColor(u, kYta);
    dt::setLight(u, {0.3, 1, -0.4}, 0.2f, 0.7f, 1.5f, true);
    dt::setFog(u, gfx::Color{20, 40, 220, 255}, 5.0, 7.5, 1.5f);

    Bild bilder[2];
    const V3 origon[2] = {{0, 6, 0}, {674000.0, 6580000.0 + 6.0, 30.0}};
    for (int i = 0; i < 2; ++i) {
        const V3                 öga = origon[i] - V3{0, 6, 0};
        const gfx::FrameUniforms fr  = ram(hålkamera(öga), origon[i]);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, g, u, a); });
        if (g_ritfel)
            return;
        bilder[i] = läs(r, *t, i == 0 ? "E8 origo" : "E8 SWEREF");
    }
    int ritade = 0;
    for (int y = 0; y < bilder[0].h; ++y)
        for (int x = 0; x < bilder[0].w; ++x)
            ritade += ärFärg(bilder[0].at(x, y), kSvart, 0) ? 0 : 1;
    int        olika = 0;
    const bool lika  = sammaBild(bilder[0], bilder[1], olika);
    check(ritade > 2000 && lika,
          "E8 RTC: samma mesh (ljus, dimma, platt normal) vid origo och vid "
          "SWEREF ger samma bild ±1 px",
          std::to_string(ritade) + " ritade px, " + std::to_string(olika) +
              " utan motsvarighet");
}

// --------------------------------------------------------------------- E9
//
// INSTANSERNA i vyn ovanifrån. Prototypen är en STAV [0, 4] × [−½, ½] i planet
// z = 0 (normalen +z), vit. Instansen = förflyttning · gir (moturs kring z) ·
// skala, i grindens egen trigonometri.
dt::MeshInstance instans(V3 p, double girGrader, V3 s, gfx::Color c)
{
    const double     g = girGrader * 3.14159265358979323846 / 180.0;
    const double     cs = std::cos(g), sn = std::sin(g);
    dt::MeshInstance m;
    const float      rader[12] = {
        static_cast<float>(cs * s.x), static_cast<float>(-sn * s.y), 0.0f,
        static_cast<float>(p.x),      static_cast<float>(sn * s.x),
        static_cast<float>(cs * s.y), 0.0f,                          static_cast<float>(p.y),
        0.0f,                         0.0f,                          static_cast<float>(s.z),
        static_cast<float>(p.z)};
    std::copy(std::begin(rader), std::end(rader), std::begin(m.rows));
    m.color = c;
    return m;
}

// Stavens lokala punkt (lx, ly) genom instansen → världen.
V3 iVärlden(V3 p, double girGrader, V3 s, double lx, double ly)
{
    const double g = girGrader * 3.14159265358979323846 / 180.0;
    return {p.x + std::cos(g) * s.x * lx - std::sin(g) * s.y * ly,
            p.y + std::sin(g) * s.x * lx + std::cos(g) * s.y * ly, p.z};
}

void fallE9(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(ovanifrån());
    dt::MeshVariant          mv;
    mv.instanced = true;
    const Gpu stav = ladda(r, mv, kvadrat({2, 0, 0}, {2, 0, 0}, {0, 0.5, 0}));

    struct Inst {
        V3         p;
        double     gir;
        V3         s;
        gfx::Color c;
    };
    const Inst in[3] = {{{-12, 8, 0}, 0.0, {1, 1, 1}, kRöd},
                        {{-4, -8, 0}, 90.0, {2, 1, 1}, kGrön},
                        {{6, -4, 0}, 30.0, {1.5, 2, 1}, kBlå}};
    std::vector<dt::MeshInstance> data;
    for (const Inst& i : in)
        data.push_back(instans(i.p, i.gir, i.s, i.c));
    auto buf = r.createBuffer(gfx::BufferKind::Vertex,
                              data.size() * sizeof(dt::MeshInstance), data.data());

    dt::MeshUniforms u;
    Anrop            a;
    a.frame     = &fr;
    a.state     = läge();
    a.inst      = buf.get();
    a.instAntal = 3;
    auto t3     = målMed(f, [&](gfx::Renderer& rr) { rita(rr, stav, u, a); });
    Anrop delen     = a;
    delen.instFörst = 1;
    delen.instAntal = 2;
    auto t2 = målMed(f, [&](gfx::Renderer& rr) { rita(rr, stav, u, delen); });
    if (g_ritfel)
        return;
    const Bild b3 = läs(r, *t3, "E9");
    const Bild b2 = läs(r, *t2, "E9 delen");

    // Mätpunkterna: stavens lokala (3, 0) och (0,5, 0) i varje instans — och
    // för gir och skala en punkt som bara den rätta matrisen täcker.
    for (int i = 0; i < 3; ++i) {
        const V3 p1 = iVärlden(in[i].p, in[i].gir, in[i].s, 3.0, 0.0);
        const V3 p2 = iVärlden(in[i].p, in[i].gir, in[i].s, 0.5, 0.0);
        const std::uint8_t* a1 = b3.at(ovX(p1.x), ovY(p1.y));
        const std::uint8_t* a2 = b3.at(ovX(p2.x), ovY(p2.y));
        check(ärFärg(a1, in[i].c) && ärFärg(a2, in[i].c),
              "E9 instans " + std::to_string(i) +
                  " på sitt läge med sin gir och skala, i sin färg",
              px(a1) + " / " + px(a2) + " väntat " + färg(in[i].c));
        // Speglad gir (den transponerade matrisen) täcker inte (3, 0).
        const V3 spegel = iVärlden(in[i].p, -in[i].gir, in[i].s, 3.0, 0.0);
        if (in[i].gir != 0.0)
            check(ärFärg(b3.at(ovX(spegel.x), ovY(spegel.y)), kSvart),
                  "E9 instans " + std::to_string(i) +
                      ": giren åt rätt håll (den speglade platsen är tom)",
                  px(b3.at(ovX(spegel.x), ovY(spegel.y))));
    }
    const V3 noll = iVärlden(in[0].p, in[0].gir, in[0].s, 3.0, 0.0);
    const V3 ett  = iVärlden(in[1].p, in[1].gir, in[1].s, 3.0, 0.0);
    const V3 två  = iVärlden(in[2].p, in[2].gir, in[2].s, 3.0, 0.0);
    check(ärFärg(b2.at(ovX(noll.x), ovY(noll.y)), kSvart) &&
              ärFärg(b2.at(ovX(ett.x), ovY(ett.y)), kGrön) &&
              ärFärg(b2.at(ovX(två.x), ovY(två.y)), kBlå),
          "E9 delen börjar vid instans 1: instans 0 ritas inte, 1 och 2 där de "
          "ska",
          px(b2.at(ovX(noll.x), ovY(noll.y))) + " / " +
              px(b2.at(ovX(ett.x), ovY(ett.y))));

    // FÄRGEN MED ALFA 0 är basens (stilens) — instansen tonar inte.
    {
        const dt::MeshInstance ingen = instans({0, 0, 0}, 0.0, {1, 1, 1},
                                               gfx::Color{0, 0, 0, 0});
        auto ib = r.createBuffer(gfx::BufferKind::Vertex, sizeof ingen, &ingen);
        dt::MeshUniforms uy;
        dt::setColor(uy, gfx::Color{255, 200, 0, 255});
        Anrop ay     = a;
        ay.inst      = ib.get();
        ay.instAntal = 1;
        auto ty      = målMed(f, [&](gfx::Renderer& rr) { rita(rr, stav, uy, ay); });
        if (g_ritfel)
            return;
        const Bild by = läs(r, *ty, "E9 alfa 0");
        check(ärFärg(by.at(ovX(3.0), ovY(0.0)), gfx::Color{255, 200, 0, 255}),
              "E9 en instansfärg med alfa 0 lämnar basens färg",
              px(by.at(ovX(3.0), ovY(0.0))));
    }

    // INSTANSENS NORMAL: en lutad yta (normalen (0, −1, 1)/√2) vriden −30°
    // kring x, och samma yta sträckt × 2 längs z (invers-transponatet: (0, −2,
    // 1)/√5). Ljuset L = (0, −0,6, 0,8).
    {
        dt::MeshVariant nv;
        nv.instanced = true;
        nv.normals   = true;
        const Gpu   lutad = ladda(r, nv, kvadrat({0, 0, 0}, {2, 0, 0}, {0, 2, 2}));
        const double g = -30.0 * 3.14159265358979323846 / 180.0;
        dt::MeshInstance j0, j1;
        const float      r0[12] = {1, 0, 0, -8,
                                   0, static_cast<float>(std::cos(g)),
                                   static_cast<float>(-std::sin(g)), 0,
                                   0, static_cast<float>(std::sin(g)),
                                   static_cast<float>(std::cos(g)), 0};
        const float      r1[12] = {1, 0, 0, 8, 0, 1, 0, 0, 0, 0, 2, 0};
        std::copy(std::begin(r0), std::end(r0), std::begin(j0.rows));
        std::copy(std::begin(r1), std::end(r1), std::begin(j1.rows));
        const dt::MeshInstance par[2] = {j0, j1};
        auto pb = r.createBuffer(gfx::BufferKind::Vertex, sizeof par, par);
        dt::MeshUniforms un;
        const gfx::Color grå{200, 200, 200, 255};
        dt::setColor(un, grå);
        const V3 L = enhet({0, -0.6, 0.8});
        dt::setLight(un, vec(V3{} - L), 0.0f, 1.0f, 1.0f, false);
        Anrop an;
        an.frame     = &fr;
        an.state     = läge();
        an.inst      = pb.get();
        an.instAntal = 2;
        auto tn      = målMed(f, [&](gfx::Renderer& rr) { rita(rr, lutad, un, an); });
        if (g_ritfel)
            return;
        const Bild bn = läs(r, *tn, "E9 normalen");
        const V3   n0 = enhet({0, std::cos(g) * -1.0 - std::sin(g) * 1.0,
                               std::sin(g) * -1.0 + std::cos(g) * 1.0});
        const V3   n1 = enhet({0, -2, 1});
        const gfx::Color v0 = belyst(grå, intensitet(0, 1, 1, dot(n0, L)));
        const gfx::Color v1 = belyst(grå, intensitet(0, 1, 1, dot(n1, L)));
        check(ärFärg(bn.at(ovX(-8), ovY(0)), v0),
              "E9 instansens normal följer dess ROTATION",
              px(bn.at(ovX(-8), ovY(0))) + " väntat " + färg(v0));
        check(ärFärg(bn.at(ovX(8), ovY(0)), v1),
              "E9 instansens normal genom dess INVERS-TRANSPONAT (olikformig "
              "skala)",
              px(bn.at(ovX(8), ovY(0))) + " väntat " + färg(v1));
    }
}

// -------------------------------------------------------------------- E10
//
// SCREEN_SIZED: en sfär med diametern 1 (lokalt) och storleken 4 mm vid 4
// px/mm ska vara 16 px bred både på djupet 3 och 40; 10 px med uiScale 1,5 är
// 15 px. Diametern mäts ur YTAN — 2·√(pixlar/π) — och inte ur en rad, vars
// pixelantal avrundas en hel px vid kanten (en skiva om 15 px kring ett
// pixelhörn har 14 pixelcentrum i mittraden). UV-sfärens kontur är en
// polygon: ytan är ≈ 1 % mindre än cirkelns, en halv procent i diametern.
double diameter(const Bild& b, gfx::Color c)
{
    int n = 0;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            n += ärFärg(b.at(x, y), c) ? 1 : 0;
    return 2.0 * std::sqrt(n / 3.14159265358979323846);
}

void fallE10(Fönster& f)
{
    gfx::Renderer&    r = f.window().renderer();
    gfx::MeshGeometry geo;
    gfx::sphere(gfx::Vec3d{0, 0, 0}, 0.5, geo);
    std::vector<dt::MeshVertex> verts;
    for (const gfx::Vec3d& p : geo.positions) {
        dt::MeshVertex m;
        m.position = f3({p.x, p.y, p.z});
        verts.push_back(m);
    }
    dt::MeshVariant mv;
    mv.screenSized = true;
    const Gpu sfär = ladda(r, mv, verts, geo.indices);
    const gfx::Color gul{240, 200, 20, 255};

    struct Prov {
        double     djup;
        float      storlek;
        glaze::Unit enhet;
        float      pxPerMm, uiScale;
        double     väntat;   // px
    };
    const Prov prov[] = {{3.0, 4.0f, glaze::Unit::Mm, 4.0f, 1.0f, 16.0},
                         {40.0, 4.0f, glaze::Unit::Mm, 4.0f, 1.0f, 16.0},
                         {3.0, 10.0f, glaze::Unit::Px, 4.0f, 1.5f, 15.0},
                         {40.0, 10.0f, glaze::Unit::Px, 4.0f, 1.5f, 15.0}};
    for (const Prov& p : prov) {
        const gfx::FrameUniforms fr = ram(hålkamera(), {}, p.pxPerMm, p.uiScale);
        const float rader[12] = {1, 0, 0, 0, 0, 1, 0, static_cast<float>(p.djup),
                                 0, 0, 1, 0};
        dt::MeshUniforms u;
        dt::setColor(u, gul);
        dt::setModel(u, rader);
        dt::setScreenSize(u, p.storlek, p.enhet);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) { rita(rr, sfär, u, a); });
        if (g_ritfel)
            return;
        const Bild   b = läs(r, *t, "E10");
        const double d = diameter(b, gul);
        check(std::abs(d - p.väntat) <= 1.0,
              "E10 SCREEN_SIZED " + tal(p.storlek) +
                  (p.enhet == glaze::Unit::Mm ? " mm" : " px") + " på djupet " +
                  tal(p.djup) + " är " + tal(p.väntat) + " px (±1)",
              tal(d) + " px");
    }
}

// -------------------------------------------------------------------- E11
//
// DRAPED (RESERVERAD): en grå yta på
// z = 0 över lådan [−8, 8]², ovanifrån, draperad med en 2 × 2-bild — röd,
// grön / blå, GENOMSKINLIG. Bildens rad 0 ligger vid lådans NORRA kant, och
// där draperingen är genomskinlig syns ytans egen färg. Samma med ett
// render-måls bild (premultiplicerad, på GL speglad).
void fallE11(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(ovanifrån());
    dt::MeshVariant          mv;
    mv.draped      = true;
    const Gpu      yta = ladda(r, mv, kvadrat({0, 0, 0}, {8, 0, 0}, {0, 8, 0}));
    const gfx::Color grå{50, 50, 50, 255};
    auto rak = bild(r, 2, 2, {kRöd, kGrön, kBlå, gfx::Color{0, 0, 0, 0}},
                    gfx::ImageFilter::Nearest);
    auto mönster = mål(r, 2, 2, false);
    iMål(f, [&](gfx::Renderer& rr) {
        rr.pushTarget(*mönster, gfx::Color{0, 0, 0, 0});
        rr.fillRect(gfx::Rectf{0, 0, 1, 1}, kRöd);
        rr.fillRect(gfx::Rectf{1, 0, 1, 1}, kGrön);
        rr.fillRect(gfx::Rectf{0, 1, 1, 1}, kBlå);
        rr.popTarget();
    });
    if (g_ritfel)
        return;
    for (int vilken = 0; vilken < 2; ++vilken) {
        const gfx::Image& img = vilken == 0 ? *rak : mönster->image();
        dt::MeshUniforms  u;
        dt::setColor(u, grå);
        dt::setDrape(u, img, -8.0, -8.0, 8.0, 8.0);
        Anrop a;
        a.frame  = &fr;
        a.state  = läge();
        a.tex[dt::meshSlots(mv).drape] = &img;
        auto t = målMed(f, [&](gfx::Renderer& rr) { rita(rr, yta, u, a); });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "E11");
        check(ärFärg(b.at(ovX(-4), ovY(4)), kRöd) &&
                  ärFärg(b.at(ovX(4), ovY(4)), kGrön) &&
                  ärFärg(b.at(ovX(-4), ovY(-4)), kBlå) &&
                  ärFärg(b.at(ovX(4), ovY(-4)), grå),
              vilken == 0 ? "E11 DRAPED: bildens rad 0 vid lådans norra kant, "
                            "ytans färg under det genomskinliga"
                          : "E11 DRAPED med ett render-måls bild "
                            "(premultiplicerad, på GL speglad)",
              px(b.at(ovX(-4), ovY(4))) + " / " + px(b.at(ovX(4), ovY(4))) +
                  " / " + px(b.at(ovX(-4), ovY(-4))) + " / " +
                  px(b.at(ovX(4), ovY(-4))));
    }
}

// ------------------------------------------------------------------ punkter

// Punkterna i instansströmmen, ritade med Points-programmet.
struct Punkter {
    dt::PointVariant                variant;
    gfx::VertexLayout               layout;
    std::unique_ptr<gfx::GpuBuffer> buf;
    std::uint32_t                   antal = 0;
};

Punkter punkter(gfx::Renderer& r, const dt::PointVariant& v,
                const std::vector<dt::PointVertex>& in)
{
    Punkter p;
    p.variant         = v;
    p.layout          = dt::pointLayout(v);
    const auto stride = p.layout.stride[1];
    std::vector<std::uint8_t> bytes(in.size() * stride);
    for (std::size_t i = 0; i < in.size(); ++i)
        dt::packPoint(v, in[i], bytes.data() + i * stride);
    p.buf   = r.createBuffer(gfx::BufferKind::Vertex, bytes.size(), bytes.data());
    p.antal = static_cast<std::uint32_t>(in.size());
    return p;
}

void ritaPunkter(gfx::Renderer& r, const Punkter& p, const dt::PointUniforms& u,
                 const gfx::FrameUniforms& fr, const gfx::RenderState& st,
                 const gfx::Image* ramp = nullptr)
{
    const gfx::ShaderProgram* prog = g_prog->pointP(r, p.variant);
    if (!prog || !p.buf)
        return;
    gfx::MeshDraw d;
    d.program       = gfx::MeshProgram::Custom;
    d.shader        = prog;
    d.layout        = &p.layout;
    d.vertexCount   = dt::kPointQuadVertices;
    d.instances     = gfx::slice(*p.buf);
    d.instanceCount = p.antal;
    d.frame         = fr;
    d.user          = &u;
    d.userBytes     = sizeof u;
    d.textures[0]   = ramp;
    d.state         = st;
    r.drawMesh(d);
}

dt::PointVertex punkt(V3 p, gfx::Color c = kVit, float s = 0.0f)
{
    dt::PointVertex v;
    v.position = f3(p);
    v.color    = c;
    v.scalar   = s;
    return v;
}

// Täckningen (röd kanal / 255, vit på svart) summerad i en ruta kring (cx, cy).
double yta(const Bild& b, int cx, int cy, int halv = 8)
{
    double s = 0.0;
    for (int y = cy - halv; y < cy + halv; ++y)
        for (int x = cx - halv; x < cx + halv; ++x)
            s += b.at(x, y)[0] / 255.0;
    return s;
}

// -------------------------------------------------------- F1, F2, F6, F7
//
// Tre punkter i vyn ovanifrån på hela världsenheter — mitten på ett
// pixelhörn — vita på svart: ytan är täckningen summerad.
void fallFStorlek(Fönster& f)
{
    gfx::Renderer& r = f.window().renderer();
    const std::vector<dt::PointVertex> tre = {punkt({-8, 0, 0}), punkt({0, 0, 0}),
                                              punkt({8, 0, 0})};
    const Punkter p = punkter(r, dt::PointVariant{}, tre);
    struct Prov {
        const char*      namn;
        float            storlek;
        glaze::Unit      enhet;
        gfx::MarkerShape form;
        float            pxPerMm;
        double           väntat;   // px²
    };
    const double kPi = 3.14159265358979323846;
    const Prov prov[] = {
        {"F1 en kvadratisk punkt om 6 px täcker 36 px²", 6.0f, glaze::Unit::Px,
         gfx::MarkerShape::Square, 0.0f, 36.0},
        {"F2 en rund punkt om 6 px täcker π·3² px²", 6.0f, glaze::Unit::Px,
         gfx::MarkerShape::Circle, 0.0f, kPi * 9.0},
        {"F6 storleken i mm × pxPerMm: 1,5 mm vid 4 px/mm = 36 px²", 1.5f,
         glaze::Unit::Mm, gfx::MarkerShape::Square, 4.0f, 36.0},
    };
    for (const Prov& pr : prov) {
        const gfx::FrameUniforms fr = ram(ovanifrån(), {}, pr.pxPerMm, 1.0f);
        dt::PointUniforms u;
        dt::setPointSize(u, pr.storlek, pr.enhet, pr.form);
        auto t = målMed(f, [&](gfx::Renderer& rr) {
            ritaPunkter(rr, p, u, fr, läge());
        });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, pr.namn);
        std::string ytor;
        bool        ok = true;
        for (double x : {-8.0, 0.0, 8.0}) {
            const double a = yta(b, ovX(x), ovY(0.0));
            ok             = ok && std::abs(a - pr.väntat) <= 1.5;
            ytor += tal(a) + " ";
        }
        check(ok, std::string(pr.namn) + " (±1,5)",
              ytor + "väntat " + tal(pr.väntat));
    }

    // F7 VÄRLDEN: 0,5 världsenheter på djupet 10 i hålkameran = 6,4 px.
    {
        const Punkter en = punkter(r, dt::PointVariant{}, {punkt({0, 10, 0})});
        const gfx::FrameUniforms fr = ram(hålkamera());
        dt::PointUniforms u;
        dt::setPointSize(u, 0.5f, glaze::Unit::World, gfx::MarkerShape::Square);
        auto t = målMed(f, [&](gfx::Renderer& rr) {
            ritaPunkter(rr, en, u, fr, läge());
        });
        if (g_ritfel)
            return;
        const Bild   b      = läs(r, *t, "F7");
        const double sida   = 0.5 * kBränn / 10.0;
        const double a      = yta(b, kSida / 2, kSida / 2);
        check(std::abs(a - sida * sida) <= 1.5,
              "F7 storleken i världen genom djupet (0,5 på djupet 10 = 6,4 px)",
              tal(a) + " väntat " + tal(sida * sida));
    }
}

// ----------------------------------------------------------- F3, F4, F5, F8
void fallFFärg(Fönster& f)
{
    gfx::Renderer&           r  = f.window().renderer();
    const gfx::FrameUniforms fr = ram(ovanifrån());

    // F3 RAMPEN: skalärerna 10, 20 och 30 mot rampen 10–30, och en ramp om
    // tre texlar med skalären 14 (t = 0,2 → 60 % blå, 40 % grön).
    {
        dt::PointVariant pv;
        pv.scalars      = true;
        const Punkter p = punkter(r, pv,
                                  {punkt({-8, 0, 0}, kVit, 10.0f),
                                   punkt({0, 0, 0}, kVit, 20.0f),
                                   punkt({8, 0, 0}, kVit, 30.0f)});
        auto rampen = ramp256(r, kTreStopp);
        auto tre    = bild(r, 3, 1, {kBlå, kGrön, kRöd}, gfx::ImageFilter::Linear);
        const Punkter fjorton = punkter(r, pv, {punkt({0, 8, 0}, kVit, 14.0f)});
        dt::PointUniforms u, u3;
        dt::setPointSize(u, 8.0f, glaze::Unit::Px, gfx::MarkerShape::Square);
        dt::setRamp(u, 10.0, 30.0, *rampen);
        u3 = u;
        dt::setRamp(u3, 10.0, 30.0, *tre);
        auto t = målMed(f, [&](gfx::Renderer& rr) {
            ritaPunkter(rr, p, u, fr, läge(), rampen.get());
            ritaPunkter(rr, fjorton, u3, fr, läge(), tre.get());
        });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "F3");
        check(ärFärg(b.at(ovX(-8), ovY(0)), kBlå) &&
                  ärFärg(b.at(ovX(0), ovY(0)), kGrön) &&
                  ärFärg(b.at(ovX(8), ovY(0)), kRöd),
              "F3 rampens min, mitt och max: blå, grön, röd",
              px(b.at(ovX(-8), ovY(0))) + " / " + px(b.at(ovX(0), ovY(0))) +
                  " / " + px(b.at(ovX(8), ovY(0))));
        const gfx::Color väntat = blanda(kBlå, kGrön, 0.4);
        check(ärFärg(b.at(ovX(0), ovY(8)), väntat),
              "F3 en ramp om tre texlar linjärt mellan texlarna",
              px(b.at(ovX(0), ovY(8))) + " väntat " + färg(väntat));
    }

    // F4 DJUPET: en grå yta på z = 0 (Less + skrivning), en punkt ovanför
    // (z = 1, närmare ögat på z = 10) och en under (z = −1).
    {
        const Gpu golv = ladda(r, dt::MeshVariant{},
                               kvadrat({0, 0, 0}, {12, 0, 0}, {0, 12, 0}));
        dt::MeshUniforms ug;
        dt::setColor(ug, gfx::Color{100, 100, 100, 255});
        const Punkter p = punkter(r, dt::PointVariant{},
                                  {punkt({-4, 0, 1}), punkt({4, 0, -1})});
        dt::PointUniforms u;
        dt::setPointSize(u, 8.0f, glaze::Unit::Px, gfx::MarkerShape::Square);
        Anrop a;
        a.frame = &fr;
        a.state = läge();
        auto t  = målMed(f, [&](gfx::Renderer& rr) {
            rita(rr, golv, ug, a);
            ritaPunkter(rr, p, u, fr, läge());
        });
        if (g_ritfel)
            return;
        const Bild b = läs(r, *t, "F4");
        check(ärFärg(b.at(ovX(-4), ovY(0)), kVit) &&
                  ärFärg(b.at(ovX(4), ovY(0)), gfx::Color{100, 100, 100, 255}),
              "F4 djuptestet mot en mesh: punkten framför syns, den bakom inte",
              px(b.at(ovX(-4), ovY(0))) + " / " + px(b.at(ovX(4), ovY(0))));
    }

    // F5 FÄRGEN PER PUNKT, och F8 Cull::Back släpper igenom punkterna.
    {
        dt::PointVariant pv;
        pv.colors       = true;
        const Punkter p = punkter(r, pv,
                                  {punkt({-8, 0, 0}, kRöd), punkt({0, 0, 0}, kGrön),
                                   punkt({8, 0, 0}, kBlå)});
        dt::PointUniforms u;
        dt::setPointSize(u, 8.0f, glaze::Unit::Px, gfx::MarkerShape::Circle);
        for (gfx::Cull c : {gfx::Cull::None, gfx::Cull::Back}) {
            auto t = målMed(f, [&](gfx::Renderer& rr) {
                ritaPunkter(rr, p, u, fr, läge(gfx::DepthTest::Less, c));
            });
            if (g_ritfel)
                return;
            const Bild b = läs(r, *t, "F5");
            check(ärFärg(b.at(ovX(-8), ovY(0)), kRöd) &&
                      ärFärg(b.at(ovX(0), ovY(0)), kGrön) &&
                      ärFärg(b.at(ovX(8), ovY(0)), kBlå),
                  c == gfx::Cull::None
                      ? "F5 färgen per punkt"
                      : "F8 punkterna överlever Cull::Back (quaden är moturs)",
                  px(b.at(ovX(-8), ovY(0))) + " / " + px(b.at(ovX(0), ovY(0))) +
                      " / " + px(b.at(ovX(8), ovY(0))));
        }
    }

    // F8 BAKOM KAMERAN och BORTOM DET BORTRE PLANET: inget ritas.
    {
        const Punkter p = punkter(r, dt::PointVariant{},
                                  {punkt({0, -5, 0}), punkt({0.5, 150, 0})});
        const gfx::FrameUniforms fh = ram(hålkamera());
        dt::PointUniforms        u;
        dt::setPointSize(u, 20.0f, glaze::Unit::Px, gfx::MarkerShape::Square);
        for (gfx::DepthTest d : {gfx::DepthTest::Less, gfx::DepthTest::Off}) {
            auto t = målMed(f, [&](gfx::Renderer& rr) {
                ritaPunkter(rr, p, u, fh, läge(d));
            });
            if (g_ritfel)
                return;
            const Bild b = läs(r, *t, "F8");
            int        ritade = 0;
            for (int y = 0; y < b.h; ++y)
                for (int x = 0; x < b.w; ++x)
                    ritade += ärFärg(b.at(x, y), kSvart, 0) ? 0 : 1;
            check(ritade == 0,
                  std::string("F8 en punkt bakom kameran och en bortom det "
                              "bortre planet ritas inte") +
                      (d == gfx::DepthTest::Off ? " (utan djup)" : " (Less)"),
                  std::to_string(ritade) + " ritade px");
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== mesh_program_gate — de inbyggda Mesh- och Points-"
                "programmen i pixlar ===\n");

    Fönster f;
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== mesh_program_gate: FALLERAR ===\n");
        return 1;   // ALDRIG skip — se filhuvudet
    }
    iMål(f, nullptr);   // första framen — fönstret ritat en gång
    {
        Program prog;   // rivs före fönstret (programmen hör till renderaren)
        g_prog = &prog;
        if (!g_ritfel) fallV1(f);
        if (!g_ritfel) fallE1(f);
        if (!g_ritfel) fallE2(f);
        if (!g_ritfel) fallE3(f);
        if (!g_ritfel) fallE4(f);
        if (!g_ritfel) fallE5(f);
        if (!g_ritfel) fallE6(f);
        if (!g_ritfel) fallE7(f);
        if (!g_ritfel) fallE8(f);
        if (!g_ritfel) fallE9(f);
        if (!g_ritfel) fallE10(f);
        if (!g_ritfel) fallE11(f);
        if (!g_ritfel) fallFStorlek(f);
        if (!g_ritfel) fallFFärg(f);
        g_prog = nullptr;
    }

    std::printf(g_fel ? "=== mesh_program_gate: %d FEL av %d ===\n"
                      : "=== mesh_program_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
