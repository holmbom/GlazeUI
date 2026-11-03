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
//   P7  samma anrop återanvänder målet (`pageRenders` står still); en ny form
//       eller en ny kamera ritar om
//   P6  `false` (+ logg): en rektangel utan yta, en ogiltig kamera (zoom 0, NaN
//       i en 3D-kamera) och ett anrop ur ritytans egen ritning (`onPaint`).
//   Sabotage som SKA fälla dem: `drawTransient` utan bildritningen (P1–P5 —
//   sidan förblir vit), `imageCorners` utan `xf.apply` (P2 — ingen rotation),
//   `clip.apply` bort ur `drawTransient` (P3), `pc.scene`/`lf.clips` bort ur
//   renderCanvasTarget (P4), målet återanvänt av båda anropen i `pageTarget`
//   (P1 — den andra rutan skriver över den första), `PaintingScope` bort ur
//   `drawCanvas` (P6 — onPaint-anropet ritar), måttet i px taget ur rektangelns
//   mm (P1 — fel upplösning).
//   P8  `renderPageImage`: 100 m i 1:1000 mäter 100 mm och en linje på 6,0 mm mäter
//       6,0 mm i två tätheter. P9 ett dolt fönster ger samma mått. P10 alla bilder finns med
//       i första anropet trots en uppladdning per varv. P11 fler bilder än varven
//       räcker till svarar `complete` false. P12 taket sänker tätheten och säger
//       vilken; fel indata och ett anrop inifrån en ritning ger ingen bild.
//   P13 etiketterna tonar inte på en sida. P14 en sida tar inte skärmens träffar.
//   Sabotage som SKA fälla dem: `sc.pxPerMm` kvar på fönstrets (P8 — linjen),
//   `continue` bort ur `renderRaster` (P10), `complete = false` bort (P11),
//   `pageRaster` utan taket (P12), `pageRender` bort ur `a.labelFade` (P13),
//   `pageGlyphs_` bort ur `glyphs` (P14).
//   P15 `PrintJob::lastError()`: tom före första anropet, felbeskedet efter ett
//   `false` ur `showDialog` och `run` med ett stängt fönster, och texten följer språket.
//   Sabotage som SKA fälla den: `error_ = tr(str::PrintErrorDialog)` bort ur
//   `showDialog`s fönsterprov, `error_` som en `std::string` upplöst vid felet (bytet).
//   P16 FILREGLERNA (`src/print_output.h`) och utfallet utan skrivare: `outcome()` är
//   None före första anropet och Failed efter ett stängt fönster, filen som står kvar
//   efter skrivarvalet (en fysisk skrivare tömmer den, en fil vald i vår dialog frågas om
//   på nytt, appens fil står kvar), filen i ett `run` (en använd dialogfil gäller inte
//   igen), när filvalet visas, det tomma intervallet, förslagets rensning, ändelse och
//   ordning, `setOutputFile` (absolut, åäö hela vägen, tom rensar), `setPrinter`, jobbets
//   resultat som utfall och felid, ett känt tomt intervall i `run` som NothingToPrint,
//   och texten för id 406 på två språk.
//   Sabotage som SKA fälla den: tömningen ur `keptOutputFile` bort (P16 — en fysisk skrivare
//   behåller filen), `!fromDialog` bort ur `keptOutputFile` (P16 — dialogfilen står kvar),
//   `spent` struntad i `fileForRun` (P16 — en använd fil gäller igen), `emptyRange` utan
//   `pages > 0` (P16), `cleanFileName` utan de förbjudna tecknen (P16),
//   `fs::absolute` bort ur `setOutputFile` (P16), `outcome_` bort ur `run`s intervallprov
//   (P16), `PrintErrorFile` ur ett språk i framework.langtxt (lang_gate, och P16 på en tom
//   text).
//   Skrivarens DC efter en kompilering mäter print_dc_gate; filutskriften på riktigt
//   mäter print_file_gate.
//   MÄTER INTE skrivaren, PDF-utdata, del-vyer eller rutnätet (ritas inte), eller
//   att 300 dpi är lika skarpt som `drawCanvas` på en utskriftssida — de delar väg.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — localView
#include "canvas/resources.h"     // intern — pageRenders
#include "canvas/scene.h"         // intern — PaintContext för sidans ritläge
#include "print_output.h"         // intern — filreglerna

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
    glaze::Polygon klipp;
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

// P7 ÅTERANVÄNDNINGEN: samma `drawCanvas` (samma rityta, ruta, kamera och
// innehåll) ritar inte om ritytan — målet återanvänds och `pageRenders` står
// still; en ny form eller en ny kamera ritar om. Sabotage som SKA fälla P7:
// målets nyckel utan `damageStamp` (ingen omritning efter `addShape`), utan
// kameran (ingen omritning vid ny kamera) eller en nyckel som ändras varje
// anrop (omritning av samma anrop).
void fallP7()
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
    const glaze::CanvasResources* res = v.yta.behaviorCtx().canvas;
    if (!res) {
        check(false, "P7 ritytans resurser finns");
        return;
    }
    const glaze::Camera kam{{50, 50}, mmZoom(), 0.0f, 0.0f};
    const auto rita = [&](const glaze::Camera& k) {
        Sida s;
        bool ok = false;
        const bool läst = sida(v, 100.0, 60.0, [&](glaze::Painter& p) {
            ok = p.drawCanvas(v.yta, glaze::Rect{10, 10, 40, 30}, k);
        }, s);
        return läst && ok;
    };
    const bool          a  = rita(kam);
    const std::uint64_t n1 = res->pageRenders();
    const bool          b  = rita(kam);
    const std::uint64_t n2 = res->pageRenders();
    check(a && b && n1 >= 1 && n2 == n1,
          "P7 samma anrop två gånger ritar ritytan EN gång",
          std::to_string(n1) + " → " + std::to_string(n2));
    v.yta.addShape(glaze::Shape::rect({0, 0, 5, 5}, kRöd));
    const bool          c  = rita(kam);
    const std::uint64_t n3 = res->pageRenders();
    check(c && n3 == n2 + 1, "P7 en ny form ritar om",
          std::to_string(n2) + " → " + std::to_string(n3));
    const bool          d  = rita(glaze::Camera{{40, 40}, mmZoom(), 0.0f, 0.0f});
    const std::uint64_t n4 = res->pageRenders();
    check(d && n4 == n3 + 1, "P7 en ny kamera ritar om",
          std::to_string(n3) + " → " + std::to_string(n4));
}

// P8–P12 SIDAN SOM BILD (`renderPageImage`). Pixeln under sidans punkt i mm.
const std::uint8_t* bildVid(const glaze::PageImage& b, double xMm, double yMm)
{
    const double pxPerMm = b.dpi / kMmPerInch;
    return b.bitmap.pixels() +
           (static_cast<std::size_t>(yMm * pxPerMm) * b.bitmap.width() +
            static_cast<std::size_t>(xMm * pxPerMm)) * 4;
}

// Antal röda pixlar i raden vid y (mm) längs bilden — ett mått i millimeter
// genom bildens täthet.
double rödMm(const glaze::PageImage& b, double yMm)
{
    const double pxPerMm = b.dpi / kMmPerInch;
    const int    rad     = static_cast<int>(yMm * pxPerMm);
    int          n       = 0;
    for (int x = 0; x < b.bitmap.width(); ++x)
        if (är(b.bitmap.pixels() + (static_cast<std::size_t>(rad) * b.bitmap.width() + x) * 4,
               kRöd, 60))
            ++n;
    return n / pxPerMm;
}

double rödMmLodrätt(const glaze::PageImage& b, double xMm)
{
    const double pxPerMm = b.dpi / kMmPerInch;
    const int    kol     = static_cast<int>(xMm * pxPerMm);
    int          n       = 0;
    for (int y = 0; y < b.bitmap.height(); ++y)
        if (är(b.bitmap.pixels() + (static_cast<std::size_t>(y) * b.bitmap.width() + kol) * 4,
               kRöd, 60))
            ++n;
    return n / pxPerMm;
}

// En pixel per färg, så att varje bild är en egen uppladdning.
glaze::Bitmap enfärgad(gfx::Color f)
{
    glaze::Bitmap b(2, 2);
    b.fill(f);
    return b;
}

// P8 SKALAN OCH TÄTHETEN: en rityta i meter med en ruta på 100 × 40 m, ritad i
// 1:1000 på en sida med en linje på 6,0 mm, mäter 100 mm, 40 mm och 6,0 mm i TVÅ
// tätheter — de som fönstrets täthet hade gjort fel.
// P9 DOLT FÖNSTER: `hide()` före `show()` ger samma mått.
void fallP8P9()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen()) {
        std::printf("  --   P8 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
        return;
    }
    v.yta.addShape(glaze::Shape::rect({0, 0, 100, 40}, kRöd));
    // 1 m = 1 mm på sidan: 1:1000.
    const glaze::Camera kam{{50, 20}, mmZoom(), 0.0f, 0.0f};
    for (const double dpi : {127.0, 254.0}) {
        const auto b = glaze::renderPageImage(v, 200.0, 120.0, dpi, [&](glaze::Painter& p) {
            p.drawCanvas(v.yta, glaze::Rect{20, 10, 120, 60}, kam);
            p.drawLine({20, 100}, {180, 100}, glaze::Stroke{kRöd, glaze::mm(6.0)});
        });
        const std::string d = " i " + std::to_string(static_cast<int>(dpi)) + " dpi";
        check(b.has_value() && b->bitmap.valid(), "P8 en bild" + d);
        if (!b)
            return;
        const double pxPerMm = dpi / kMmPerInch;
        check(std::abs(b->dpi - dpi) < 1e-9, "P8 bilden har den begärda tätheten" + d);
        check(b->bitmap.width() == static_cast<int>(std::lround(200.0 * pxPerMm)) &&
                  b->bitmap.height() == static_cast<int>(std::lround(120.0 * pxPerMm)),
              "P8 bildens mått följer tätheten" + d);
        check(b->complete, "P8 inga bilder saknas" + d);
        check(std::abs(rödMm(*b, 40.0) - 100.0) < 0.5, "P8 100 m i 1:1000 mäter 100 mm" + d,
              std::to_string(rödMm(*b, 40.0)));
        check(std::abs(rödMmLodrätt(*b, 80.0) - 46.0) < 0.5,
              "P8 40 m mäter 40 mm och linjen 6,0 mm" + d,
              std::to_string(rödMmLodrätt(*b, 80.0)));
        check(är(bildVid(*b, 5.0, 40.0), kVit), "P8 sidan är vit utanför rutan" + d);
    }

    Värd dolt;
    dolt.hide();
    dolt.show();
    if (!dolt.isOpen())
        return;
    dolt.yta.addShape(glaze::Shape::rect({0, 0, 100, 40}, kRöd));
    const auto b = glaze::renderPageImage(dolt, 200.0, 120.0, 127.0, [&](glaze::Painter& p) {
        p.drawCanvas(dolt.yta, glaze::Rect{20, 10, 120, 60}, kam);
    });
    check(b.has_value() && std::abs(rödMm(*b, 40.0) - 100.0) < 0.5,
          "P9 ett dolt fönster ger samma mått");
}

// P10 ALLA BILDER I FÖRSTA ANROPET: en uppladdning per varv, och fem bilder på en
// rityta och fem i målaren är alla med. P11 TAKET: fler bilder än varven räcker
// till svarar `complete` false, och bilden finns ändå.
void fallP10P11()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen())
        return;
    v.window().renderFrame();
    const std::size_t förval = glaze::app().canvasUploadBudget();
    glaze::app().setCanvasUploadBudget(1);

    std::vector<glaze::Bitmap> rutor;
    for (int i = 0; i < 10; ++i)
        rutor.push_back(enfärgad(gfx::Color{static_cast<std::uint8_t>(200 + i), 0, 0, 255}));
    for (int i = 0; i < 5; ++i)
        v.yta.addShape(glaze::Shape::image({i * 10.0, 0.0, 10.0, 10.0}, rutor[std::size_t(i)]));
    const glaze::Camera kam{{25, 5}, mmZoom(), 0.0f, 0.0f};
    int varv = 0;
    const auto b = glaze::renderPageImage(v, 60.0, 30.0, 50.8, [&](glaze::Painter& p) {
        ++varv;
        p.drawCanvas(v.yta, glaze::Rect{5, 5, 50, 10}, kam);
        for (int i = 0; i < 5; ++i)
            p.drawImage(glaze::Rect{5.0 + i * 10.0, 20.0, 10.0, 5.0}, rutor[std::size_t(5 + i)]);
    });
    check(b.has_value() && b->complete, "P10 sidan är komplett");
    check(varv > 1, "P10 uppladdningen väntade, så sidan ritades om",
          std::to_string(varv));
    if (b) {
        bool allaRitytan = true, allaMålaren = true;
        for (int i = 0; i < 5; ++i) {
            allaRitytan = är(bildVid(*b, 10.0 + i * 10.0, 10.0),
                             gfx::Color{static_cast<std::uint8_t>(200 + i), 0, 0, 255}, 3) &&
                          allaRitytan;
            allaMålaren = är(bildVid(*b, 10.0 + i * 10.0, 22.0),
                             gfx::Color{static_cast<std::uint8_t>(205 + i), 0, 0, 255}, 3) &&
                          allaMålaren;
        }
        check(allaRitytan, "P10 alla bilder på ritytan finns med");
        check(allaMålaren, "P10 alla bilder i målaren finns med");
    }

    std::vector<glaze::Bitmap> många;
    for (int i = 0; i < 300; ++i)
        många.push_back(enfärgad(
            gfx::Color{static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 8), 1, 255}));
    const auto t = glaze::renderPageImage(v, 30.0, 30.0, 25.4, [&](glaze::Painter& p) {
        for (std::size_t i = 0; i < många.size(); ++i)
            p.drawImage(glaze::Rect{double(i % 30), double(i / 30), 1.0, 1.0}, många[i]);
    });
    check(t.has_value() && !t->complete,
          "P11 fler bilder än varven räcker till: complete false, och en bild ändå");
    glaze::app().setCanvasUploadBudget(förval);
}

// P13 ETIKETTERNA TONAR INTE på en sida: med `setLabelFade(true)` är en etikett
// fullt röd på första sidan och på nästa, med en annan kamera.
// P14 SKÄRMENS ETIKETTER STÅR KVAR: en sida i en annan täthet får inte ta
// etikettens träff på ritytan, utan att ritytan ritas om.
void fallP13P14()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen())
        return;
    v.yta.setLabelFade(true);
    v.yta.setCamera(glaze::Camera{{50, 50}, mmZoom(), 0.0f, 0.0f});
    v.yta.addShape(glaze::Shape::text(
        {50, 50}, "Etikett", glaze::TextStyle{.size = glaze::mm(10.0f), .color = kRöd}));
    v.window().renderFrame();

    const auto rödaPixlar = [](const glaze::PageImage& b) {
        int n = 0;
        for (int i = 0; i < b.bitmap.width() * b.bitmap.height(); ++i)
            if (är(b.bitmap.pixels() + std::size_t(i) * 4, kRöd, 40))
                ++n;
        return n;
    };
    const auto sidan = [&](glaze::Point mitt) {
        return glaze::renderPageImage(v, 100.0, 60.0, 50.8, [&](glaze::Painter& p) {
            p.drawCanvas(v.yta, glaze::Rect{0, 0, 100, 60},
                         glaze::Camera{mitt, mmZoom(), 0.0f, 0.0f});
        });
    };
    const auto första = sidan({50, 50});
    const auto andra  = sidan({40, 45});
    check(första.has_value() && rödaPixlar(*första) > 50,
          "P13 etiketten är fullt röd på första sidan",
          första ? std::to_string(rödaPixlar(*första)) : std::string{});
    check(andra.has_value() && rödaPixlar(*andra) > 50,
          "P13 etiketten är fullt röd med en annan kamera",
          andra ? std::to_string(rödaPixlar(*andra)) : std::string{});

    const gfx::Sizef   yta = v.yta.actualSize();
    const glaze::Point mitt{yta.w / 2.0, yta.h / 2.0};
    check(!v.yta.hitTest(mitt).empty(), "P14 etiketten träffas före sidan");
    const auto sida = glaze::renderPageImage(v, 100.0, 60.0, 254.0, [&](glaze::Painter& p) {
        p.drawCanvas(v.yta, glaze::Rect{0, 0, 100, 60},
                     glaze::Camera{{50, 50}, mmZoom(), 0.0f, 0.0f});
    });
    check(sida.has_value() && !v.yta.hitTest(mitt).empty(),
          "P14 etiketten träffas efter en sida i en annan täthet");
}

// P12 TAKET OCH FELEN: en sida över taket får en lägre täthet och anroparen får veta
// vilken. Fel mått, ingen ritfunktion, ett stängt fönster och ett anrop inifrån en
// ritning ger ingen bild.
void fallP12()
{
    Värd v;
    v.setOffscreen(true);
    v.show();
    if (!v.isOpen())
        return;
    v.window().renderFrame();
    const auto tom = [](glaze::Painter&) {};
    const auto lång = glaze::renderPageImage(v, 1000.0, 10.0, 300.0, tom);
    check(lång.has_value() && lång->dpi < 300.0, "P12 över taket: en lägre täthet");
    if (lång) {
        const int tak = std::max(lång->bitmap.width(), lång->bitmap.height());
        check(tak <= glaze::PrintJob::kMaxPagePx, "P12 längsta sidan inom taket",
              std::to_string(tak));
        check(std::abs(lång->bitmap.width() / lång->dpi * kMmPerInch - 1000.0) < 1.0,
              "P12 tätheten stämmer med bildens mått",
              std::to_string(lång->dpi));
    }
    check(!glaze::renderPageImage(v, 0.0, 10.0, 300.0, tom), "P12 bredden 0: ingen bild");
    check(!glaze::renderPageImage(v, 10.0, -1.0, 300.0, tom), "P12 negativ höjd: ingen bild");
    check(!glaze::renderPageImage(v, 10.0, 10.0, 0.0, tom), "P12 tätheten 0: ingen bild");
    check(!glaze::renderPageImage(v, 10.0, 10.0, std::nan(""), tom), "P12 NaN: ingen bild");
    check(!glaze::renderPageImage(v, 10.0, 10.0, 96.0, nullptr), "P12 ingen ritfunktion");
    Värd stängt;
    check(!glaze::renderPageImage(stängt, 10.0, 10.0, 96.0, tom), "P12 stängt fönster");
    std::optional<glaze::PageImage> inre = glaze::PageImage{};
    const auto yttre = glaze::renderPageImage(v, 10.0, 10.0, 96.0, [&](glaze::Painter&) {
        inre = glaze::renderPageImage(v, 10.0, 10.0, 96.0, tom);
    });
    check(yttre.has_value() && !inre, "P12 inifrån en ritning: ingen bild, och den yttre lever");
}

// P15 FELBESKEDET: ett fel ser inte ut som ett avbrott. Utan utskriftssystem (Linux)
// är beskedet det, annars dialogens eller jobbets.
void fallP15()
{
    Värd öppet;
    öppet.setOffscreen(true);
    öppet.show();
    if (!öppet.isOpen())
        return;
    const auto beskedet = [](std::uint32_t id) {
        return glaze::tr(glaze::PrintJob::isSupported() ? id
                                                        : glaze::str::PrintErrorUnsupported);
    };
    glaze::PrintJob jobb;
    check(jobb.lastError().empty(), "P15 inget fel före första anropet");

    Värd stängt;
    const glaze::TrText dialogen = beskedet(glaze::str::PrintErrorDialog);
    check(!jobb.showDialog(stängt), "P15 showDialog med ett stängt fönster: false");
    check(!jobb.lastError().empty() && jobb.lastError() == dialogen.str(),
          "P15 showDialog: dialogens felbesked", jobb.lastError());

    const glaze::TrText jobbet = beskedet(glaze::str::PrintErrorJob);
    const auto          tom    = [](int, glaze::Painter&) { return false; };
    check(!jobb.run(stängt, 1, tom), "P15 run med ett stängt fönster: false");
    check(!jobb.lastError().empty() && jobb.lastError() == jobbet.str(),
          "P15 run: jobbets felbesked", jobb.lastError());
    check(!jobb.run(öppet, -1, tom) && jobb.lastError() == jobbet.str(),
          "P15 run med ett negativt sidantal: jobbets felbesked", jobb.lastError());

    const std::string förut = glaze::language();
    glaze::setLanguage("en");
    const std::string engelska = jobb.lastError();
    glaze::setLanguage("sv");
    check(!engelska.empty() && engelska != jobb.lastError() && jobb.lastError() == jobbet.str(),
          "P15 felbeskedet följer språket", engelska + " / " + jobb.lastError());
    glaze::setLanguage(förut);
}

// P16 FILREGLERNA OCH UTFALLET: de rena reglerna i print_output.h, och det som `PrintJob`
// svarar utan skrivare.
void fallP16()
{
    namespace po = glaze::printout;
    using glaze::PrintOutcome;

    // Filen efter skrivarvalet i utskriftsdialogen.
    check(po::keptOutputFile("a.pdf", false, true) == "a.pdf",
          "P16 en fil från appen står kvar hos en filskrivare");
    check(po::keptOutputFile("a.pdf", false, false).empty(),
          "P16 en fysisk skrivare tömmer filen");
    check(po::keptOutputFile("a.pdf", true, true).empty(),
          "P16 en fil vald i vår dialog frågas om på nytt");

    // Filen i ett `run`.
    check(po::fileForRun("a.pdf", false, true, false) == "a.pdf",
          "P16 run: appens fil gäller också utan filskrivare");
    check(po::fileForRun("a.pdf", true, false, true) == "a.pdf",
          "P16 run: en dialogfil gäller sitt jobb");
    check(po::fileForRun("a.pdf", true, true, true).empty(),
          "P16 run: en använd dialogfil gäller inte igen");
    check(po::fileForRun("a.pdf", true, false, false).empty(),
          "P16 run: en dialogfil utan filskrivare gäller inte");

    // När filvalet visas.
    check(po::needsFilePrompt(true, "", true), "P16 filvalet: skrivaren frågar, ingen fil");
    check(!po::needsFilePrompt(false, "", true), "P16 filvalet: fysisk skrivare");
    check(!po::needsFilePrompt(true, "a.pdf", true), "P16 filvalet: filen är satt");
    check(!po::needsFilePrompt(true, "", false), "P16 filvalet: avstängt");
    check(po::emptyRange(3, 5) && !po::emptyRange(3, 3) && !po::emptyRange(0, 5) &&
              !po::emptyRange(3, 0),
          "P16 tomt intervall bara när antalet är känt och början ligger efter slutet");

    // Förslaget.
    check(po::cleanFileName("Ritning: 12/A?") == "Ritning 12A", "P16 förbjudna tecken bort",
          po::cleanFileName("Ritning: 12/A?"));
    check(po::cleanFileName("  x. ") == "x" && po::cleanFileName("...").empty() &&
              po::cleanFileName("å ä\tö") == "å äö",
          "P16 blanktecken och punkter i kanterna bort, åäö kvar");
    check(po::suggestedName("", "", "Fönster", ".pdf") == "Fönster.pdf",
          "P16 förslaget: ägarens titel med ändelse");
    check(po::suggestedName("Egen", "Titel", "Ägare", ".pdf") == "Egen.pdf" &&
              po::suggestedName("", "Titel", "Ägare", "") == "Titel",
          "P16 förslaget: appens går först, ingen ändelse när den är okänd");
    check(po::suggestedName("???", "Titel", "Ägare", ".pdf") == "Titel.pdf",
          "P16 förslaget: ett namn som rensas bort ger vika för nästa");
    check(po::suggestedName("v1.2", "", "", ".pdf") == "v1.2" &&
              po::suggestedName("", "", "", ".pdf").empty(),
          "P16 förslaget: en egen ändelse står kvar, inget namn ger inget");
    check(po::extensionLabel(".pdf") == "PDF" && po::extensionLabel("").empty(),
          "P16 filtrets namn ur ändelsen");

    // Jobbresultatet.
    check(po::outcomeOf(gfx::PrintJobResult::Ok) == PrintOutcome::Ok &&
              po::outcomeOf(gfx::PrintJobResult::Cancelled) == PrintOutcome::Cancelled &&
              po::outcomeOf(gfx::PrintJobResult::FileFailed) == PrintOutcome::FileFailed &&
              po::outcomeOf(gfx::PrintJobResult::Failed) == PrintOutcome::Failed,
          "P16 jobbresultatet som utfall");
    check(po::errorTextId(gfx::PrintJobResult::FileFailed) == glaze::str::PrintErrorFile &&
              po::errorTextId(gfx::PrintJobResult::Failed) == glaze::str::PrintErrorJob &&
              po::errorTextId(gfx::PrintJobResult::Ok) == 0 &&
              po::errorTextId(gfx::PrintJobResult::Cancelled) == 0,
          "P16 felbeskedet per jobbresultat, inget för Ok och avbrott");

    // Egenskaperna.
    glaze::PrintJob jobb;
    check(jobb.outcome() == PrintOutcome::None, "P16 inget utfall före första anropet");
    jobb.setOutputFile("rapport å.pdf");
    namespace fd = glaze::filedlg;
    const std::string väntad =
        fd::utf8FromPath(std::filesystem::absolute(fd::pathFromUtf8("rapport å.pdf")));
    check(fd::pathFromUtf8(jobb.outputFile()).is_absolute() && jobb.outputFile() == väntad,
          "P16 setOutputFile: absolut och åäö orörda", jobb.outputFile());
    jobb.setOutputFile("");
    check(jobb.outputFile().empty(), "P16 setOutputFile: tom rensar");
    jobb.setPrinter("Någon skrivare");
    check(jobb.printerName() == "Någon skrivare" && !jobb.promptsForFile(),
          "P16 setPrinter: namnet satt, skrivaren okänd tills den öppnats");

    // Utfallet utan skrivare.
    Värd stängt;
    check(!jobb.showDialog(stängt) && jobb.outcome() == PrintOutcome::Failed &&
              !jobb.isCancelled(),
          "P16 showDialog med ett stängt fönster: Failed");
    const auto tom = [](int, glaze::Painter&) { return false; };
    check(!jobb.run(stängt, 1, tom) && jobb.outcome() == PrintOutcome::Failed &&
              !jobb.isCancelled(),
          "P16 run med ett stängt fönster: Failed");

    Värd öppet;
    öppet.setOffscreen(true);
    öppet.show();
    if (öppet.isOpen()) {
        glaze::PrintJob intervall;
        intervall.setPageRange(5, 6);
        const bool gick = intervall.run(öppet, 3, tom);
        const PrintOutcome väntat = glaze::PrintJob::isSupported() ? PrintOutcome::NothingToPrint
                                                                   : PrintOutcome::Failed;
        check(!gick && intervall.outcome() == väntat && !intervall.isCancelled() &&
                  intervall.pagesPrinted() == 0,
              "P16 run: ett känt tomt intervall är NothingToPrint");
        check(!glaze::PrintJob::isSupported() || intervall.lastError().empty(),
              "P16 run: inget att skriva ut är inget fel", intervall.lastError());
    }

    // Texten för filfelet följer språket.
    const std::string förut = glaze::language();
    glaze::setLanguage("sv");
    const glaze::TrText text = glaze::tr(glaze::str::PrintErrorFile);
    const std::string   sv   = text.str();
    glaze::setLanguage("en");
    const std::string en = text.str();
    glaze::setLanguage(förut);
    check(sv == "Filen gick inte att skriva." && en == "The file could not be written.",
          "P16 id 406 på svenska och engelska", sv + " / " + en);
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
    fallP7();
    fallP8P9();
    fallP10P11();
    fallP12();
    fallP13P14();
    fallP15();
    fallP16();
    if (g_skips > 0 && g_fall == 0) {
        std::printf("=== print_gate: hoppades över (ingen display) ===\n");
        return 77;
    }
    std::printf(g_fel ? "=== print_gate: %d FEL av %d ===\n"
                      : "=== print_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
