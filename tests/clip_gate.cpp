// clip_gate — RUNDAT BARNKLIPP, mätt i PIXLAR.
//
// VAD GRINDEN PÅSTÅR — och inget mer (MÄTKONTRAKT-regeln: en grind som
// påstår mer än den mäter är värre än ingen):
//
//   1. Ett barn i en container vars `clipRadius()` är > 0 blir BORTMASKAT
//      i containerns hörn, medan kantens mitt och centrum står kvar.
//   2. Samma träd med radie 0 lämnar hörnet orört — kontrollkörningen som
//      gör punkt 1 till ett utfall och inte till en tautologi.
//   3. Masken gäller TVÅ av tre shader-vägar: mode 1 (SDF-fylld rect, det
//      `Draw::rect` ritar) OCH mode 0 (texturmask, samma väg som all
//      TEXT). Mäts bara mode 1 missas att text läcker.
//   4. NÄSTLING: en rundad panel inuti en rundad panel maskas av BÅDA —
//      det yttre hörnet skär även där det inre barnet vill rita. Fallet
//      finns för att `applyTop()` väljer de fyra yttersta rundade
//      nivåerna ur stacken, och det urvalet är annars omätt.
//
// Grinden mäter INTE, och det är en SKRIVEN lista och inte ett förbiseende:
//   · kantkvalitet/AA-bandet — probe-punkterna ligger med marginal utanför
//   · **mode 5 (BILDER)** — den vägen kräver ett laddat `assets.bin`, och
//     att binda pixelgrinden till demons assetfil hade gjort den skör.
//     Alfakontraktet skiljer sig just här (rak alfa ⇒ bara `.a` maskeras,
//     medan compositens quad är premultiplicerad), så vägen är GRANSKAD men
//     inte MÄTT.
//   · nästling djupare än TVÅ nivåer, och urvalet när fler än fyra rundade
//     nivåer är aktiva (degraderingen till rektangel)
//   · hit-test — följer med flit REKTANGELN, skriven gräns i widget.h
//   · andra backends än den som körs — samma kod, men mäts inte härifrån,
//     se doc/os/*/TESTS.md
//
// VÄGEN är glaze-renders, redan bevisad: `setOffscreen(true)` → `show()`
// → `renderFrame()` → `captureFrame()`. `captureFrame` läser retained-
// FBO:n FÖRE compositen, så det är RITVÄGENS klipp som syns — fönstrets
// egen hörnmask (`setCompositeRadius`) ligger efter och kan inte
// förväxlas med den vi mäter.
//
// INGEN TYST SKIP: utan fungerande headless-yta FALLER
// grinden. `unadopt_gate`s SKIP_RETURN_CODE 77 finns för deny-fall och är
// inte en förebild — en pixelgrind som hoppar bevisar ingenting om
//   shadern, och en grind som tyst slutar köras mäter ingenting.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fel = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// ------------------------------------------------------------- fixturen

// Tal i en TESTFIL är tillåtna (regel 3 gäller widget-filer) — de här är
// grindens mätpunkter och hör inte hemma i en Style.
constexpr float kFönsterW = 400.0f;
constexpr float kFönsterH = 340.0f;
constexpr float kPanelX   = 40.0f;
constexpr float kPanelY   = 40.0f;
constexpr float kPanelW   = 240.0f;
constexpr float kPanelH   = 200.0f;
constexpr float kRadie    = 24.0f;   // logiska px

// PROBE-MARGINALEN: hörnpunkten (d, d) från hörnet ligger innanför
// rundningen först när d >= r(1 − 1/√2) ≈ 0.293·r. Med r = 24 är gränsen
// ≈ 7.0 px, och vi mäter på d = 2 — alltså ~7 px UTANFÖR bågen, långt
// bortom AA-bandets 1 px. Marginalen är utskriven för att den är hela
// skälet att grinden inte behöver kopiera en formel ur shadern.
constexpr int kProbe = 2;

const gfx::Color kRöd{220, 30, 30, 255};
const gfx::Color kGrön{30, 200, 60, 255};

bool ärRöd(const std::uint8_t* p)
{
    return p[0] > 180 && p[1] < 90 && p[2] < 90;
}
bool ärGrön(const std::uint8_t* p)
{
    return p[1] > 150 && p[0] < 120 && p[2] < 120;
}

// Barnet: överhalvan i mode 1 (`Draw::rect` → fillRoundedRect), underhalvan
// i mode 0 (`Draw::line` med bredd → fillRect → vita R8-texturen). Två
// shader-grenar, en widget.
class TvåfärgadRuta : public glaze::Widget {
public:
    void draw(const glaze::Ctx& c) override
    {
        const gfx::Rectf b = c.bounds();
        const float      h = b.h / 2.0f;
        glaze::Draw::rect(c, {b.x, b.y, b.w, h}, 0.0f, kRöd);
        // Vågrät stav med hela underhalvans tjocklek — `Draw::line` väljer
        // fillRect-vägen för axelparallella breda linjer.
        glaze::Draw::line(c, b.x, b.y + h + h / 2.0f, b.x + b.w,
                          b.y + h + h / 2.0f, glaze::Pen{kGrön, h});
    }
};

// Containern: en vanlig Panel som RUNDAR sitt barnklipp. Radien anges i
// logiska px och skalas här — `clipRadius` talar FYSISKA px, parhäst till
// `clipRect`.
class RundadPanel : public glaze::Panel {
public:
    float radie = 0.0f;

    float clipRadius(const glaze::Ctx& c) const override
    {
        return radie * c.scale;
    }
};

class Fixtur : public glaze::AppWindow {
public:
    explicit Fixtur(float radie)
    {
        setTitle("clip_gate");
        setClientSize(static_cast<int>(kFönsterW),
                      static_cast<int>(kFönsterH));
        panel.name = "panel";
        // RAMLÖS med flit: `Panel::innerBounds` insettar bara när ramen
        // finns, så clipRect blir EXAKT panelens rect och probe-punkterna
        // kan räknas ur den röda ytans bounding box utan att en
        // `panelInset` smyger sig in i matten.
        panel.setBorder(false);
        panel.x      = kPanelX;
        panel.y      = kPanelY;
        panel.width  = kPanelW;
        panel.height = kPanelH;
        panel.radie  = radie;

        ruta.name = "ruta";
        ruta.dock = glaze::Dock::Fill;   // barnet FYLLER klippytan
        panel.adopt(ruta);
        root().adopt(panel);
    }

    RundadPanel   panel;
    TvåfärgadRuta ruta;
};

// NÄSTLINGEN — och kombinationen är vald med flit: yttre panelen är RUNDAD,
// den inre RAK. Den inre pushar alltså ett REKTANGULÄRT klipp ovanpå det
// rundade, vilket är precis det fall där `applyTop()` kan gå fel: behåller
// den den yttre masken maskas barnets hörn ändå, medan en implementation
// som låter den innersta nivån bestämma lämnar hörnet färgat.
// En rundad inre panel hade DÖLJT det felet — då hade hörnet maskats av den
// inre masken och grinden varit grön av fel skäl.
class NästladFixtur : public glaze::AppWindow {
public:
    explicit NästladFixtur(float yttreRadie)
    {
        setTitle("clip_gate nästlad");
        setClientSize(static_cast<int>(kFönsterW),
                      static_cast<int>(kFönsterH));
        yttre.name = "yttre";
        yttre.setBorder(false);
        yttre.x      = kPanelX;
        yttre.y      = kPanelY;
        yttre.width  = kPanelW;
        yttre.height = kPanelH;
        yttre.radie  = yttreRadie;

        inre.name = "inre";
        inre.setBorder(false);
        inre.dock  = glaze::Dock::Fill;
        inre.radie = 0.0f;   // RAK — se noten ovan

        ruta.name = "ruta";
        ruta.dock = glaze::Dock::Fill;
        inre.adopt(ruta);
        yttre.adopt(inre);
        root().adopt(yttre);
    }

    RundadPanel   yttre;
    RundadPanel   inre;
    TvåfärgadRuta ruta;
};

// En renderad frame + de mätpunkter grinden bryr sig om.
struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    const std::uint8_t* at(int x, int y) const
    {
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

bool rendera(float radie, Bild& ut)
{
    Fixtur win(radie);
    win.setOffscreen(true);
    win.show();
    if (!win.isOpen())
        return false;
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

bool renderaNästlad(float yttreRadie, Bild& ut)
{
    NästladFixtur win(yttreRadie);
    win.setOffscreen(true);
    win.show();
    if (!win.isOpen())
        return false;
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

// Röda ytans bounding box. Rundningen kapar HÖRNEN men aldrig kantens
// mitt, så boxen är densamma med och utan radie — det är just därför den
// duger som referens för båda körningarna.
bool rödBox(const Bild& b, int& x0, int& y0, int& x1, int& y1)
{
    x0 = b.w;
    y0 = b.h;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (ärRöd(b.at(x, y))) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    return x1 >= x0 && y1 >= y0;
}

bool grönBox(const Bild& b, int& x0, int& y0, int& x1, int& y1)
{
    x0 = b.w;
    y0 = b.h;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x)
            if (ärGrön(b.at(x, y))) {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x);
                y1 = std::max(y1, y);
            }
    return x1 >= x0 && y1 >= y0;
}

std::string punkt(const Bild& b, int x, int y)
{
    const std::uint8_t* p = b.at(x, y);
    char buf[80];
    std::snprintf(buf, sizeof buf, "(%d,%d) = rgba %u,%u,%u,%u", x, y,
                  unsigned(p[0]), unsigned(p[1]), unsigned(p[2]),
                  unsigned(p[3]));
    return buf;
}

} // namespace

int main(int argc, char** argv)
{
    // `--png <fil>` skriver den RUNDADE körningen som bild. Den är inte en
    // del av grinden utan den POSITIVA FIXTUREN: en bild man kan ÖGNA säger
    // direkt om
    // masken nådde shadern eller inte. Se doc/os/*/TESTS.md.
    std::string pngUt;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            pngUt = argv[++i];
    }
    std::printf("=== clip_gate — rundat barnklipp, mätt i pixlar ===\n");

    // ---------------------------------------------------- kontrollen (r=0)
    // Måste komma FÖRST: den etablerar att fixturen över huvud taget ritar
    // det den påstår. Fallerar den är alla andra utfall meningslösa.
    Bild rak;
    if (!rendera(0.0f, rak)) {
        std::printf("  FEL  kunde inte rendera fixturen (radie 0) — ingen "
                    "headless-yta?\n");
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;   // ALDRIG skip — se filhuvudet
    }

    int rx0, ry0, rx1, ry1;
    if (!rödBox(rak, rx0, ry0, rx1, ry1)) {
        std::printf("  FEL  ingen röd yta i den raka körningen\n");
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;
    }
    int gx0, gy0, gx1, gy1;
    if (!grönBox(rak, gx0, gy0, gx1, gy1)) {
        std::printf("  FEL  ingen grön yta i den raka körningen\n");
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;
    }

    if (g_verbose)
        std::printf("  röd box %d,%d..%d,%d · grön box %d,%d..%d,%d "
                    "(bild %dx%d)\n",
                    rx0, ry0, rx1, ry1, gx0, gy0, gx1, gy1, rak.w, rak.h);

    // Ytorna ska vara ungefär panelens halvor — en sanity-koll som fäller
    // en fixtur som råkat rita någon annanstans.
    check(rx1 - rx0 > 100 && ry1 - ry0 > 40,
          "fixtur: den röda ytan har rimlig storlek",
          std::to_string(rx1 - rx0) + "x" + std::to_string(ry1 - ry0));
    check(gx0 == rx0 && gx1 == rx1,
          "fixtur: grön och röd yta delar vänster- och högerkant");

    // Kontrollen: med radie 0 ÄR hörnen färgade — i BÅDA shader-vägarna.
    check(ärRöd(rak.at(rx0 + kProbe, ry0 + kProbe)),
          "radie 0: mode 1-hörnet (uppe vänster) är färgat",
          punkt(rak, rx0 + kProbe, ry0 + kProbe));
    check(ärRöd(rak.at(rx1 - kProbe, ry0 + kProbe)),
          "radie 0: mode 1-hörnet (uppe höger) är färgat",
          punkt(rak, rx1 - kProbe, ry0 + kProbe));
    check(ärGrön(rak.at(gx0 + kProbe, gy1 - kProbe)),
          "radie 0: mode 0-hörnet (nere vänster) är färgat",
          punkt(rak, gx0 + kProbe, gy1 - kProbe));
    check(ärGrön(rak.at(gx1 - kProbe, gy1 - kProbe)),
          "radie 0: mode 0-hörnet (nere höger) är färgat",
          punkt(rak, gx1 - kProbe, gy1 - kProbe));

    // -------------------------------------------------- den rundade (r>0)
    Bild rund;
    if (!rendera(kRadie, rund)) {
        std::printf("  FEL  kunde inte rendera fixturen (radie %g)\n", kRadie);
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;
    }
    check(rund.w == rak.w && rund.h == rak.h,
          "de två körningarna har samma bildstorlek");

    if (!pngUt.empty()) {
        if (gfx::encodePng(pngUt, rund.px, rund.w, rund.h))
            std::printf("  skrev %s (%dx%d) — rundad fixtur att ögna\n",
                        pngUt.c_str(), rund.w, rund.h);
        else
            std::printf("  VARNING: kunde inte skriva %s\n", pngUt.c_str());
    }

    // Bounding boxen är OFÖRÄNDRAD — rundningen kapar hörnen, aldrig
    // kantens mitt. Att mäta om den är samtidigt beviset för det.
    int nx0, ny0, nx1, ny1;
    check(rödBox(rund, nx0, ny0, nx1, ny1) && nx0 == rx0 && ny0 == ry0 &&
              nx1 == rx1 && ny1 == ry1,
          "rundat: röda ytans bounding box är oförändrad (kantmitten står "
          "kvar)");

    // KANTMITTEN och CENTRUM står kvar — masken kapar hörn, inte yta.
    const int midY = (ry0 + ry1) / 2;
    check(ärRöd(rund.at(rx0 + kProbe, midY)),
          "rundat: vänsterkantens mitt är fortfarande färgad",
          punkt(rund, rx0 + kProbe, midY));
    check(ärRöd(rund.at((rx0 + rx1) / 2, ry0 + kProbe)),
          "rundat: överkantens mitt är fortfarande färgad",
          punkt(rund, (rx0 + rx1) / 2, ry0 + kProbe));

    // ---- HUVUDPÅSTÅENDET ------------------------------------------------
    // Grinden mäter beteendet att hörnet MASKAS BORT: barnet är
    // bortmaskat i containerns hörn, i båda shader-vägarna.
    check(!ärRöd(rund.at(rx0 + kProbe, ry0 + kProbe)),
          "RUNDAT: mode 1-hörnet uppe vänster är bortmaskat",
          punkt(rund, rx0 + kProbe, ry0 + kProbe));
    check(!ärRöd(rund.at(rx1 - kProbe, ry0 + kProbe)),
          "RUNDAT: mode 1-hörnet uppe höger är bortmaskat",
          punkt(rund, rx1 - kProbe, ry0 + kProbe));
    check(!ärGrön(rund.at(gx0 + kProbe, gy1 - kProbe)),
          "RUNDAT: mode 0-hörnet nere vänster är bortmaskat",
          punkt(rund, gx0 + kProbe, gy1 - kProbe));
    check(!ärGrön(rund.at(gx1 - kProbe, gy1 - kProbe)),
          "RUNDAT: mode 0-hörnet nere höger är bortmaskat",
          punkt(rund, gx1 - kProbe, gy1 - kProbe));

    // ---- NÄSTLING -------------------------------------------------------
    // Yttre panel RUNDAD, inre RAK. Mäter att en rektangulär nivå INTE
    // nollställer den yttre masken — alltså `applyTop()`s urval ur stacken,
    // som annars är helt omätt i pixlar.
    Bild nästKontroll, nästRund;
    if (!renderaNästlad(0.0f, nästKontroll) ||
        !renderaNästlad(kRadie, nästRund)) {
        std::printf("  FEL  kunde inte rendera den nästlade fixturen\n");
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;
    }

    int kx0, ky0, kx1, ky1;
    if (!rödBox(nästKontroll, kx0, ky0, kx1, ky1)) {
        std::printf("  FEL  ingen röd yta i den nästlade kontrollen\n");
        std::printf("=== clip_gate: FALLERAR ===\n");
        return 1;
    }
    // Kontrollen först — annars är påståendet nedan en tautologi.
    check(ärRöd(nästKontroll.at(kx0 + kProbe, ky0 + kProbe)),
          "nästlat, båda raka: hörnet är färgat",
          punkt(nästKontroll, kx0 + kProbe, ky0 + kProbe));

    int ex0, ey0, ex1, ey1;
    check(rödBox(nästRund, ex0, ey0, ex1, ey1) && ex0 == kx0 && ey0 == ky0 &&
              ex1 == kx1 && ey1 == ky1,
          "nästlat: bounding boxen är oförändrad av yttre radien");
    check(!ärRöd(nästRund.at(kx0 + kProbe, ky0 + kProbe)),
          "NÄSTLAT: YTTRE panelens radie maskar barnet trots RAK inre nivå",
          punkt(nästRund, kx0 + kProbe, ky0 + kProbe));
    check(ärRöd(nästRund.at(kx0 + kProbe, (ky0 + ky1) / 2)),
          "nästlat: vänsterkantens mitt står kvar",
          punkt(nästRund, kx0 + kProbe, (ky0 + ky1) / 2));

    std::printf(g_fel ? "=== clip_gate: %d FEL ===\n"
                      : "=== clip_gate: allt grönt (%d fel) ===\n",
                g_fel);
    return g_fel ? 1 : 0;
}
