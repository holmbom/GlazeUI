// IKONARKET — `glaze::writeIconSheet` (glaze/stock_icon.h) och dess bild
// `detail::renderIconSheet` (icon_sheet.h), som verktyget för ramverkets egna
// glyfer också ritar sina ikoner med: ikonerna i 16, 24,
// 32 och 48 med namnet under, i fyra rutor — mono till vänster och färg till
// höger, ljust överst och mörkt under. Allt går genom ramverkets egen väg
// (`Draw::glyph` med ett `drawnIcon` eller `stockIcon`) i ett dolt fönster i
// skärmens skala, som glaze-render: `setOffscreen(true)` → `show()` →
// `renderFrame()` → `captureFrame()`. Arket visar alltså ikonerna som de ser
// ut på maskinen, utan förstoring.

#include "icon_sheet.h"

#include "glaze/stock_icon.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gfx/image.h>      // encodePng
#include <gfx/platform.h>   // log

#include "glaze/app.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"

namespace glaze {
namespace {

// En ruta per ikon: de fyra storlekarna bredvid varandra på en gemensam
// baslinje och namnet under.
constexpr int   kKolumner    = 8;
constexpr float kCellB       = 168.0f;
constexpr float kCellH       = 84.0f;
constexpr int   kStorlekar[] = {16, 24, 32, 48};
constexpr float kLuft        = 8.0f;
constexpr float kStörst      = 48.0f;
constexpr float kNamnY       = 60.0f;
constexpr float kNamnH       = 18.0f;

// Arket ritar så många bilder i EN bild att förvalets uppladdningsbudget
// inte räcker — resten hade väntat på en bild som aldrig ritas.
constexpr std::size_t kArkbudget = std::size_t{1} << 30;

struct Post {
    std::string namn;
    bool        app = false;   // appens ikon, annars standardikonen `stock`
    StockIcon   stock{};
};

ImageSource bildAv(const Post& p, int storlek, IconStyle stil)
{
    return p.app ? drawnIcon(p.namn, storlek, stil) : stockIcon(p.stock, storlek, stil);
}

struct Ikonrutor : Widget {
    std::vector<Post> poster;
    int               kolumner = 1;
    IconStyle         stil     = IconStyle::Mono;

protected:
    void draw(const Ctx& c) override
    {
        const gfx::Color text = c.theme->colors.text;
        for (std::size_t i = 0; i < poster.size(); ++i) {
            const int   n  = static_cast<int>(i);
            const float x0 = static_cast<float>(n % kolumner) * kCellB;
            const float y0 = static_cast<float>(n / kolumner) * kCellH;
            float       x  = x0 + kLuft;
            for (const int s : kStorlekar) {
                const float sida = static_cast<float>(s);
                Draw::glyph(c, {x, y0 + kLuft + (kStörst - sida), sida, sida},
                            bildAv(poster[i], s, stil));
                x += sida + kLuft;
            }
            Draw::text(c, {x0 + kLuft, y0 + kNamnY, kCellB - 2.0f * kLuft, kNamnH},
                       poster[i].namn, text);
        }
    }
};

// Ett dolt fönster med rutnätet som fyller det.
class Ark : public AppWindow {
public:
    Ark(int b, int h) : AppWindow("writeIconSheet", b, h, gfx::Chrome::None)
    {
        rutor.name = "rutor";
        rutor.dock = Dock::Fill;
        root().adopt(rutor);
        setOffscreen(true);
    }

    Ikonrutor rutor;
};

// Appens uppladdningsbudget, återställd när arket är klart.
struct Budget {
    std::size_t förra = app().canvasUploadBudget();
    Budget() { app().setCanvasUploadBudget(kArkbudget); }
    ~Budget() { app().setCanvasUploadBudget(förra); }
    Budget(const Budget&)            = delete;
    Budget& operator=(const Budget&) = delete;
};

} // namespace

bool writeIconSheet(const std::string& pngPath, const std::vector<std::string>& names)
{
    detail::IconSheetImage ark;
    if (!detail::renderIconSheet(names, ark))
        return false;
    if (!gfx::encodePng(pngPath, ark.px, ark.w, ark.h)) {
        gfx::log("writeIconSheet: kunde inte skriva %s", pngPath.c_str());
        return false;
    }
    return true;
}

namespace detail {

bool renderIconSheet(const std::vector<std::string>& names, IconSheetImage& out)
{
    const std::vector<std::string> appens = drawnIconNames();
    const std::vector<std::string> lista  = names.empty() ? appens : names;
    std::vector<Post>              poster;
    for (const std::string& n : lista) {
        Post p;
        p.namn = n;
        if (std::find(appens.begin(), appens.end(), n) != appens.end()) {
            p.app = true;
        } else if (const auto s = stockIconByName(n)) {
            p.stock = *s;
        } else {
            gfx::log("ikonarket: ikonen \"%s\" finns inte — inget ark", n.c_str());
            return false;
        }
        poster.push_back(std::move(p));
    }
    if (poster.empty()) {
        gfx::log("ikonarket: inga ikoner att rita — inget ark");
        return false;
    }

    const Budget budget;
    const int    kolumner = std::min(kKolumner, static_cast<int>(poster.size()));
    const int    rader    = (static_cast<int>(poster.size()) + kolumner - 1) / kolumner;
    Ark          win(static_cast<int>(static_cast<float>(kolumner) * kCellB),
                     static_cast<int>(static_cast<float>(rader) * kCellH));
    win.rutor.poster   = std::move(poster);
    win.rutor.kolumner = kolumner;
    win.show();
    if (!win.isOpen()) {
        gfx::log("ikonarket: fönstret kunde inte skapas — inget ark");
        return false;
    }

    // Rutorna i läsordning: ljust mono, ljust färg, mörkt mono, mörkt färg.
    IconSheetImage rutor[4];
    for (int i = 0; i < 4; ++i) {
        win.rutor.stil = i % 2 == 0 ? IconStyle::Mono : IconStyle::Color;
        win.rutor.invalidate();
        win.setThemeMode(i < 2 ? ThemeMode::Light : ThemeMode::Dark);
        win.window().renderFrame();
        if (!win.window().captureFrame(rutor[i].px, rutor[i].w, rutor[i].h) ||
            rutor[i].w != rutor[0].w || rutor[i].h != rutor[0].h) {
            gfx::log("ikonarket: bilden kunde inte läsas tillbaka — inget ark");
            return false;
        }
    }

    out.w = rutor[0].w * 2;
    out.h = rutor[0].h * 2;
    out.px.assign(static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h) * 4, 0);
    for (int i = 0; i < 4; ++i) {
        const IconSheetImage& k  = rutor[i];
        const int             x0 = (i % 2) * k.w, y0 = (i / 2) * k.h;
        for (int y = 0; y < k.h; ++y)
            std::copy_n(&k.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(k.w) * 4],
                        static_cast<std::size_t>(k.w) * 4,
                        &out.px[(static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(out.w) +
                                 static_cast<std::size_t>(x0)) * 4]);
    }
    return true;
}

} // namespace detail
} // namespace glaze
