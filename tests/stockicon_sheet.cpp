// stockicon_sheet — STANDARDIKONERNAS ARK: ritar varje
// `glaze::StockIcon` i storleksklasserna 16, 24, 32 och 48 med namnet under,
// i fyra rutor — mono till vänster och färg till höger, ljust tema överst och
// mörkt under — och skriver arket som PNG.
//
//     stockicon_sheet [ut.png]      förval: stockicons.png i arbetskatalogen
//
// INGEN GRIND: programmet mäter ingenting och har inget add_test — det är
// ögats underlag när ikonernas stil ska bedömas och justeras. Ikonerna går
// genom ramverkets egen väg (`Draw::glyph` med ett `stockIcon`), i ett dolt
// fönster som glaze-render: `setOffscreen(true)` → `show()` →
// `renderFrame()` → `captureFrame()`. Skalan är fönstrets (skärmens), så
// arket visar den täthet maskinen har.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <gfx/image.h>
#include <glaze/glaze.h>

namespace {

constexpr int   kKolumner = 8;
constexpr float kCellB    = 168.0f;   // logiska px
constexpr float kCellH    = 84.0f;
constexpr int   kStorlekar[] = {16, 24, 32, 48};

int rader()
{
    return (glaze::stockIconCount() + kKolumner - 1) / kKolumner;
}

struct Rutnät : glaze::Widget {
    glaze::StockIconStyle stil = glaze::StockIconStyle::Mono;

protected:
    void draw(const glaze::Ctx& c) override
    {
        const gfx::Color text = c.theme->colors.text;
        for (int i = 0; i < glaze::stockIconCount(); ++i) {
            const auto  ikon = static_cast<glaze::StockIcon>(i);
            const float x0   = static_cast<float>(i % kKolumner) * kCellB;
            const float y0   = static_cast<float>(i / kKolumner) * kCellH;
            float       x    = x0 + 8.0f;
            for (const int s : kStorlekar) {
                const float sida = static_cast<float>(s);
                glaze::Draw::glyph(c, {x, y0 + 8.0f + (48.0f - sida), sida, sida},
                                   glaze::stockIcon(ikon, s, stil));
                x += sida + 8.0f;
            }
            glaze::Draw::text(c, {x0 + 8.0f, y0 + 60.0f, kCellB - 16.0f, 18.0f},
                              std::string(glaze::stockIconName(ikon)), text);
        }
    }
};

class Ark : public glaze::AppWindow {
public:
    Ark()
        : glaze::AppWindow("stockicon_sheet",
                           static_cast<int>(kKolumner * kCellB),
                           static_cast<int>(static_cast<float>(rader()) * kCellH),
                           gfx::Chrome::None)
    {
        rutnät.name = "rutnät";
        rutnät.dock = glaze::Dock::Fill;
        root().adopt(rutnät);
    }

    Rutnät rutnät;
};

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
};

bool rendera(Ark& win, glaze::ThemeMode läge, glaze::StockIconStyle stil, Bild& ut)
{
    win.rutnät.stil = stil;
    win.rutnät.invalidate();
    win.setThemeMode(läge);
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string ut = argc > 1 ? argv[1] : "stockicons.png";

    // Arket ritas i EN bild per ruta, och hundratals ikonbilder ryms inte i
    // förvalets uppladdningsbudget (16 MiB per bild) — resten hade väntat på
    // en bild som aldrig ritas.
    glaze::app().setCanvasUploadBudget(std::size_t{1} << 30);

    Ark win;
    win.setOffscreen(true);
    win.show();
    if (!win.isOpen()) {
        std::fprintf(stderr, "stockicon_sheet: fönstret kunde inte skapas\n");
        return 2;
    }
    // Rutorna i läsordning: ljust mono, ljust färg, mörkt mono, mörkt färg.
    constexpr glaze::ThemeMode      kLägen[] = {glaze::ThemeMode::Light,
                                                glaze::ThemeMode::Dark};
    constexpr glaze::StockIconStyle kStilar[] = {glaze::StockIconStyle::Mono,
                                                 glaze::StockIconStyle::Color};
    Bild rutor[4];
    for (int i = 0; i < 4; ++i)
        if (!rendera(win, kLägen[i / 2], kStilar[i % 2], rutor[i]) ||
            rutor[i].w != rutor[0].w || rutor[i].h != rutor[0].h) {
            std::fprintf(stderr, "stockicon_sheet: captureFrame misslyckades\n");
            return 2;
        }

    Bild ark;
    ark.w = rutor[0].w * 2;
    ark.h = rutor[0].h * 2;
    ark.px.resize(static_cast<std::size_t>(ark.w) * ark.h * 4);
    for (int i = 0; i < 4; ++i) {
        const Bild& k  = rutor[i];
        const int   x0 = (i % 2) * k.w, y0 = (i / 2) * k.h;
        for (int y = 0; y < k.h; ++y)
            std::copy_n(&k.px[static_cast<std::size_t>(y) * k.w * 4],
                        static_cast<std::size_t>(k.w) * 4,
                        &ark.px[(static_cast<std::size_t>(y0 + y) * ark.w +
                                 static_cast<std::size_t>(x0)) * 4]);
    }
    if (!gfx::encodePng(ut, ark.px, ark.w, ark.h)) {
        std::fprintf(stderr, "stockicon_sheet: kunde inte skriva %s\n", ut.c_str());
        return 2;
    }
    std::printf("stockicon_sheet: %d ikoner, %d x %d px -> %s\n",
                glaze::stockIconCount(), ark.w, ark.h, ut.c_str());
    return 0;
}
