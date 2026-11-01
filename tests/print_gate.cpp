// print_gate — RITYTANS LAGER PÅ EN ANNAN YTA (`Painter::drawCanvas`), i
// pixlar och genom den riktiga backenden.
//
// Utskriftssidan ritas av `PrintJob` i ett offscreen-block med en `Painter` i
// millimeter (src/print.cpp `renderPage`). Grinden gör SAMMA sak utan skrivaren:
// ett offscreen-block, ett mål med sidans upplösning, en `PaintContext` vars
// värld är millimeter, och en målare — och ritar en riktig `glaze::Canvas` på
// den med `drawCanvas`. Sidan läses tillbaka med `readTarget`; det är SIDAN som
// läses, ritytans eget mål läses aldrig (ingen återläsning av ritytan).
// Förväntat värde är geometrin: en röd ruta i världen på kända mått, och var
// den hamnar på sidan i millimeter.
//
// Kör:  ./build/tests/print_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ingen display
//
// VAD GRINDEN PÅSTÅR:
//   P1  TVÅ RUTOR MED OLIKA KAMEROR på samma sida, rätt mm-skala: världen är mm
//       (zoomen = pxPerMm / skalan), den röda rutan 20 × 20 mm ligger i den
//       första rutans mitt och är där 20 × 20 mm på sidan; i den andra rutan
//       (dubbla zoomen) fyller den hela rutan. Utanför rutorna är sidan vit.
//   P2  ROTATIONEN: 180° kring rutans mitt flyttar den röda rutan från
//       övre vänstra till nedre högra kvadranten.
//   P3  DELVIS BORTKLIPPT RUTA: `save(Rect)` över rutans vänstra hälft — där
//       är den röd, i den högra hälften vit.
//   P4  RITYTANS KLIPP (`Canvas::setClips`) gäller sidan: röd bara i klippet.
//   P5  EN RITYTA I ETT ANNAT FÖNSTER än sidans renderare ritas med SIDANS
//       resurser (utskriftens förloppsfönster är ett eget fönster).
//   P6  `false` (+ logg): en rektangel utan yta, en ogiltig kamera (zoom 0, NaN
//       i en 3D-kamera) och ett anrop ur ritytans egen ritning (`onPaint`).
//   Sabotage som SKA fälla dem: `drawTransient` utan bildritningen (P1–P5 —
//   sidan förblir vit), `imageCorners` utan `xf.apply` (P2 — ingen rotation),
//   `clip.apply` bort ur `drawTransient` (P3), `pc.scene`/`lf.clips` bort ur
//   renderCanvasTarget (P4), målet återanvänt av båda anropen i `pageTarget`
//   (P1 — den andra rutan skriver över den första), `PaintingScope` bort ur
//   `drawCanvas` (P6 — onPaint-anropet ritar), måttet i px taget ur rektangelns
//   mm (P1 — fel upplösning).
//   MÄTER INTE skrivaren, PDF-utdata, del-vyer eller rutnätet (ritas inte).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — localView
#include "canvas/scene.h"         // intern — PaintContext för sidans ritläge

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    ++g_fel;
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
}

const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kRöd{255, 0, 0, 255};
const gfx::Color kBlå{0, 0, 255, 255};

// Sidans täthet: 2 px per mm (50,8 dpi) — nog för att läsa mm på pixeln.
constexpr double kPxPerMm = 2.0;
constexpr double kMmPerInch = 25.4, kLogicalPerInch = 96.0;

struct Sida {
    std::vector<std::uint8_t> rgba;
    int                       w = 0, h = 0;
    double                    pxPerMm = kPxPerMm;

    // Pixeln under sidans punkt (mm).
    const std::uint8_t* vid(double xMm, double yMm) const
    {
        static const std::uint8_t ingen[4] = {1, 2, 3, 4};
        const int x = static_cast<int>(xMm * pxPerMm), y = static_cast<int>(yMm * pxPerMm);
        if (x < 0 || y < 0 || x >= w || y >= h)
            return ingen;
        return &rgba[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

bool är(const std::uint8_t* p, gfx::Color c, int tol = 6)
{
    return std::abs(int(p[0]) - c.r) <= tol && std::abs(int(p[1]) - c.g) <= tol &&
           std::abs(int(p[2]) - c.b) <= tol;
}

std::string px(const std::uint8_t* p)
{
    char b[32];
    std::snprintf(b, sizeof b, "(%d,%d,%d)", p[0], p[1], p[2]);
    return b;
}

// Ett fönster med en rityta — ritytans scen finns bara i CPU-minnet tills en
// ritning ber om den; resurserna är fönstrets.
class Värd : public glaze::AppWindow {
public:
    Värd()
    {
        setTitle("print_gate");
        setClientSize(240, 200);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        yta.setBackground(kVit);
        yta.setBorder(false);
        root().adopt(yta);
    }
    glaze::Canvas yta;
};

// Ritar en sida (wMm × hMm) med `rita` och läser tillbaka den. false om målet
// eller läsningen misslyckas.
bool sida(Värd& fönster, double wMm, double hMm,
          const std::function<void(glaze::Painter&)>& rita, Sida& ut)
{
    const glaze::Ctx c = fönster.yta.behaviorCtx();
    if (!c.r || !c.canvas)
        return false;
    gfx::Renderer& r = *c.r;
    ut.w             = static_cast<int>(std::ceil(wMm * kPxPerMm));
    ut.h             = static_cast<int>(std::ceil(hMm * kPxPerMm));
    glaze::Ctx sc    = c;
    sc.box           = gfx::Rectf{0.0f, 0.0f, static_cast<float>(ut.w),
                                  static_cast<float>(ut.h)};
    sc.area          = sc.box;
    sc.scale         = static_cast<float>(kPxPerMm * kMmPerInch / kLogicalPerInch);
    sc.pxPerMm       = static_cast<float>(kPxPerMm);
    if (!r.beginOffscreen())
        return false;
    const std::unique_ptr<gfx::RenderTarget> mål = r.createRenderTarget(ut.w, ut.h);
    if (!mål->image().valid()) {
        r.endOffscreen();
        return false;
    }
    r.pushTarget(*mål, kVit);
    glaze::detail::PaintContext f;
    f.c      = &sc;
    f.res    = c.canvas;
    f.world  = gfx::MapView{wMm / 2.0, hMm / 2.0, kPxPerMm, 0.0, 0.0, false, sc.box};
    f.screen = glaze::detail::localView(sc.box, sc.scale);
    f.zoom   = kPxPerMm / sc.scale;
    {
        glaze::Painter p(f, false);
        rita(p);
    }
    r.popTarget();
    r.endOffscreen();
    return r.readTarget(*mål, ut.rgba);
}

// Zoomen (logiska px per världsenhet) som gör en världsenhet till en mm på
// sidan.
double mmZoom()
{
    return kLogicalPerInch / kMmPerInch;
}

// En rityta med en röd ruta 40–60 i världen (mm) och en blå 0–10 i hörnet.
void fyll(glaze::Canvas& y)
{
    y.addShape(glaze::Shape::rect({40, 40, 20, 20}, kRöd));
    y.addShape(glaze::Shape::rect({0, 0, 10, 10}, kBlå));
}

void fallP1()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen()) {
        std::printf("  --   P1 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
        return;
    }
    v.window().renderFrame();
    fyll(v.yta);
    Sida s;
    bool ok1 = false, ok2 = false;
    const bool läst = sida(v, 100.0, 60.0, [&](glaze::Painter& p) {
        ok1 = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 30},
                           glaze::Camera{{50, 50}, mmZoom(), 0.0f, 0.0f});
        ok2 = p.drawCanvas(v.yta, glaze::Rect{60, 10, 30, 30},
                           glaze::Camera{{50, 50}, mmZoom() * 2.0, 0.0f, 0.0f});
    }, s);
    check(läst && ok1 && ok2, "P1 båda anropen svarar true och sidan läses");
    if (!läst)
        return;
    // Första rutan (10–50 × 10–40 mm): världen 30–70 × 35–65 → den röda rutan
    // 20 × 20 mm i mitten (20–40 × 15–35 mm på sidan).
    std::string fick;
    bool        ok = är(s.vid(30, 25), kRöd) && är(s.vid(21, 16), kRöd) &&
              är(s.vid(39, 34), kRöd);
    fick += px(s.vid(30, 25)) + px(s.vid(21, 16)) + px(s.vid(39, 34));
    fick += " |";
    ok = är(s.vid(19, 25), kVit) && är(s.vid(41, 25), kVit) && är(s.vid(30, 13), kVit) &&
         är(s.vid(30, 37), kVit) && ok;
    fick += px(s.vid(19, 25)) + px(s.vid(41, 25)) + px(s.vid(30, 13)) + px(s.vid(30, 37));
    check(ok, "P1 första rutan: den röda rutan är 20 × 20 mm, mitt i rutan", fick);
    // Andra rutan (60–90 × 10–40 mm), dubbla zoomen: världen 42,5–57,5 → röd
    // över hela rutan.
    check(är(s.vid(62, 12), kRöd) && är(s.vid(88, 38), kRöd) && är(s.vid(75, 25), kRöd),
          "P1 andra rutan med dubbla zoomen: röd över hela rutan",
          px(s.vid(62, 12)) + px(s.vid(88, 38)) + px(s.vid(75, 25)));
    // Mellan och utanför rutorna är sidan vit (ritytans bakgrund ritas inte).
    check(är(s.vid(55, 25), kVit) && är(s.vid(5, 50), kVit) && är(s.vid(95, 50), kVit),
          "P1 utanför rutorna är sidan vit", px(s.vid(55, 25)) + px(s.vid(5, 50)));
}

void fallP2P3P4()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen()) {
        ++g_skips;
        return;
    }
    v.window().renderFrame();
    // En röd ruta i övre vänstra kvadranten av en 40 × 40 mm ruta (världen
    // 30–70 kring (50, 50)): rutan 30–50 × 30–50.
    v.yta.addShape(glaze::Shape::rect({30, 30, 20, 20}, kRöd));
    const glaze::Camera kam{{50, 50}, mmZoom(), 0.0f, 0.0f};

    // P2 ROTATIONEN: 180° kring rutans mitt (30, 30 på sidan).
    Sida s;
    bool ok = false;
    bool läst = sida(v, 60.0, 60.0, [&](glaze::Painter& p) {
        p.translate(30, 30);
        p.rotate(180);
        ok = p.drawCanvas(v.yta, glaze::Rect{-20, -20, 40, 40}, kam);
    }, s);
    check(läst && ok && är(s.vid(40, 40), kRöd) && är(s.vid(21, 21), kVit),
          "P2 180°: den röda rutan flyttar från övre vänstra till nedre högra kvadranten",
          px(s.vid(40, 40)) + px(s.vid(21, 21)));

    // P3 DELVIS BORTKLIPPT: utan rotation, rutan 10–50 × 10–50 mm på sidan; den
    // röda rutan ligger i 10–30 × 10–30. Klippet täcker sidans 0–20 i x, så bara
    // 10–20 av den syns.
    läst = sida(v, 60.0, 60.0, [&](glaze::Painter& p) {
        auto k = p.save(glaze::Rect{0, 0, 20, 60});
        ok     = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 40}, kam);
    }, s);
    check(läst && ok && är(s.vid(15, 20), kRöd) && är(s.vid(25, 20), kVit),
          "P3 klipp över rutans vänstra del: röd innanför klippet, vit utanför",
          px(s.vid(15, 20)) + px(s.vid(25, 20)));

    // P4 RITYTANS KLIPP gäller sidan: världsklippet x 30–40 (den röda rutan är
    // 30–50) — röd bara där.
    glaze::ClipPolygon klipp;
    klipp.outer = {{30, 0}, {40, 0}, {40, 100}, {30, 100}};
    v.yta.setClipSpace(glaze::ClipSpace::World);
    v.yta.addClip(klipp);
    läst = sida(v, 60.0, 60.0, [&](glaze::Painter& p) {
        ok = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 40}, kam);
    }, s);
    check(läst && ok && är(s.vid(15, 20), kRöd) && är(s.vid(25, 20), kVit),
          "P4 ritytans klipp gäller sidan: röd i x 30–40, vit i 40–50",
          px(s.vid(15, 20)) + px(s.vid(25, 20)));
}

void fallP5()
{
    // Ritytan i ett fönster, sidan i ett ANNAT (utskriftens förloppsfönster).
    Värd a, b;
    a.setOffscreen(true);
    b.setOffscreen(true);
    a.show();
    b.show();
    if (!a.isOpen() || !b.isOpen()) {
        ++g_skips;
        return;
    }
    a.window().renderFrame();
    b.window().renderFrame();
    fyll(a.yta);
    Sida s;
    bool ok = false;
    const bool läst = sida(b, 100.0, 60.0, [&](glaze::Painter& p) {
        ok = p.drawCanvas(a.yta, glaze::Rect{10, 10, 40, 30},
                          glaze::Camera{{50, 50}, mmZoom(), 0.0f, 0.0f});
    }, s);
    check(läst && ok && är(s.vid(30, 25), kRöd) && är(s.vid(19, 25), kVit),
          "P5 en rityta i ett annat fönster ritas med sidans resurser",
          px(s.vid(30, 25)) + px(s.vid(19, 25)));
}

void fallP6()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen()) {
        ++g_skips;
        return;
    }
    v.window().renderFrame();
    fyll(v.yta);
    Sida s;
    bool tomRuta = true, nollZoom = true, nanKamera = true;
    const bool läst = sida(v, 100.0, 60.0, [&](glaze::Painter& p) {
        const glaze::Camera kam{{50, 50}, mmZoom(), 0.0f, 0.0f};
        tomRuta  = p.drawCanvas(v.yta, glaze::Rect{10, 10, 0, 30}, kam);
        nollZoom = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 30},
                                glaze::Camera{{50, 50}, 0.0, 0.0f, 0.0f});
        glaze::Camera3D k3;
        k3.target = {std::nan(""), 0, 0};
        nanKamera = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 30}, k3);
    }, s);
    check(läst && !tomRuta && !nollZoom && !nanKamera,
          "P6 en rektangel utan yta, zoom 0 och en 3D-kamera med NaN ger false",
          std::string(tomRuta ? "tom=true " : "") + (nollZoom ? "zoom0=true " : "") +
              (nanKamera ? "nan=true" : ""));
    // Ur ritytans egen ritning: onPaint anropar drawCanvas på sin egen rityta.
    std::optional<bool> ur;
    v.yta.onPaint = [&](glaze::Widget*, glaze::PaintArgs& e) {
        ur = e.painter.drawCanvas(v.yta, glaze::Rect{0, 0, 10, 10},
                                  glaze::Camera{{0, 0}, 1.0, 0.0f, 0.0f});
    };
    v.yta.invalidate();
    v.window().renderFrame();
    check(ur.has_value() && !*ur,
          "P6 ett anrop ur ritytans egen ritning ger false",
          ur ? (*ur ? "true" : "false") : "onPaint kördes inte");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== print_gate — ritytan på en annan yta (Painter::drawCanvas) ===\n");
    fallP1();
    fallP2P3P4();
    fallP5();
    fallP6();
    if (g_skips > 0 && g_fall == 0) {
        std::printf("=== print_gate: hoppades över (ingen display) ===\n");
        return 77;
    }
    std::printf(g_fel ? "=== print_gate: %d FEL av %d ===\n"
                      : "=== print_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
