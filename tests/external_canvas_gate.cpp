// external_canvas_gate — RITYTANS SCEN I EXTERNA MÅL MED EN UTTRYCKLIG VY
// (`detail::ExternalCanvasPass`, glaze/canvas/external_pass.h): ett headsets
// ögon. Passet ritar huvudvyns lager i varje mål med målets egen
// `gfx::View3D`, båda målen i ETT offscreen-block, utan att röra skrivbordets
// kamera, mål och cache.
//
// VÄGEN är scene3d_pixel_gates: ett DOLT fönster (`setOffscreen`) med en
// `glaze::Canvas` i 3D, och målen är vanliga `createRenderTarget`-mål som
// grinden själv läser med `readTarget`. Scenen är en röd kvadrat (en mesh utan
// ljus) i planet z = 0, sedd uppifrån. Förväntat värde är grindens EGNA
// pinhole-räkning (blick × upp = höger, tangenter från ytans kanter) och
// `renderToBitmap` för det symmetriska fallet.
//
// VAD GRINDEN PÅSTÅR:
//   S1  en symmetrisk vy (synfältet `fovY`, och samma synfält som ett
//       explicit frustum) ritar samma bild som `Canvas::renderToBitmap` med
//       motsvarande kamera (högst 0,5 % av pixlarna skiljer mer än 24 nivåer),
//       och kvadraten ligger där grindens egen projektion lägger den.
//   S2  ett ASYMMETRISKT frustum per mål, båda ögonen i ett anrop: kvadratens
//       kanter hamnar där den geometriska referensen säger (±4 px) i bägge
//       målen — och inte där den symmetriska vyn hade lagt dem (kontrollen).
//   S3  skrivbordet orört: kamerans fält, ritytans 3D-mål ritades inte om av
//       passet, och skrivbordets bild efter passet är byte-lika med den före.
//   S4  `castRay` med en uttrycklig vy: SAMMA världsstråle ger olika svar i
//       olika vyer mot en skärmkonstant mesh (storleken i px, alltså i världen
//       px × världen per pixel i vyn): den träffar i den breda vyn och missar i
//       den smala, och en förskjuten symmetrisk vy svarar som den symmetriska.
//   S5  felen: inga mål, ett ogiltigt mål, en ruta som inte är hela målet och
//       en oanvändbar vy ger false + text, och passet ritar sedan som förut.
//   S6  ett andra anrop med samma vyer ger samma bild (resursbudgeten
//       förnyas per anrop utan att målens resurser slängs).
//
// INTE MÄTT här: etiketter, terräng och `onPaint` i målen (de delar
// `renderView` med skärmen och bilden, som deras grindar mäter), importerade
// swapchain-bilder (external_target_gate) och ett headset. Saknas display
// (inget dolt fönster går att öppna) hoppas grinden över (77).
//
// SABOTAGEN som ska fälla grinden: vyns `frustum` ignorerad i passet (S2),
// skrivbordets kamera använd i stället för vyn (S1/S2), skrivbordets mål
// ritat om av passet (S3), `castRay` mot fönstrets vy (S4), en `trim` mellan
// målen som släpper det första målets resurser (S2 — vänster mål tomt eller
// fel), två anrop som ger olika bilder (S6).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <gfx/renderer.h>
#include <glaze/canvas/external_pass.h>
#include <glaze/glaze.h>

#include "canvas/resources.h"   // intern — 3D-målets räknare (S3)

namespace {

int  g_fel  = 0;
int  g_fall = 0;
bool g_verbose = false;

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

// Grindens färger, mått och mätpunkter.
const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kRöd{255, 0, 0, 255};

constexpr int    kB = 256;   // målens bredd
constexpr int    kH = 192;   // och höjd
constexpr double kAvstånd = 10.0;   // ögat står 10 enheter över planet z = 0
// Kvadraten i planet z = 0: x ∈ [1, 3], y ∈ [−2, 0,5].
constexpr double kX0 = 1.0, kX1 = 3.0, kY0 = -2.0, kY1 = 0.5;
// Den skärmkonstanta märkeskvadratens mitt i planet z = 0 (S4), på avstånd
// från den röda kvadraten.
constexpr double kMärkeX = -5.0;

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    // Utanför bilden svarar den med en pixel ingen förväntan matchar.
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

class Fönster : public glaze::AppWindow {
public:
    Fönster()
    {
        setTitle("external_canvas_gate");
        setClientSize(360, 300);
        yta.name   = "yta";
        yta.x      = 12;
        yta.y      = 12;
        yta.width  = 300;
        yta.height = 220;
        yta.setBackground(kVit);
        yta.setBorder(false);
        root().adopt(yta);
    }
    glaze::Canvas yta;
};

// Skrivbordets bild: ritytan invalideras och fönstret fångas.
Bild skrivbord(Fönster& f)
{
    Bild b;
    f.yta.invalidate();
    f.window().renderFrame();
    f.window().captureFrame(b.px, b.w, b.h);
    return b;
}

Bild läs(gfx::Renderer& r, gfx::RenderTarget& t)
{
    Bild b;
    b.w = kB;
    b.h = kH;
    if (!r.readTarget(t, b.px))
        b.px.clear();
    return b;
}

// ---------------------------------------------------- grindens egen geometri

// Vyn från ögat rakt ned (blick −z, upp +y, höger +x), med 45° vertikalt
// synfält eller ett explicit frustum.
gfx::View3D nedåtVy()
{
    gfx::View3D v;
    v.eye     = {0.0, 0.0, kAvstånd};
    v.forward = {0.0, 0.0, -1.0};
    v.up      = {0.0, 1.0, 0.0};
    v.fovY    = 45.0 * std::numbers::pi / 180.0;
    v.nearZ   = 1.0;
    v.farZ    = 100.0;
    v.box     = gfx::Rectf{0.0f, 0.0f, static_cast<float>(kB),
                       static_cast<float>(kH)};
    return v;
}

gfx::View3D frustumVy(double l, double r, double d, double u)
{
    gfx::View3D v = nedåtVy();
    v.frustum     = gfx::FrustumTangents{l, r, d, u};
    return v;
}

// Tangenterna ur 45° och bildförhållandet — det symmetriska frustumet.
gfx::FrustumTangents symmetriska()
{
    const double ty = std::tan(45.0 * std::numbers::pi / 180.0 / 2.0);
    const double tx = ty * kB / kH;
    return gfx::FrustumTangents{-tx, tx, -ty, ty};
}

// Pinhole-projektionen av en punkt i planet z = 0 för en vy som ser rakt ned:
// bildens x ur tangenten åt höger, y ur tangenten uppåt (rad 0 överst).
void projicera(const gfx::FrustumTangents& t, double wx, double wy, double& sx,
               double& sy)
{
    const double tx = wx / kAvstånd, ty = wy / kAvstånd;
    sx = kB * (tx - t.left) / (t.right - t.left);
    sy = kH * (t.up - ty) / (t.up - t.down);
}

// Strålen genom bildpunkten (sx, sy) i samma vy.
glaze::Ray3 stråle(const gfx::FrustumTangents& t, double sx, double sy)
{
    const double tx = t.left + (t.right - t.left) * sx / kB;
    const double ty = t.up - (t.up - t.down) * sy / kH;
    return glaze::Ray3{{0.0, 0.0, kAvstånd}, {tx, ty, -1.0}};
}

// Kvadratens kanter i bilden för en vy, och kontrollen att de stämmer: mitten
// röd, och fyra pixlar inom respektive utanför varje kant (så långt de ryms).
void kontrolleraKvadraten(const Bild& b, const gfx::FrustumTangents& t,
                          const std::string& namn)
{
    double x0 = 0, y1 = 0, x1 = 0, y0 = 0;
    projicera(t, kX0, kY1, x0, y0);   // övre vänstra (y uppåt → rad nedåt)
    projicera(t, kX1, kY0, x1, y1);   // nedre högra
    const int cx = static_cast<int>(std::clamp((x0 + x1) / 2.0, 0.0, kB - 1.0));
    const int cy = static_cast<int>((y0 + y1) / 2.0);
    check(ärFärg(b.at(cx, cy), kRöd), namn + ": kvadratens mitt är röd",
          px(b.at(cx, cy)));
    const auto inne = [&](int x, int y, const char* kant) {
        check(ärFärg(b.at(x, y), kRöd), namn + ": inom " + kant + "kanten är röd",
              px(b.at(x, y)));
    };
    const auto ute = [&](int x, int y, const char* kant) {
        if (x < 0 || y < 0 || x >= kB || y >= kH)
            return;   // utanför målet: inget att mäta
        check(ärFärg(b.at(x, y), kVit), namn + ": utanför " + kant + "kanten är vit",
              px(b.at(x, y)));
    };
    const int ix0 = static_cast<int>(std::ceil(x0)) + 4;
    const int ix1 = static_cast<int>(std::floor(x1)) - 4;
    const int iy0 = static_cast<int>(std::ceil(y0)) + 4;
    const int iy1 = static_cast<int>(std::floor(y1)) - 4;
    if (x0 >= 0.0) {   // vänsterkanten syns
        inne(ix0, cy, "vänster");
        ute(ix0 - 8, cy, "vänster");
    }
    if (x1 < kB) {   // högerkanten syns
        inne(ix1, cy, "höger");
        ute(ix1 + 8, cy, "höger");
    }
    inne(cx, iy0, "övre");
    ute(cx, iy0 - 8, "övre");
    inne(cx, iy1, "nedre");
    ute(cx, iy1 + 8, "nedre");
}

// Andelen pixlar som skiljer mer än `tol` nivåer i någon kanal.
double skillnad(const Bild& a, const glaze::Bitmap& b, int tol)
{
    if (!b.valid() || b.width() != a.w || b.height() != a.h ||
        a.px.size() < static_cast<std::size_t>(a.w) * a.h * 4)
        return 1.0;
    const std::uint8_t* q = b.pixels();
    if (!q)
        return 1.0;
    std::size_t olika = 0;
    for (int i = 0; i < a.w * a.h; ++i)
        for (int k = 0; k < 4; ++k)
            if (std::abs(a.px[static_cast<std::size_t>(i) * 4 + k] -
                         q[static_cast<std::size_t>(i) * 4 + k]) > tol) {
                ++olika;
                break;
            }
    return static_cast<double>(olika) / (static_cast<double>(a.w) * a.h);
}

bool lika(const Bild& a, const Bild& b)
{
    return !a.px.empty() && a.w == b.w && a.h == b.h && a.px == b.px;
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== external_canvas_gate — ritytans scen i externa mål med en "
                "uttrycklig vy ===\n");

    Fönster f;
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  SKIP fönstret öppnades inte — ingen display\n");
        return 77;
    }
    f.window().renderFrame();

    // Skrivbordets kamera ser scenen snett från ett annat håll än målens vyer.
    glaze::Canvas& y = f.yta;
    y.setCamera(glaze::Camera3D{.target = {5, 5, 0}, .distance = 25,
                                .elevation = 50, .nearPlane = 1,
                                .farPlane = 200});
    glaze::MeshStyle ljusfri;
    ljusfri.lit = false;
    ljusfri.fog = false;
    y.addShape(0, glaze::Shape::mesh(
                      glaze::Mesh::triangles(
                          {{kX0, kY0, 0}, {kX1, kY0, 0}, {kX1, kY1, 0},
                           {kX0, kY0, 0}, {kX1, kY1, 0}, {kX0, kY1, 0}},
                          kRöd),
                      {}, ljusfri));

    std::string                                          fel;
    std::unique_ptr<glaze::detail::ExternalCanvasPass>   pass =
        glaze::detail::ExternalCanvasPass::create(y, fel);
    check(pass != nullptr, "S0 passet skapas för en rityta i ett öppet fönster",
          fel);
    if (!pass) {
        std::printf("=== external_canvas_gate: %d FEL av %d ===\n", g_fel, g_fall);
        return 1;
    }
    gfx::Renderer&   r = pass->renderer();
    gfx::TargetDesc  d;
    d.samples = r.supportedSamples(4);
    d.depth   = true;
    std::unique_ptr<gfx::RenderTarget> vänster = r.createRenderTarget(kB, kH, d);
    std::unique_ptr<gfx::RenderTarget> höger   = r.createRenderTarget(kB, kH, d);
    check(vänster->image().valid() && höger->image().valid(),
          "S0 två mål med djup går att skapa");

    // ------------------------------------------------------------------ S3 före
    const Bild         före      = skrivbord(f);
    const glaze::Camera3D kamera0 = y.camera3D();
    const std::uint64_t ritat0   = f.yta.behaviorCtx().canvas->targetRenders();

    // --------------------------------------------------------------- S1
    // Vänster: synfältet; höger: samma synfält som ett explicit frustum.
    {
        const gfx::FrustumTangents sym = symmetriska();
        gfx::View3D                a = nedåtVy();
        gfx::View3D                b = frustumVy(sym.left, sym.right, sym.down, sym.up);
        const glaze::detail::ExternalCanvasView vyer[] = {
            {vänster.get(), a}, {höger.get(), b}};
        const bool ok = pass->render(vyer, fel);
        check(ok, "S1 passet ritar två mål", fel);
        // S3: ingenting av skrivbordets 3D-mål ritades om av passet.
        check(f.yta.behaviorCtx().canvas->targetRenders() == ritat0,
              "S3 passet ritar inte om ritytans 3D-mål");

        glaze::Camera3D kam{.target = {0, 0, 0}, .distance = kAvstånd,
                            .elevation = 90, .nearPlane = 1, .farPlane = 100};
        const std::optional<glaze::Bitmap> bild = y.renderToBitmap(kB, kH, kam);
        check(bild.has_value(), "S1 renderToBitmap ger referensbilden");
        const Bild l = läs(r, *vänster), h = läs(r, *höger);
        if (bild) {
            const double sl = skillnad(l, *bild, 24), sh = skillnad(h, *bild, 24);
            check(sl <= 0.005,
                  "S1 synfältsvyn ritar samma bild som renderToBitmap",
                  std::to_string(sl * 100.0) + " % av pixlarna skiljer");
            check(sh <= 0.005,
                  "S1 ett explicit symmetriskt frustum ritar samma bild som "
                  "renderToBitmap",
                  std::to_string(sh * 100.0) + " % av pixlarna skiljer");
        }
        kontrolleraKvadraten(l, sym, "S1 vänster");
        kontrolleraKvadraten(h, sym, "S1 höger");
    }

    // --------------------------------------------------------------- S2
    // Två olika asymmetriska frustum, båda ögonen i ETT anrop.
    const gfx::FrustumTangents tvä = {-0.2, 0.5, -0.3, 0.1};
    const gfx::FrustumTangents tre = {-0.5, 0.2, -0.3, 0.1};
    Bild                       asymL, asymR;
    {
        const glaze::detail::ExternalCanvasView vyer[] = {
            {vänster.get(), frustumVy(tvä.left, tvä.right, tvä.down, tvä.up)},
            {höger.get(), frustumVy(tre.left, tre.right, tre.down, tre.up)}};
        const bool ok = pass->render(vyer, fel);
        check(ok, "S2 passet ritar två asymmetriska mål i ett anrop", fel);
        asymL = läs(r, *vänster);
        asymR = läs(r, *höger);
        kontrolleraKvadraten(asymL, tvä, "S2 vänster");
        kontrolleraKvadraten(asymR, tre, "S2 höger");
        // Kontrollen: den symmetriska vyn hade lagt kvadratens vänsterkant vid
        // x ≈ 151; vid x = 120 är den vit där och röd i det asymmetriska målet.
        check(ärFärg(asymL.at(120, 84), kRöd),
              "S2 frustumet verkar: kvadraten täcker x = 120 i det "
              "asymmetriska målet",
              px(asymL.at(120, 84)));
    }

    // --------------------------------------------------------------- S3 efter
    {
        const Bild efter = skrivbord(f);
        check(lika(före, efter),
              "S3 skrivbordets bild efter passet är byte-lika med den före");
        const glaze::Camera3D k1 = y.camera3D();
        check(k1.target.x == kamera0.target.x && k1.target.y == kamera0.target.y &&
                  k1.target.z == kamera0.target.z &&
                  k1.distance == kamera0.distance &&
                  k1.elevation == kamera0.elevation &&
                  k1.azimuth == kamera0.azimuth && k1.fov == kamera0.fov,
              "S3 skrivbordets kamera är orörd");
    }

    // --------------------------------------------------------------- S4
    // Världsstrålen är KONSTANT; bara vyn byts. Beviset är en skärmkonstant
    // mesh (`MeshStyle::size` i px): dess storlek i världen är px × världen per
    // pixel i den vy `castRay` får, så strålen träffar den bara i vyer där
    // den är tillräckligt stor. Fel vy (fönstrets kamera, en annan öga) ger fel
    // svar. Meshen är en kvadrat med halva sidan 1 lokal enhet = `kMärkePx`
    // fysiska px, långt från den röda kvadraten.
    {
        constexpr double kMärkePx = 20.0;
        const double     skala    = y.behaviorCtx().scale;   // fysiska px per logisk px
        glaze::MeshStyle märke;
        märke.lit  = false;
        märke.fog  = false;
        märke.size = glaze::px(static_cast<float>(kMärkePx / skala));
        const int lager = y.addLayer("S4 märke");
        y.addShape(lager, glaze::Shape::mesh(
                              glaze::Mesh::triangles(
                                  {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0},
                                   {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}},
                                  kRöd),
                              glaze::Placement{.at = {kMärkeX, 0.0, 0.0}}, märke));

        // Fyra vyer med olika världen per pixel (faktor × den symmetriska):
        // smal asymmetrisk, symmetrisk, symmetrisk men förskjuten, bred
        // asymmetrisk. Världen per fysisk px, ur grindens egen geometri.
        struct Vy {
            const char* namn;
            double      faktor, cx, cy;
        };
        const Vy vyer[] = {{"smal asymmetrisk", 0.5, 0.15, -0.05},
                           {"symmetrisk", 1.0, 0.0, 0.0},
                           {"symmetrisk förskjuten", 1.0, -0.2, 0.1},
                           {"bred asymmetrisk", 2.0, 0.2, 0.1}};
        const double ty = std::tan(45.0 * std::numbers::pi / 180.0 / 2.0);
        // Strålen rakt ned δ världsenheter från märkets mitt: märkets halva
        // sida är kMärkePx × världen per px. δ = 1,2 ryms bara i den breda
        // vyn (halva sidan 1,73), δ = 0,6 i den breda och de symmetriska
        // (0,86) men inte i den smala (0,43).
        struct Stråle {
            double dx;
            bool   träffar[4];
        };
        const Stråle strålar[] = {{1.2, {false, false, false, true}},
                                  {0.6, {false, true, true, true}}};
        for (const Stråle& st : strålar) {
            const glaze::Ray3 ray{{kMärkeX + st.dx, 0.0, kAvstånd}, {0.0, 0.0, -1.0}};
            for (int i = 0; i < 4; ++i) {
                const gfx::FrustumTangents sym = symmetriska();
                const double fk = vyer[i].faktor;
                const gfx::View3D vy = frustumVy(
                    vyer[i].cx + sym.left * fk, vyer[i].cx + sym.right * fk,
                    vyer[i].cy + sym.down * fk, vyer[i].cy + sym.up * fk);
                const std::vector<glaze::HitResult> t =
                    pass->castRay(ray, glaze::HitOptions{}, vy);
                const bool träff = !t.empty() && t.front().layer == lager &&
                                   t.front().kind == glaze::HitKind::Surface &&
                                   t.front().point.has_value();
                const double halv = kMärkePx * fk * 2.0 * ty * kAvstånd / kH;
                check(träff == st.träffar[i],
                      std::string("S4 samma världsstråle (δ ") +
                          std::to_string(st.dx) + ") i vyn \"" + vyer[i].namn +
                          "\" " + (st.träffar[i] ? "träffar" : "missar") +
                          " den skärmkonstanta kvadraten",
                      std::to_string(t.size()) + " träffar, kvadratens halva "
                          "sida " + std::to_string(halv) + " världsenheter");
                if (träff && st.träffar[i]) {
                    const glaze::Point3& p = *t.front().point;
                    check(std::abs(p.x - (kMärkeX + st.dx)) < 0.02 &&
                              std::abs(p.y) < 0.02 && std::abs(p.z) < 0.02,
                          "S4 träffen ligger där strålen når planet",
                          std::to_string(p.x) + "," + std::to_string(p.y) + "," +
                              std::to_string(p.z));
                }
            }
        }
        y.removeLayer(lager);
    }

    // --------------------------------------------------------------- S5
    {
        std::string                    e;
        check(!pass->render(std::span<const glaze::detail::ExternalCanvasView>{}, e) &&
                  !e.empty(),
              "S5 inga mål ger false + text", e);

        e.clear();
        const glaze::detail::ExternalCanvasView utanMål[] = {
            {nullptr, frustumVy(tvä.left, tvä.right, tvä.down, tvä.up)}};
        check(!pass->render(utanMål, e) && !e.empty(),
              "S5 ett ogiltigt mål ger false + text", e);

        e.clear();
        gfx::View3D fel1 = frustumVy(tvä.left, tvä.right, tvä.down, tvä.up);
        fel1.box         = gfx::Rectf{0.0f, 0.0f, 100.0f, 100.0f};
        const glaze::detail::ExternalCanvasView felRuta[] = {{vänster.get(), fel1}};
        check(!pass->render(felRuta, e) && !e.empty(),
              "S5 en ruta som inte är hela målet ger false + text", e);

        e.clear();
        gfx::View3D oanvändbar = nedåtVy();
        oanvändbar.forward     = {0.0, 0.0, 0.0};
        const glaze::detail::ExternalCanvasView felVy[] = {
            {vänster.get(), oanvändbar}};
        check(!pass->render(felVy, e) && !e.empty(),
              "S5 en oanvändbar vy ger false + text", e);

        e.clear();
        gfx::View3D omvänd = frustumVy(0.5, -0.5, -0.3, 0.1);
        const glaze::detail::ExternalCanvasView felFrustum[] = {
            {vänster.get(), omvänd}};
        check(!pass->render(felFrustum, e) && !e.empty(),
              "S5 ett frustum med left > right ger false + text", e);
    }

    // --------------------------------------------------------------- S6
    {
        const glaze::detail::ExternalCanvasView vyer[] = {
            {vänster.get(), frustumVy(tvä.left, tvä.right, tvä.down, tvä.up)},
            {höger.get(), frustumVy(tre.left, tre.right, tre.down, tre.up)}};
        const bool ok = pass->render(vyer, fel);
        check(ok, "S6 passet ritar igen efter felen", fel);
        check(lika(läs(r, *vänster), asymL) && lika(läs(r, *höger), asymR),
              "S6 ett andra anrop med samma vyer ger samma bild");
    }

    pass.reset();   // före ritytan
    std::printf(g_fel ? "=== external_canvas_gate: %d FEL av %d ===\n"
                      : "=== external_canvas_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
