// stockicon_gate — STANDARDIKONERNAS grind: att bilderna är pixelperfekta och
// syns mot ytan de står på.
//
// Kör:  ./build-test/tests/stockicon_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Headless — inga fönster, ingen pump, ingen `gfx::init`. Bilderna rastreras
// genom `detail::stockBitmap`, samma väg som `Draw::glyph` och fönstrets
// bildcache tar, i palettens färger.
//
//   K1  KANTEN MOT BAKGRUNDEN: en täckande pixel (alfa ≥ hälften) med en
//       granne som inte är det har en SYNLIG GRÄNS — den når 3:1 mot
//       `windowBg`, `panel` och `titlebar`, eller mot en täckande granne (där
//       ytan har vuxit en ring), eller är en mellanton mot en granne som når
//       det — för minst tre fjärdedelar av kantpixlarna i varje ikon: motorns
//       löfte (`kKantAndel`), och resten tar ringen rakt utanför lagret eller,
//       vid bildens kant, pixeln själv. En ikon med ett lager med `contrast 2`
//       (den bakre sidan i Copy och Duplicate, den bakre disketten, det
//       dockade fönstret och de bakre rutorna i ordna-ikonerna) mäts mot sitt
//       krav, 2:1. K1a: varje ikon i 16 och 20 px, mono och färg,
//       ljust och mörkt, i varje ritnings förvalda tema. K1b: ett urval ikoner
//       (ytor, linjer i färg, utstansad etikett, rundade och sneda former) i 20
//       och 32 px i VARJE tema.
//   K2  YTAN ÄR DENSAMMA I BÅDA LÄGENA: mappens framsida i färg, en pixel
//       mitt i den, är samma i ljust och mörkt läge — bara kanten följer
//       bakgrunden.
//   K3  PIXELFÄSTNINGEN: ett vågrätt streck (`Remove`) i 16, 20, 24, 30, 32
//       och 40 px är exakt ett helt antal rader — 1 px under 32, 2 px från 32
//       — där varje pixel är helt täckande eller helt genomskinlig: inga
//       kantutjämnade rader och inga runda ändar.
//   K4  DET SKURNA HÖRNET: en rundad rektangel (`Rectangle`) i 20 och 40 px
//       har sin yttersta hörnpixel genomskinlig — ringen växer inte in i den —
//       och grannarna längs kanterna täckande, i alla fyra hörn.
//   K5  MONO ÄR NEDTONAD FÄRG: stoppskyltens yta i mono är röd, och ljusare
//       än i färg.
//   K6  CACHEN NYCKLAS PÅ YTORNA: två paletter som bara skiljer i `panel`
//       ger olika bilder när kanten måste rättas mot panelen.
//   K7  TECKNET MOT YTAN: pilen på Backs skiva når 3:1 mot skivan intill, i
//       ljust och mörkt läge.
//   K8  DEN TUNNA LINJEN: Ellipse i 20 px i mörkt läge — minst tre fjärdedelar
//       av de täckande pixlarna når textens 4,5:1 mot fönstrets bakgrund.
//   K9  BOCKEN (`Draw::check`) mot en inspelande renderer, i Slates
//       `checkStroke`, vid skala 1 och 1,5 i en ruta som börjar mitt i en
//       pixel: inga linjer, bara hela pixlar, och varje rad utom den översta
//       och den nedersta har en pixel i pennans fulla färg — vid skala 1 också
//       lika mycket luft på båda sidor.
//   K10 STRECKEN (`Draw::minus`, `Draw::plus`) i samma ruta vid skala 1,25:
//       strecket är EN rektangel på hela pixelrader med pennan avrundad till
//       hela px, och plusets tre delar täcker ingen pixel två gånger.
//   K11 APPENS IKON ÄR STANDARDIKONEN: `registerIcon("ProvFil", "@fil",
//       "#fil")` ritas pixel för pixel som `StockIcon::File`, mono och färg,
//       ljust och mörkt.
//   K12 AUTO: `forState` ger Color aktiv och Mono inaktiv, och utan tillstånd
//       ritas Auto som Color.
//   K13 OMREGISTRERINGEN ersätter receptet (cachen töms), ett okänt namn ritas
//       som `StockIcon::Image`, och tomt namn eller tom kontur avvisas.
//   K14 APPENS DEL (`registerIconPart`) i `@` och `#` ger samma bild som
//       ramverkets, och `drawnIconNames` står i registreringsordning.
//   K15 FELET LOGGAS EN GÅNG PER IKON: en appikon med en trasig beskrivning,
//       rastrerad i två storlekar och båda stilarna, står EN gång bland de
//       loggade namnen (`detail::iconErrorNames`) — under sitt namn.
//   K16 RINGEN VÄXER: en vit skiva på en vit yta (en appikon) kan inte bli
//       ljusare, så ytan rakt utanför den mörkas tills de skiljer sig 3:1.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · stock_icon.cpp: kKantKontrast 2 i stället för 3 ................ K1 fäller
//   · stock_icon.cpp: läggPå utan ringen som växer .................. K16 fäller
//   · stock_icon.cpp: `appen` utan `p.titlebar` ...................... K1 fäller
//   · stock_icon.cpp: `krav = max(krav, kLinjeKontrast)` borttaget ... K8 fäller
//   · stock_icon.cpp: Avbildning::fäst med `mitt` alltid 0 ........... K3 fäller
//   · stock_icon.cpp: iStreck med `rund` alltid sann (runda ändar) .... K3 fäller
//   · stock_icon.cpp: ikonStreck alltid 1 ........................... K3 fäller
//   · stock_icon.cpp: det skurna hörnet borttaget i `inne` ............ K4 fäller
//   · theme.cpp: stockYellow med lägets ljushet ...................... K2 fäller
//   · stock_icon.cpp: mono utan färgens lager ........................ K5 fäller
//   · stock_icon.cpp: cache-nyckeln utan `colors.panel` ............. K6 fäller
//   · draw.cpp: Draw::check med två `Draw::line` igen ................ K9 fäller
//   · style.cpp: checkStroke 1 ...................................... K9 fäller
//   · draw.cpp: helaPx utan avrundningen ........................... K10 fäller
//   · draw.cpp: Draw::minus med y0 = p.y + (p.h − t) / 2 ............ K10 fäller
//   · draw.cpp: Draw::plus med det stående strecket i ett stycke .... K10 fäller
//   · stock_icon.cpp: drawnBitmap utan läggTillLager (bara konturen) K11 fäller
//   · shape.cpp: forState svarar alltid med sig själv ............... K12 fäller
//   · stock_icon.cpp: registrera utan appCache().clear() ............ K13 fäller
//   · stock_icon.cpp: delKontur utan appDelar() ..................... K14 fäller
//   · stock_icon.cpp: läggPå med ringen också på diagonalen ......... K4 fäller
//   · stock_icon.cpp: förstaFelet svarar alltid true ................ K15 fäller
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · att ikonerna SER BRA UT — det är ögats sak, på arket (`glyph_sheet`).
//   · kontrasten mot en markerad rad, en platta under pekaren eller en
//     appens egen yta — ikonen känner bara palettens tre ytor.
//   · den sammansatta kontrasten för en svagt täckande kantpixel: en pixel
//     som är till 70 % genomskinlig visar mest bakgrunden och kan inte nå 3:1
//     vilken färg den än har; kravet är dess egen färg.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <glaze/glaze.h>

#include "image_cache.h"   // detail::stockBitmap

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s — fick \"%s\"\n", what.c_str(), fick.c_str());
    ++g_fails;
}

constexpr float kKrav = 3.0f;   // WCAG:s krav på grafik

const char* uiNamn(glaze::UI ui)
{
    switch (ui) {
    case glaze::UI::Glaze:        return "Glaze";
    case glaze::UI::Slate:        return "Slate";
    case glaze::UI::Minimalistic: return "Minimalistic";
    }
    return "?";
}

std::string ikonNamn(glaze::StockIcon i)
{
    return std::string(glaze::stockIconName(i));
}

struct Pixel {
    std::uint8_t r, g, b, a;
};

Pixel pixel(const glaze::Bitmap& b, int x, int y)
{
    if (x < 0 || y < 0 || x >= b.width() || y >= b.height())
        return {0, 0, 0, 0};
    const std::uint8_t* p =
        b.pixels() + (static_cast<std::size_t>(y) * b.width() + x) * 4;
    return {p[0], p[1], p[2], p[3]};
}

// En kantpixel: kantutjämnad, eller täckande med en otäckt granne.
bool ärKant(const glaze::Bitmap& b, int x, int y)
{
    const int a = pixel(b, x, y).a;
    return a > 0 && (a < 255 || pixel(b, x - 1, y).a < 128 ||
                     pixel(b, x + 1, y).a < 128 || pixel(b, x, y - 1).a < 128 ||
                     pixel(b, x, y + 1).a < 128);
}

// K1: en täckande pixel med en otäckt granne har en SYNLIG GRÄNS inom en
// pixel — motorns regel (`läggPå`): den når 3:1 mot palettens tre ytor; eller
// ytan har vuxit en ring, och pixeln når 3:1 mot en täckande granne (av åtta);
// eller den är en MELLANTON — dess ljushet ligger mellan bakgrundens och en
// täckande grannes, och den grannen når 3:1 mot ytorna (kantutjämningen mot
// en kant som bär kontrasten, och en ringpixel i bakgrundens egen färg).
// Svarar antalet som föll mot `krav`, räknar kantpixlarna i `alla` och lämnar
// den sämsta och dess plats.
int kantFel(const glaze::Bitmap& b, const glaze::Theme::Colors& p, float krav,
            float& sämst, int& sx, int& sy, int& alla)
{
    const gfx::Color ytor[] = {p.windowBg, p.panel, p.titlebar};
    const auto       färg   = [&](int x, int y) {
        const Pixel q = pixel(b, x, y);
        return gfx::Color{q.r, q.g, q.b, 255};
    };
    const auto motYtor = [&](gfx::Color c) {
        float k = 100.0f;
        for (const gfx::Color yta : ytor)
            k = std::min(k, glaze::color::contrast(c, yta));
        return k;
    };
    const float bgL = glaze::color::luminance(p.windowBg);
    int         fel = 0;
    alla            = 0;
    sämst           = 100.0f;
    for (int y = 0; y < b.height(); ++y)
        for (int x = 0; x < b.width(); ++x) {
            if (pixel(b, x, y).a < 128)
                continue;
            if (pixel(b, x - 1, y).a >= 128 && pixel(b, x + 1, y).a >= 128 &&
                pixel(b, x, y - 1).a >= 128 && pixel(b, x, y + 1).a >= 128)
                continue;   // ingen otäckt granne
            ++alla;
            const gfx::Color c = färg(x, y);
            const float      cL = glaze::color::luminance(c);
            float            k  = motYtor(c);
            for (int dy = -1; dy <= 1 && k < krav; ++dy)
                for (int dx = -1; dx <= 1 && k < krav; ++dx) {
                    if ((!dx && !dy) || pixel(b, x + dx, y + dy).a < 128)
                        continue;
                    const gfx::Color g  = färg(x + dx, y + dy);
                    const float      gL = glaze::color::luminance(g);
                    k = std::max(k, glaze::color::contrast(c, g));
                    if (motYtor(g) >= krav && cL >= std::min(bgL, gL) - 1e-4f &&
                        cL <= std::max(bgL, gL) + 1e-4f)
                        k = std::max(k, motYtor(g));
                }
            if (k < sämst) {
                sämst = k;
                sx    = x;
                sy    = y;
            }
            fel += k < krav - 1e-3f ? 1 : 0;
        }
    return fel;
}

// Ikonens krav: 3:1, eller 2:1 för en ikon med ett lager med `contrast 2`.
float kravFör(glaze::StockIcon ikon)
{
    using I = glaze::StockIcon;
    switch (ikon) {
    case I::Copy:
    case I::Duplicate:
    case I::SaveAll:
    case I::Float:
    case I::BringToFront:
    case I::SendToBack:
    case I::BringForward:
    case I::SendBackward:
        return 2.0f;
    default:
        return kKrav;
    }
}

void kantFall(glaze::StockIcon ikon, int px, glaze::IconStyle stil,
              const glaze::Theme::Colors& p, const std::string& var)
{
    const glaze::Bitmap b    = glaze::detail::stockBitmap(ikon, px, stil, p);
    const float         krav = kravFör(ikon);
    float               sämst = 0.0f;
    int                 sx = -1, sy = -1, alla = 0;
    const int fel = b.valid() ? kantFel(b, p, krav, sämst, sx, sy, alla) : -1;
    char      buf[112];
    std::snprintf(buf, sizeof buf, "%d av %d kantpixlar under %.0f:1, sämst %.2f:1 i (%d, %d)",
                  fel, alla, static_cast<double>(krav), static_cast<double>(sämst), sx, sy);
    // Motorns löfte: det gemensamma steget når kravet för `kKantAndel` (tre
    // fjärdedelar) av ytterpixlarna, och ringen tar så många av resten den
    // får plats med.
    check(fel >= 0 && alla > 0 && fel * 4 <= alla,
          "K1 " + var + " " + ikonNamn(ikon) + " " + std::to_string(px) + " px " +
              (stil == glaze::IconStyle::Color ? "färg" : "mono"),
          buf);
}

void k1()
{
    constexpr glaze::UI kUI[] = {glaze::UI::Glaze, glaze::UI::Slate,
                                 glaze::UI::Minimalistic};
    constexpr glaze::IconStyle kStilar[] = {glaze::IconStyle::Mono,
                                                 glaze::IconStyle::Color};
    using I = glaze::StockIcon;
    // K1a — varje ikon, de små storlekarna, varje ritnings förvalda tema.
    for (const glaze::UI ui : kUI) {
        const glaze::Theme& t = glaze::defaultTheme(ui);
        for (int läge = 0; läge < 2; ++läge) {
            const auto& p   = läge == 0 ? t.light.colors : t.dark.colors;
            const std::string var =
                std::string(uiNamn(ui)) + "/" + t.name + (läge == 0 ? " ljust" : " mörkt");
            for (int i = 0; i < glaze::stockIconCount(); ++i)
                for (const int px : {16, 20})
                    for (const glaze::IconStyle s : kStilar)
                        kantFall(static_cast<I>(i), px, s, p, var);
        }
    }
    // K1b — ett urval i varje tema.
    constexpr I kUrval[] = {I::Folder, I::File,      I::Document, I::Save,
                            I::Stop,   I::Home,      I::Print,    I::ArrowLeft,
                            I::Import, I::ThemeDark, I::FilePdf,  I::Search};
    for (const glaze::UI ui : kUI)
        for (int ti = 0; ti < glaze::themeCount(ui); ++ti) {
            const glaze::Theme& t = glaze::themeAt(ui, ti);
            for (int läge = 0; läge < 2; ++läge) {
                const auto& p = läge == 0 ? t.light.colors : t.dark.colors;
                const std::string var = std::string(uiNamn(ui)) + "/" + t.name +
                                        (läge == 0 ? " ljust" : " mörkt");
                for (const I ikon : kUrval)
                    for (const int px : {20, 32})
                        for (const glaze::IconStyle s : kStilar)
                            kantFall(ikon, px, s, p, var);
            }
        }
}

void k2()
{
    const glaze::Theme& t = glaze::defaultTheme(glaze::UI::Slate);
    const auto ljus  = glaze::detail::stockBitmap(glaze::StockIcon::Folder, 32,
                                                  glaze::IconStyle::Color,
                                                  t.light.colors);
    const auto mörk  = glaze::detail::stockBitmap(glaze::StockIcon::Folder, 32,
                                                  glaze::IconStyle::Color,
                                                  t.dark.colors);
    // Mitt i framsidan: rutans (12, 16) i en bild på 32 — långt från kanten.
    const Pixel a = pixel(ljus, 16, 21), b = pixel(mörk, 16, 21);
    check(a.a == 255 && !ärKant(ljus, 16, 21),
          "K2 facit — pixeln ligger inne i mappens yta",
          std::to_string(a.a));
    check(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a,
          "K2 mappens yta är densamma i ljust och mörkt läge",
          std::to_string(a.r) + "," + std::to_string(a.g) + "," + std::to_string(a.b) +
              " mot " + std::to_string(b.r) + "," + std::to_string(b.g) + "," +
              std::to_string(b.b));
}

void k3()
{
    const auto& p = glaze::defaultTheme(glaze::UI::Slate).light.colors;
    for (const int px : {16, 20, 24, 30, 32, 40}) {
        const glaze::Bitmap b = glaze::detail::stockBitmap(
            glaze::StockIcon::Remove, px, glaze::IconStyle::Mono, p);
        int  rader   = 0;   // rader med något täckt
        bool hela    = true;
        for (int y = 0; y < b.height(); ++y) {
            bool något = false;
            for (int x = 0; x < b.width(); ++x) {
                const int a = pixel(b, x, y).a;
                if (a != 0 && a != 255)
                    hela = false;
                if (a != 0)
                    något = true;
            }
            rader += något ? 1 : 0;
        }
        const int väntat = px < 32 ? 1 : 2;
        check(rader == väntat && hela,
              "K3 Remove i " + std::to_string(px) + " px: " + std::to_string(väntat) +
                  " hela rader, inga halvtäckta pixlar",
              std::to_string(rader) + " rader" + (hela ? "" : ", halvtäckta pixlar"));
    }
}

void k4()
{
    const auto& p = glaze::defaultTheme(glaze::UI::Slate).light.colors;
    for (const int px : {20, 40}) {
        const glaze::Bitmap b = glaze::detail::stockBitmap(
            glaze::StockIcon::Rectangle, px, glaze::IconStyle::Mono, p);
        int x0 = b.width(), y0 = b.height(), x1 = -1, y1 = -1;
        for (int y = 0; y < b.height(); ++y)
            for (int x = 0; x < b.width(); ++x)
                if (pixel(b, x, y).a > 0) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
        bool ok = x1 > x0 + 2 && y1 > y0 + 2;
        for (const auto [hx, hy, dx, dy] :
             {std::array<int, 4>{x0, y0, 1, 1}, std::array<int, 4>{x1, y0, -1, 1},
              std::array<int, 4>{x0, y1, 1, -1}, std::array<int, 4>{x1, y1, -1, -1}})
            ok = ok && pixel(b, hx, hy).a == 0 && pixel(b, hx + dx, hy).a == 255 &&
                 pixel(b, hx, hy + dy).a == 255;
        check(ok, "K4 Rectangle i " + std::to_string(px) +
                      " px: hörnpixlarna genomskinliga, kanterna hela");
    }
}

void k5()
{
    const auto& p    = glaze::defaultTheme(glaze::UI::Slate).light.colors;
    const auto  mono = glaze::detail::stockBitmap(glaze::StockIcon::Stop, 32,
                                                  glaze::IconStyle::Mono, p);
    const auto  färg = glaze::detail::stockBitmap(glaze::StockIcon::Stop, 32,
                                                  glaze::IconStyle::Color, p);
    // Rutans (12, 6) — inne i åttahörningen, ovanför krysset.
    const Pixel m = pixel(mono, 16, 8), f = pixel(färg, 16, 8);
    const auto  lum = [](const Pixel& q) {
        return glaze::color::luminance(gfx::Color{q.r, q.g, q.b, 255});
    };
    check(m.a == 255 && m.r > m.g + 20 && m.r > m.b + 20,
          "K5 stoppskylten i mono har en röd yta",
          std::to_string(m.r) + "," + std::to_string(m.g) + "," + std::to_string(m.b));
    check(f.a == 255 && lum(m) > lum(f), "K5 monos yta är ljusare än färgens");
}

void k6()
{
    glaze::Theme::Colors a = glaze::defaultTheme(glaze::UI::Slate).light.colors;
    glaze::Theme::Colors b = a;
    b.panel                = gfx::Color{90, 90, 90, 255};   // grått: kanten måste rättas
    const auto ba = glaze::detail::stockBitmap(glaze::StockIcon::File, 20,
                                               glaze::IconStyle::Color, a);
    const auto bb = glaze::detail::stockBitmap(glaze::StockIcon::File, 20,
                                               glaze::IconStyle::Color, b);
    const std::size_t byte = static_cast<std::size_t>(ba.width()) * ba.height() * 4;
    check(ba.valid() && bb.valid() && byte > 0 &&
              std::memcmp(ba.pixels(), bb.pixels(), byte) != 0,
          "K6 en annan panel ger en annan bild");
}

void k7()
{
    const glaze::Theme& t = glaze::defaultTheme(glaze::UI::Slate);
    for (int läge = 0; läge < 2; ++läge) {
        const auto& p = läge == 0 ? t.light.colors : t.dark.colors;
        const auto  b = glaze::detail::stockBitmap(glaze::StockIcon::Back, 32,
                                                   glaze::IconStyle::Color, p);
        // Pilens skaft (rutans y 12 → raderna 15 och 16 i 32 px) och raden
        // ovanför det, där skivan eller dess ring ligger.
        const Pixel pil = pixel(b, 18, 15), intill = pixel(b, 18, 14);
        const float k   = glaze::color::contrast(gfx::Color{pil.r, pil.g, pil.b, 255},
                                                 gfx::Color{intill.r, intill.g, intill.b, 255});
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.2f:1", static_cast<double>(k));
        check(pil.a == 255 && intill.a == 255 && k >= kKrav - 1e-3f,
              std::string("K7 pilen på Back mot skivan intill, ") +
                  (läge == 0 ? "ljust" : "mörkt"),
              buf);
    }
}

void k8()
{
    const auto& p = glaze::defaultTheme(glaze::UI::Slate).dark.colors;
    const auto  b = glaze::detail::stockBitmap(glaze::StockIcon::Ellipse, 20,
                                               glaze::IconStyle::Color, p);
    int täckta = 0, når = 0;
    for (int y = 0; y < b.height(); ++y)
        for (int x = 0; x < b.width(); ++x) {
            const Pixel q = pixel(b, x, y);
            if (q.a != 255)
                continue;
            ++täckta;
            når += glaze::color::contrast(gfx::Color{q.r, q.g, q.b, 255}, p.windowBg) >= 4.5f
                       ? 1
                       : 0;
        }
    check(täckta > 0 && når * 4 >= täckta * 3,
          "K8 Ellipse i 20 px, mörkt: linjen når 4,5:1 mot bakgrunden",
          std::to_string(når) + " av " + std::to_string(täckta));
}

// Rektanglarna och linjerna en glyf-form ritar.
struct Inspelning final : gfx::NullRenderer {
    struct Ruta {
        gfx::Rectf r;
        gfx::Color c;
    };
    std::vector<Ruta> rutor;
    int               linjer = 0;
    void fillRect(const gfx::Rectf& r, gfx::Color c) override { rutor.push_back({r, c}); }
    void line(float, float, float, float, gfx::Color) override { ++linjer; }
};

bool helt(float v)
{
    return v == std::floor(v);
}

bool helaPixlar(const Inspelning& rec)
{
    for (const Inspelning::Ruta& r : rec.rutor)
        if (!helt(r.r.x) || !helt(r.r.y) || !helt(r.r.w) || !helt(r.r.h) ||
            r.r.w < 1.0f || r.r.h < 1.0f)
            return false;
    return true;
}

// En ruta på 14 logiska px vars hörn ligger mitt i en fysisk pixel.
glaze::Ctx glyfCtx(Inspelning& rec, float skala)
{
    glaze::Ctx c{};
    c.r     = &rec;
    c.scale = skala;
    c.ui    = glaze::UI::Slate;
    c.style = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme = &glaze::defaultTheme(glaze::UI::Slate).light;
    c.box   = gfx::Rectf{10.5f, 20.5f, 14.0f * skala, 14.0f * skala};
    return c;
}

void k9()
{
    const gfx::Color penna{40, 100, 200, 255};
    for (const float skala : {1.0f, 1.5f}) {
        Inspelning       rec;
        const glaze::Ctx c = glyfCtx(rec, skala);
        glaze::Draw::check(c, {0.0f, 0.0f, 14.0f, 14.0f},
                           glaze::Pen{penna, c.style->checkStroke});
        const std::string vid = " vid skala " + std::to_string(skala).substr(0, 3);
        check(rec.linjer == 0 && !rec.rutor.empty() && helaPixlar(rec),
              "K9 bocken är hela pixlar" + vid,
              std::to_string(rec.linjer) + " linjer, " +
                  std::to_string(rec.rutor.size()) + " rutor");
        int y0 = 1 << 20, y1 = -(1 << 20), x0 = 1 << 20, x1 = -(1 << 20);
        for (const Inspelning::Ruta& r : rec.rutor) {
            y0 = std::min(y0, static_cast<int>(r.r.y));
            y1 = std::max(y1, static_cast<int>(r.r.y));
            x0 = std::min(x0, static_cast<int>(r.r.x));
            x1 = std::max(x1, static_cast<int>(r.r.x + r.r.w) - 1);
        }
        int bleka = 0;
        for (int y = y0 + 1; y < y1; ++y) {
            bool full = false;
            for (const Inspelning::Ruta& r : rec.rutor)
                full = full || (static_cast<int>(r.r.y) == y && r.c.a == 255 &&
                                r.c.r == penna.r && r.c.g == penna.g &&
                                r.c.b == penna.b);
            bleka += full ? 0 : 1;
        }
        check(y1 - y0 >= 4 && bleka == 0,
              "K9 bocken har pennans fulla färg i varje inre rad" + vid,
              std::to_string(bleka) + " rader utan");
        if (skala == 1.0f) {
            // Rutan avrundas till pixel 11 (10,5 → 11) och är 14 px bred.
            const int vänster = x0 - 11, höger = 11 + 14 - 1 - x1;
            check(vänster == höger, "K9 bocken är centrerad vid skala 1",
                  std::to_string(vänster) + " mot " + std::to_string(höger));
        }
    }
}

void k10()
{
    const gfx::Color penna{40, 100, 200, 255};
    const float      skala = 1.25f;
    const float      t     = std::round(2.0f * skala);
    {
        Inspelning       rec;
        const glaze::Ctx c = glyfCtx(rec, skala);
        glaze::Draw::minus(c, {0.0f, 0.0f, 14.0f, 14.0f}, glaze::Pen{penna, 2.0f});
        check(rec.rutor.size() == 1 && helaPixlar(rec) && rec.rutor[0].r.h == t,
              "K10 minus är en rektangel på hela pixelrader",
              rec.rutor.empty() ? "inga" : std::to_string(rec.rutor[0].r.h));
    }
    {
        Inspelning       rec;
        const glaze::Ctx c = glyfCtx(rec, skala);
        glaze::Draw::plus(c, {0.0f, 0.0f, 14.0f, 14.0f}, glaze::Pen{penna, 2.0f});
        bool överlapp = false;
        for (std::size_t i = 0; i < rec.rutor.size(); ++i)
            for (std::size_t j = i + 1; j < rec.rutor.size(); ++j) {
                const gfx::Rectf& a = rec.rutor[i].r;
                const gfx::Rectf& b = rec.rutor[j].r;
                överlapp = överlapp || (a.x < b.x + b.w && b.x < a.x + a.w &&
                                        a.y < b.y + b.h && b.y < a.y + a.h);
            }
        check(rec.rutor.size() == 3 && helaPixlar(rec) && !överlapp,
              "K10 plus är tre delar på hela pixlar utan överlapp",
              std::to_string(rec.rutor.size()) + " rutor");
    }
}

bool likaBilder(const glaze::Bitmap& a, const glaze::Bitmap& b)
{
    if (!a.valid() || !b.valid() || a.width() != b.width() || a.height() != b.height())
        return false;
    const std::size_t n = static_cast<std::size_t>(a.width()) *
                          static_cast<std::size_t>(a.height()) * 4;
    return std::memcmp(a.pixels(), b.pixels(), n) == 0;
}

// K11–K14: APPENS IKONER går genom standardikonernas motor.
void k11()
{
    using glaze::IconStyle;
    const auto& tema = glaze::defaultTheme(glaze::UI::Slate);
    // K11: samma beskrivning ger samma bild, i båda stilarna och lägena.
    check(glaze::registerIcon("ProvFil", "@fil", "#fil"),
          "K11 registerIcon svarar true");
    for (const auto* p : {&tema.light.colors, &tema.dark.colors})
        for (const IconStyle s : {IconStyle::Mono, IconStyle::Color})
            check(likaBilder(glaze::detail::drawnBitmap("ProvFil", 20, s, *p),
                             glaze::detail::stockBitmap(glaze::StockIcon::File, 20, s, *p)),
                  std::string("K11 appens @fil/#fil är standardikonen File, ") +
                      (s == IconStyle::Color ? "färg" : "mono"));

    // K12: Auto är Color i aktivt tillstånd och utan tillstånd, Mono i inaktivt.
    const glaze::ImageSource autoIkon = glaze::drawnIcon("ProvFil", 20, IconStyle::Auto);
    check(autoIkon.forState(true).iconStyle() == IconStyle::Color &&
              autoIkon.forState(false).iconStyle() == IconStyle::Mono &&
              glaze::stockIcon(glaze::StockIcon::Save).forState(false).iconStyle() ==
                  IconStyle::Mono,
          "K12 forState: Auto blir Color aktiv och Mono inaktiv, Mono förblir Mono");
    check(likaBilder(glaze::detail::iconBitmap(autoIkon, 20, tema.light.colors),
                     glaze::detail::stockBitmap(glaze::StockIcon::File, 20,
                                                IconStyle::Color, tema.light.colors)),
          "K12 Auto utan tillstånd ritas som Color");

    // K13: ett nytt recept under samma namn ersätter det gamla (cachen töms),
    // och ett okänt namn ritas som StockIcon::Image.
    check(glaze::registerIcon("ProvFil", "@mapp", "#mapp"),
          "K13 omregistreringen svarar true");
    check(likaBilder(glaze::detail::drawnBitmap("ProvFil", 20, IconStyle::Color,
                                                tema.light.colors),
                     glaze::detail::stockBitmap(glaze::StockIcon::Folder, 20,
                                                IconStyle::Color, tema.light.colors)),
          "K13 det nya receptet ritas");
    check(likaBilder(glaze::detail::drawnBitmap("FinnsInte", 20, IconStyle::Color,
                                                tema.light.colors),
                     glaze::detail::stockBitmap(glaze::StockIcon::Image, 20,
                                                IconStyle::Color, tema.light.colors)),
          "K13 ett okänt namn ritas som StockIcon::Image");
    check(!glaze::registerIcon("", "@fil") && !glaze::registerIcon("Tom", ""),
          "K13 tomt namn och tom kontur registreras inte");

    // K14: appens DEL i konturen (`@`) och färgen (`#`), som ramverkets.
    check(glaze::registerIconPart("provdel", "M 14 2 H 4 V 22 H 20 V 8 Z M 14 2 V 8 H 20",
                                  "all deep paper gradient paper F @fil dark paper F @vikt"),
          "K14 registerIconPart svarar true");
    check(glaze::registerIcon("ProvDel", "@provdel", "#provdel"),
          "K14 en ikon med appens del registreras");
    check(likaBilder(glaze::detail::drawnBitmap("ProvDel", 24, IconStyle::Color,
                                                tema.dark.colors),
                     glaze::detail::stockBitmap(glaze::StockIcon::File, 24,
                                                IconStyle::Color, tema.dark.colors)),
          "K14 appens del ger samma bild som ramverkets");
    const std::vector<std::string> namn = glaze::drawnIconNames();
    check(namn.size() == 2 && namn[0] == "ProvFil" && namn[1] == "ProvDel",
          "K14 drawnIconNames i registreringsordning",
          std::to_string(namn.size()) + " namn");

    // K15: ett trasigt recept loggas EN gång, under ikonens namn — inte en gång
    // per storlek och stil.
    check(glaze::registerIcon("ProvFel", "M 3 3 X 4"), "K15 en trasig ikon registreras");
    for (const int px : {16, 24})
        for (const IconStyle s : {IconStyle::Mono, IconStyle::Color})
            (void)glaze::detail::drawnBitmap("ProvFel", px, s, tema.light.colors);
    const std::vector<std::string> fel = glaze::detail::iconErrorNames();
    check(std::count(fel.begin(), fel.end(), std::string("ProvFel")) == 1,
          "K15 felet loggas en gång, under ikonens namn",
          std::to_string(fel.size()) + " loggade namn");

    // K16: RINGEN VÄXER — en vit skiva på en vit yta kan inte bli ljusare, så
    // ytan rakt utanför skivan mörkas tills de skiljer sig 3:1.
    check(glaze::registerIcon("ProvRing", "B 2 2 20 20 0 | D 12 12 4", "all paper | all paper"),
          "K16 ikonen registreras");
    const glaze::Bitmap ring =
        glaze::detail::drawnBitmap("ProvRing", 24, IconStyle::Color, tema.light.colors);
    // Skivans kant står vid x 16–17 i 24 px; ringen är någon av pixlarna rakt
    // utanför den, på ytan.
    const Pixel mitt = pixel(ring, 12, 12);
    float       k    = 0.0f;
    for (int x = 15; x <= 19; ++x) {
        const Pixel q = pixel(ring, x, 12);
        if (q.a == 255)
            k = std::max(k, glaze::color::contrast(gfx::Color{mitt.r, mitt.g, mitt.b, 255},
                                                   gfx::Color{q.r, q.g, q.b, 255}));
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f:1", static_cast<double>(k));
    check(mitt.a == 255 && k >= kKrav - 1e-3f,
          "K16 ringen utanför en vit skiva på vitt når 3:1", buf);
}

} // namespace

int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    std::printf("stockicon_gate — standardikonernas kant, fästning och färg\n");
    k1();
    k2();
    k3();
    k4();
    k5();
    k6();
    k7();
    k8();
    k9();
    k10();
    k11();
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "STOCKICON_GATE GRÖN" : "STOCKICON_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
