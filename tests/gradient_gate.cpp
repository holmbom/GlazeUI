// gradient_gate — FÖNSTRETS GRADIENT.
//
// Kör:  ./build/tests/gradient_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = bilddelen kunde inte
//       köras (ingen display)
//
// HEADLESS:
//   G1 ett hörn är en färg och en styrka mot `windowBg`, eller temats accent
//   G2 av som förval; på så snart något hörn har en styrka, i något läge
//   G3 `Renderer::fillRectCorners`: rutnätet täcker ytan utan glipor, hörnen är
//      de angivna och mitten det bilinjära medelvärdet
//   G4 appens gradient ärvs, ett fönsters egen går före — också en tom, som
//      stänger av — och `std::nullopt` följer appens igen
//   G5 den platta formen ger samma tillstånd som aggregatet
// I BILD (dolt fönster, `renderFrame`/`captureFrame`):
//   B1 hörnen syns, och en dold eller synlig statusrad släpper igenom gradienten
//   B2 mörkt läge tar de mörka hörnen, och ett mörkt hörn som inte är satt är
//      bakgrunden
//   B3 en partiell omritning ger samma bild som en full, utan skarvar
//
// SABOTAGEN den ska fälla: `fillRectCorners` med två trianglar över hela ytan
// (G3 antalet och mitten), `Bar::fillBody` utan gradientvakten (B1 statusraden),
// mörkt läge som läser de ljusa hörnen (B2), partiell omritning utan
// `drawWindowGradient` (B3), `windowGradient` som bara läser appens (G4).
//
// MÄTER INTE: gradienten på Metal och Linux (doc/os), rundade fönster (Linux)
// och dockningens remsor.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& detalj = {})
{
    ++g_fall;
    if (!ok) {
        ++g_fel;
        std::printf("  FEL  %s%s%s\n", vad.c_str(), detalj.empty() ? "" : " — ",
                    detalj.c_str());
    } else if (g_verbose) {
        std::printf("  OK   %s\n", vad.c_str());
    }
}

bool nära(int a, int b, int tol = 2)
{
    return std::abs(a - b) <= tol;
}

std::string text(gfx::Color c)
{
    return std::to_string(c.r) + "," + std::to_string(c.g) + "," +
           std::to_string(c.b);
}

constexpr gfx::Color kRöd{255, 0, 0, 255};
constexpr gfx::Color kGrön{0, 255, 0, 255};
constexpr gfx::Color kBlå{0, 0, 255, 255};
constexpr gfx::Color kVit{255, 255, 255, 255};

void fallHörn()
{
    const glaze::Theme::Colors& c = glaze::Theme::slateDefault().dark.colors;
    const gfx::Color full = glaze::tint(kRöd).resolve(c);
    check(full == kRöd, "G1 styrkan 1 är färgen", text(full));
    const gfx::Color noll = glaze::tint(kRöd, 0.0f).resolve(c);
    check(noll == c.windowBg, "G1 styrkan 0 är bakgrunden", text(noll));
    const gfx::Color halv = glaze::tint(kRöd, 0.5f).resolve(c);
    check(nära(halv.r, (c.windowBg.r + 255) / 2) && nära(halv.g, c.windowBg.g / 2),
          "G1 halva styrkan ligger mitt emellan", text(halv));
    const gfx::Color acc = glaze::tintAccent(1.0f).resolve(c);
    check(acc == c.accent, "G1 tintAccent är temats accent", text(acc));

    glaze::WindowGradient g;
    check(!g.active(), "G2 av som förval");
    g.topRight = glaze::tint(kRöd, 0.1f);
    check(g.active(), "G2 ett ljust hörn slår på den");
    glaze::WindowGradient m;
    m.bottomLeftDark = glaze::tintAccent(0.1f);
    check(m.active(), "G2 ett mörkt hörn slår på den");
}

struct Ritare final : gfx::NullRenderer {
    struct Quad {
        gfx::Rectf r;
        gfx::Color tl, tr, br, bl;
    };
    std::vector<Quad> quads;
    void fillQuadCorners(const gfx::Rectf& r, gfx::Color tl, gfx::Color tr,
                         gfx::Color br, gfx::Color bl) override
    {
        quads.push_back({r, tl, tr, br, bl});
    }
};

void fallRutnät()
{
    Ritare r;
    r.fillRectCorners({10.0f, 20.0f, 160.0f, 320.0f}, kRöd, kGrön, kBlå, kVit);
    check(r.quads.size() == 256, "G3 rutnätet har 16 × 16 quadar",
          std::to_string(r.quads.size()));
    if (r.quads.size() != 256)
        return;
    const auto& först = r.quads.front();
    const auto& sist  = r.quads.back();
    check(först.tl == kRöd, "G3 övre vänstra hörnet", text(först.tl));
    check(sist.br == kBlå, "G3 nedre högra hörnet", text(sist.br));
    check(r.quads[15].tr == kGrön, "G3 övre högra hörnet", text(r.quads[15].tr));
    check(r.quads[240].bl == kVit, "G3 nedre vänstra hörnet", text(r.quads[240].bl));
    check(först.r.x == 10.0f && först.r.y == 20.0f && först.r.w == 10.0f &&
              först.r.h == 20.0f,
          "G3 första rutan börjar i rektangelns hörn");
    check(sist.r.x + sist.r.w == 170.0f && sist.r.y + sist.r.h == 340.0f,
          "G3 sista rutan slutar i rektangelns hörn");
    bool täta = true;
    for (std::size_t i = 1; i < 16; ++i)
        täta = täta && r.quads[i].r.x == r.quads[i - 1].r.x + r.quads[i - 1].r.w;
    check(täta, "G3 grannrutorna delar kant");
    // Mitten: medelvärdet av de fyra hörnen, (255+0+0+255)/4 = 127.5 i rött.
    const gfx::Color mitt = r.quads[136].tl;
    check(nära(mitt.r, 127) && nära(mitt.g, 127) && nära(mitt.b, 127),
          "G3 mitten är det bilinjära medelvärdet", text(mitt));
}

struct Fönster : glaze::AppWindow {
    glaze::Button knapp{"Knapp"};
    Fönster() : glaze::AppWindow("gradient_gate", 320, 240)
    {
        knapp.name = "knapp";
        knapp.x    = 100;
        knapp.y    = 100;
        root().adopt(knapp);
    }
};

void fallÄrvning()
{
    Fönster w;
    glaze::WindowGradient app;
    app.topLeft = glaze::tint(kRöd);
    glaze::app().setWindowGradient(app);
    check(w.windowGradient().active(), "G4 fönstret ärver appens");

    glaze::WindowGradient egen;
    egen.topLeft = glaze::tint(kBlå);
    w.setWindowGradient(egen);
    check(w.windowGradient().topLeft.color == kBlå, "G4 en egen går före appens");

    w.setWindowGradient(glaze::WindowGradient{});
    check(!w.windowGradient().active(), "G4 en tom egen stänger av den här");

    w.setWindowGradient(std::nullopt);
    check(w.windowGradient().topLeft.color == kRöd, "G4 nullopt följer appens igen");
    glaze::app().setWindowGradient(glaze::WindowGradient{});
    check(!w.windowGradient().active(), "G4 appens gradient går att stänga av");

    Fönster platt;
    platt.setGradientCorner(glaze::WindowCorner::TopLeft, kRöd);
    platt.setGradientCorner(glaze::WindowCorner::TopLeftDark, kRöd, 0.25f);
    platt.setGradientAccent(glaze::WindowCorner::BottomRight, 0.08f);
    const glaze::WindowGradient& p = platt.windowGradient();
    check(p.topLeft.color == kRöd && p.topLeft.strength == 1.0f &&
              p.topLeftDark.strength == 0.25f && !p.bottomRight.color &&
              p.bottomRight.strength == 0.08f && p.topRight.strength == 0.0f,
          "G5 den platta formen ger aggregatets tillstånd");
}

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    gfx::Color at(int x, int y) const
    {
        const std::size_t i =
            (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
        return gfx::Color{px[i], px[i + 1], px[i + 2], px[i + 3]};
    }
};

// Provpunktens avstånd från sidokanten, tre rader över nederkanten. Ett
// RUNDAT fönster (Linux) har hörnen bortklippta — genomskinligt 0,0,0 — så
// punkten flyttas in förbi bågen på den raden; utan radie (Windows, macOS)
// är svaret 2, som förut.
int inåt(Fönster& f)
{
    const float r = f.cornerRadius();
    if (r <= 3.0f)
        return 2;
    const float d = r - 3.0f;   // radens avstånd från bågens medelpunkt
    return static_cast<int>(std::ceil(r - std::sqrt(r * r - d * d))) + 2;
}

bool rendera(Fönster& f, Bild& b)
{
    f.window().renderFrame();
    return f.window().captureFrame(b.px, b.w, b.h);
}

int fallBild()
{
    Fönster f;
    f.setUI(glaze::UI::Slate);
    f.setThemeMode(glaze::ThemeMode::Light);
    glaze::WindowGradient g;
    g.bottomLeft  = glaze::tint(kRöd);
    g.bottomRight = glaze::tint(kVit);
    f.setWindowGradient(g);
    f.setOffscreen(true);
    f.show();
    if (!f.isOpen()) {
        std::printf("  SKIP bilddelen: fönstret kunde inte skapas\n");
        return 77;
    }

    Bild b;
    if (!rendera(f, b)) {
        check(false, "B1 captureFrame lyckades");
        return 0;
    }
    const gfx::Color vänster = b.at(inåt(f), b.h - 3);
    const gfx::Color höger   = b.at(b.w - 1 - inåt(f), b.h - 3);
    check(vänster.r > 230 && vänster.g < 40, "B1 det röda hörnet syns", text(vänster));
    check(höger.r > 240 && höger.g > 240, "B1 det vita hörnet syns", text(höger));

    f.statusBar.show();
    f.statusBar.addPanel("Klar", 0, true);
    if (!rendera(f, b)) {
        check(false, "B1 captureFrame lyckades (statusrad)");
        return 0;
    }
    const gfx::Color rad = b.at(inåt(f), b.h - 3);
    check(rad.r > 230 && rad.g < 40, "B1 statusraden släpper igenom gradienten",
          text(rad));

    // PARTIELL OMRITNING: bara knappen ritas om, och bilden ska vara densamma.
    Bild full;
    f.invalidate();
    rendera(f, full);
    f.knapp.invalidate();
    Bild delvis;
    rendera(f, delvis);
    bool lika = full.w == delvis.w && full.h == delvis.h;
    for (std::size_t i = 0; lika && i < full.px.size(); ++i)
        lika = std::abs(int(full.px[i]) - int(delvis.px[i])) <= 1;
    check(lika, "B3 en partiell omritning ger samma bild som en full");

    f.setThemeMode(glaze::ThemeMode::Dark);
    f.invalidate();
    if (!rendera(f, b)) {
        check(false, "B2 captureFrame lyckades");
        return 0;
    }
    const gfx::Color bg = f.theme().dark.colors.windowBg;
    const gfx::Color m1 = b.at(inåt(f), b.h - 3);
    check(nära(m1.r, bg.r, 4) && nära(m1.g, bg.g, 4),
          "B2 mörkt hörn som inte är satt: bakgrunden, inte det ljusa", text(m1));

    g.bottomLeftDark = glaze::tint(kGrön, 0.5f);
    f.setWindowGradient(g);
    f.invalidate();
    if (!rendera(f, b)) {
        check(false, "B2 captureFrame lyckades (mörkt hörn)");
        return 0;
    }
    const gfx::Color m2 = b.at(inåt(f), b.h - 3);
    check(m2.g > 100 && m2.r < 60, "B2 mörkt läge tar de mörka hörnen", text(m2));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("=== gradient_gate — fönstrets gradient ===\n");
    fallHörn();
    fallRutnät();
    fallÄrvning();
    const int skip = fallBild();
    std::printf("=== gradient_gate: %d FEL av %d ===\n", g_fel, g_fall);
    if (g_fel != 0)
        return 1;
    return skip;
}
