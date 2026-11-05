// glyph_sheet — GLYFERNAS ARK: ritar varje glyf ramverket har och skriver
// arket som PNG.
//
//     glyph_sheet [ut.png] [ikon ...]   förval: glyfer.png i arbetskatalogen
//
// Med ikonnamn (`Save Close Print` — namnen i `glaze::StockIcon`) ritas bara
// de ikonerna och inga Draw-glyfer: ett prov på några ikoner i alla storlekar.
//
// Överst DRAW-GLYFERNA — formerna widgetarna ritar (bocken, strecket, plus,
// krysset, vinkelhakarna, rutan, märket och greppet) i storlekarna 8–16 med
// hårlinje och med penna, och `Draw::icon`s ikoner i 12–24 — ljust till
// vänster och mörkt till höger. Under dem varje `glaze::StockIcon` i
// storleksklasserna 16, 24, 32 och 48 med namnet under, i fyra rutor: mono
// till vänster och färg till höger, ljust överst och mörkt under — ritade av
// RAMVERKETS ark (`detail::renderIconSheet`, samma som `writeIconSheet`).
//
// INGEN GRIND: programmet mäter ingenting och har inget add_test — det är
// ögats underlag när glyfernas stil ska bedömas och justeras. Allt går genom
// ramverkets egen väg (`Draw`, och arket med `Draw::glyph` och ett
// `stockIcon`), i ett dolt fönster som glaze-render: `setOffscreen(true)` →
// `show()` → `renderFrame()` → `captureFrame()`. Skalan är fönstrets
// (skärmens), så arket visar glyferna som de ser ut på maskinen.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <gfx/image.h>
#include <glaze/glaze.h>

#include "icon_sheet.h"   // detail::renderIconSheet — ikonernas rutor

namespace {

using glaze::Draw;
using glaze::Pen;

// Remsans bredd i logiska px — lika bred som en av ikonarkets rutor (åtta
// kolumner på 168), så de fyra rutorna står under remsans två halvor.
constexpr float kArkB = 8 * 168.0f;

// ------------------------------------------------------------ Draw-glyferna

constexpr float kMarg     = 16.0f;
constexpr float kRubrikH  = 24.0f;   // avsnittets rubrik
constexpr float kHuvudH   = 36.0f;   // pennornas och storlekarnas rader
constexpr float kEtikettB = 112.0f;  // radens namn
constexpr float kGruppLuft = 16.0f;  // mellan två pennor

// Formerna: storlekarna (rutans sida) och pennorna (0 = hårlinje).
constexpr float kFormSidor[]   = {8.0f, 10.0f, 12.0f, 16.0f};
constexpr float kFormPennor[]  = {0.0f, 1.0f, 1.5f};
constexpr float kFormCellB     = 28.0f;
constexpr float kFormRadH      = 28.0f;

// `Draw::icon`: storlekarna och pennorna (1 = `iconStroke`).
constexpr float kIkonSidor[]   = {12.0f, 16.0f, 20.0f, 24.0f};
constexpr float kIkonPennor[]  = {0.0f, 1.0f};
constexpr float kIkonCellB     = 36.0f;
constexpr float kIkonRadH      = 32.0f;
constexpr float kIkonX         = 560.0f;   // avsnittets vänsterkant

enum class Form {
    Bock, Streck, Plus, Kryss, Upp, Ner, Vänster, Höger, Ruta,
};

struct FormRad {
    Form        form;
    const char* namn;
};

constexpr FormRad kFormer[] = {
    {Form::Bock, "bock"},          {Form::Streck, "streck"},
    {Form::Plus, "plus"},          {Form::Kryss, "kryss"},
    {Form::Upp, "vinkel upp"},     {Form::Ner, "vinkel ner"},
    {Form::Vänster, "vinkel vänster"}, {Form::Höger, "vinkel höger"},
    {Form::Ruta, "ruta"},
};

struct IkonRad {
    glaze::Icon icon;
    const char* namn;
};

constexpr IkonRad kIkoner[] = {
    {glaze::Icon::Folder, "Folder"},     {glaze::Icon::File, "File"},
    {glaze::Icon::Document, "Document"}, {glaze::Icon::Drive, "Drive"},
    {glaze::Icon::House, "House"},       {glaze::Icon::Monitor, "Monitor"},
    {glaze::Icon::Download, "Download"}, {glaze::Icon::Picture, "Picture"},
    {glaze::Icon::ArrowUp, "ArrowUp"},   {glaze::Icon::Calendar, "Calendar"},
    {glaze::Icon::Pinned, "Pinned"},     {glaze::Icon::Unpinned, "Unpinned"},
};

constexpr float kMärkeSidor[] = {12.0f, 14.0f, 16.0f, 20.0f};

std::string pennnamn(float p)
{
    if (p <= 0.0f)
        return "hårlinje";
    char buf[16];
    std::snprintf(buf, sizeof buf, "%g px", static_cast<double>(p));
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string tal(float v)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

void ritaForm(const glaze::Ctx& c, Form f, const gfx::Rectf& r, const Pen& p)
{
    switch (f) {
    case Form::Bock:    Draw::check(c, r, p); break;
    case Form::Streck:  Draw::minus(c, r, p); break;
    case Form::Plus:    Draw::plus(c, r, p); break;
    case Form::Kryss:   Draw::cross(c, r, p); break;
    case Form::Upp:     Draw::chevron(c, r, glaze::Dir::Up, p); break;
    case Form::Ner:     Draw::chevron(c, r, glaze::Dir::Down, p); break;
    case Form::Vänster: Draw::chevron(c, r, glaze::Dir::Left, p); break;
    case Form::Höger:   Draw::chevron(c, r, glaze::Dir::Right, p); break;
    case Form::Ruta:    Draw::frame(c, r, 0.0f, p); break;
    }
}

// Pennornas rubriker och storlekarnas tal ovanför en matris: en grupp per
// penna, en kolumn per storlek.
template <std::size_t P, std::size_t S>
void matrishuvud(const glaze::Ctx& c, float x0, float y, const float (&pennor)[P],
                 const float (&sidor)[S], float cellB)
{
    const auto& col    = c.theme->colors;
    const float gruppB = static_cast<float>(S) * cellB;
    for (std::size_t g = 0; g < P; ++g) {
        const float gx = x0 + static_cast<float>(g) * (gruppB + kGruppLuft);
        Draw::text(c, {gx, y, gruppB, 18.0f}, pennnamn(pennor[g]), col.text,
                   glaze::Align::Center);
        for (std::size_t s = 0; s < S; ++s)
            Draw::text(c, {gx + static_cast<float>(s) * cellB, y + 18.0f, cellB,
                           18.0f},
                       tal(sidor[s]), col.textDim, glaze::Align::Center);
    }
}

float formHöjd()
{
    return kRubrikH + kHuvudH +
           static_cast<float>(std::size(kFormer) + 2) * kFormRadH;
}

float ikonHöjd()
{
    return kRubrikH + kHuvudH +
           static_cast<float>(std::size(kIkoner)) * kIkonRadH;
}

float glyfHöjd()
{
    return kMarg + std::max(formHöjd(), ikonHöjd()) + kMarg;
}

struct Glyfer : glaze::Widget {
protected:
    void draw(const glaze::Ctx& c) override
    {
        const auto& col = c.theme->colors;

        // FORMERNA — en rad per form, glyfen centrerad i sin cell.
        float       y  = kMarg;
        const float x0 = kMarg + kEtikettB;
        Draw::text(c, {kMarg, y, 400.0f, kRubrikH}, "Draw-formerna", col.text);
        y += kRubrikH;
        matrishuvud(c, x0, y, kFormPennor, kFormSidor, kFormCellB);
        y += kHuvudH;
        const float formGruppB =
            static_cast<float>(std::size(kFormSidor)) * kFormCellB;
        for (const FormRad& rad : kFormer) {
            Draw::text(c, {kMarg, y, kEtikettB, kFormRadH}, rad.namn, col.textDim);
            for (std::size_t g = 0; g < std::size(kFormPennor); ++g) {
                const float gx = x0 + static_cast<float>(g) * (formGruppB + kGruppLuft);
                for (std::size_t s = 0; s < std::size(kFormSidor); ++s) {
                    const float sida = kFormSidor[s];
                    const float cx   = gx + static_cast<float>(s) * kFormCellB;
                    ritaForm(c, rad.form,
                             {cx + (kFormCellB - sida) / 2.0f,
                              y + (kFormRadH - sida) / 2.0f, sida, sida},
                             Pen{col.text, kFormPennor[g]});
                }
            }
            y += kFormRadH;
        }

        // MÄRKET (drag-och-släppets: plus, avvisat, flytta) i fyra
        // storlekar, och GREPPET liggande och stående.
        Draw::text(c, {kMarg, y, kEtikettB, kFormRadH}, "märke", col.textDim);
        float mx = x0;
        for (const glaze::Sign tecken :
             {glaze::Sign::Plus, glaze::Sign::Minus, glaze::Sign::Arrow}) {
            for (const float sida : kMärkeSidor) {
                const gfx::Rectf r{mx + (kFormCellB - sida) / 2.0f,
                                   y + (kFormRadH - sida) / 2.0f, sida, sida};
                if (tecken == glaze::Sign::Minus)
                    Draw::badge(c, r, tecken, col.errorSurface, col.onError);
                else
                    Draw::badge(c, r, tecken, col.accentSurface, col.onAccent);
                mx += kFormCellB;
            }
            mx += kGruppLuft / 2.0f;
        }
        y += kFormRadH;
        Draw::text(c, {kMarg, y, kEtikettB, kFormRadH}, "grepp", col.textDim);
        Draw::grip(c, {x0, y + 8.0f, 48.0f, 12.0f}, false, col.textDim);
        Draw::grip(c, {x0 + 64.0f, y + 8.0f, 64.0f, 12.0f}, false, col.textDim);
        Draw::grip(c, {x0 + 144.0f, y, 12.0f, kFormRadH}, true, col.textDim);
        Draw::grip(c, {x0 + 172.0f, y, 8.0f, kFormRadH}, true, col.textDim);

        // DRAW::ICON — en rad per ikon.
        y              = kMarg;
        const float ix = kIkonX + kEtikettB;
        Draw::text(c, {kIkonX, y, 400.0f, kRubrikH}, "Draw::icon", col.text);
        y += kRubrikH;
        matrishuvud(c, ix, y, kIkonPennor, kIkonSidor, kIkonCellB);
        y += kHuvudH;
        const float ikonGruppB =
            static_cast<float>(std::size(kIkonSidor)) * kIkonCellB;
        for (const IkonRad& rad : kIkoner) {
            Draw::text(c, {kIkonX, y, kEtikettB, kIkonRadH}, rad.namn, col.textDim);
            for (std::size_t g = 0; g < std::size(kIkonPennor); ++g) {
                const float gx = ix + static_cast<float>(g) * (ikonGruppB + kGruppLuft);
                for (std::size_t s = 0; s < std::size(kIkonSidor); ++s) {
                    const float sida = kIkonSidor[s];
                    const float cx   = gx + static_cast<float>(s) * kIkonCellB;
                    Draw::icon(c,
                               {cx + (kIkonCellB - sida) / 2.0f,
                                y + (kIkonRadH - sida) / 2.0f, sida, sida},
                               rad.icon, Pen{col.text, kIkonPennor[g]});
                }
            }
            y += kIkonRadH;
        }
    }
};

// ------------------------------------------------------------------ arket

// Ett dolt fönster med EN widget som fyller det.
template <class Innehåll>
class Ark : public glaze::AppWindow {
public:
    Ark(float b, float h)
        : glaze::AppWindow("glyph_sheet", static_cast<int>(b),
                           static_cast<int>(h), gfx::Chrome::None)
    {
        innehåll.name = "innehåll";
        innehåll.dock = glaze::Dock::Fill;
        root().adopt(innehåll);
        setOffscreen(true);
    }

    Innehåll innehåll;
};

using Bild = glaze::detail::IconSheetImage;

template <class A>
bool fånga(A& win, glaze::ThemeMode läge, Bild& ut)
{
    win.innehåll.invalidate();
    win.setThemeMode(läge);
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

void klistra(Bild& ark, const Bild& k, int x0, int y0)
{
    for (int y = 0; y < k.h; ++y)
        std::copy_n(&k.px[static_cast<std::size_t>(y) * k.w * 4],
                    static_cast<std::size_t>(k.w) * 4,
                    &ark.px[(static_cast<std::size_t>(y0 + y) * ark.w +
                             static_cast<std::size_t>(x0)) * 4]);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string        ut = argc > 1 ? argv[1] : "glyfer.png";
    std::vector<std::string> urval;   // ikonerna arket ritar
    for (int i = 2; i < argc; ++i) {
        if (!glaze::stockIconByName(argv[i])) {
            std::fprintf(stderr, "glyph_sheet: okänd ikon \"%s\"\n", argv[i]);
            return 2;
        }
        urval.emplace_back(argv[i]);
    }
    const bool prov = !urval.empty();   // bara de namngivna ikonerna
    if (!prov)
        for (int i = 0; i < glaze::stockIconCount(); ++i)
            urval.emplace_back(glaze::stockIconName(static_cast<glaze::StockIcon>(i)));

    constexpr glaze::ThemeMode kLägen[] = {glaze::ThemeMode::Light,
                                           glaze::ThemeMode::Dark};

    // Draw-glyferna: ljust och mörkt — inte i ett prov.
    Bild glyfer[2];
    if (!prov) {
        Ark<Glyfer> win(kArkB, glyfHöjd());
        win.show();
        if (!win.isOpen()) {
            std::fprintf(stderr, "glyph_sheet: fönstret kunde inte skapas\n");
            return 2;
        }
        for (int i = 0; i < 2; ++i)
            if (!fånga(win, kLägen[i], glyfer[i]) || glyfer[i].w != glyfer[0].w ||
                glyfer[i].h != glyfer[0].h) {
                std::fprintf(stderr, "glyph_sheet: captureFrame misslyckades\n");
                return 2;
            }
    }

    // Standardikonerna: ramverkets ark, i fyra rutor — ljust mono, ljust färg,
    // mörkt mono, mörkt färg.
    Bild ikoner;
    if (!glaze::detail::renderIconSheet(urval, ikoner)) {
        std::fprintf(stderr, "glyph_sheet: ikonarket kunde inte ritas\n");
        return 2;
    }

    if (!prov && glyfer[0].w * 2 != ikoner.w) {
        std::fprintf(stderr, "glyph_sheet: remsan och ikonarket är olika breda "
                             "(%d och %d px)\n",
                     glyfer[0].w * 2, ikoner.w);
        return 2;
    }
    Bild ark;
    ark.w = ikoner.w;
    ark.h = glyfer[0].h + ikoner.h;
    ark.px.resize(static_cast<std::size_t>(ark.w) * ark.h * 4);
    if (!prov)
        for (int i = 0; i < 2; ++i)
            klistra(ark, glyfer[i], i * glyfer[i].w, 0);
    klistra(ark, ikoner, 0, glyfer[0].h);
    if (!gfx::encodePng(ut, ark.px, ark.w, ark.h)) {
        std::fprintf(stderr, "glyph_sheet: kunde inte skriva %s\n", ut.c_str());
        return 2;
    }
    std::printf("glyph_sheet: %s%zu standardikoner, %d x %d px -> %s\n",
                prov ? "" : "Draw-glyferna och ", urval.size(), ark.w, ark.h,
                ut.c_str());
    return 0;
}
