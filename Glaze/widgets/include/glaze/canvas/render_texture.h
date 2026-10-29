#pragma once
#include <functional>
#include <memory>

#include <gfx/color.h>
#include <gfx/image.h>

// glaze::RenderTexture — EN YTA ATT RITA I OCH SEDAN RITA SOM BILD
// En dyr sammansättning EN gång — en symbol, en
// legend, en rastrerad platta — som sedan ritas billigt så ofta som helst.
//
//     glaze::RenderTexture legend(256, 128);          // FYSISKA px
//     legend.paint([](const glaze::Ctx& c) {          // körs LAT, se nedan
//         glaze::Draw::text(c, c.bounds(), "1:2000", färg);
//     });
//
// `paint` ritar INTE direkt — det finns ingen renderer utanför en frame. Den
// lagrar funktionen och körs i NÄSTA frame som ritar texturen, en gång per
// fönster (varje fönster har sina egna texturer), och igen först när
// `paint`/`repaint` anropas eller fönstret släppt texturen under
// minnesbudgeten (ritytans `Canvas::setMemoryBudget`, förvalet
// `App::setCanvasMemoryBudget`). Funktionen får ett Ctx vars
// `box` är texturens hela yta från (0, 0) — `Draw::*` ritar i den precis som
// i en widget, i logiska px, med fönstrets tema, mått och fonter.
// Funktionen måste därför vara DETERMINISTISK: samma tillstånd, samma pixlar,
// i vilket fönster och vilken frame den än körs.
//
// Innehållet är PREMULTIPLICERAT och rensas till `clear` (förval helt
// genomskinligt) före varje målning; ritvägarna sköter resten (gfx/image.h).
//
// ETT HANDTAG som Bitmap: kopior delar texturen. Regel 8: en målning är kod
// som körs i drift och har ingen `.glazeui`-form, precis som `onPaint`.

namespace glaze {

struct Ctx;

namespace detail {
struct RenderTextureData;   // src/canvas/image_data.h
}

class RenderTexture {
public:
    using PaintFn = std::function<void(const Ctx&)>;

    // Ingen textur: `valid()` falskt, ritas inte.
    RenderTexture() = default;

    // w × h FYSISKA px. Mått ≤ 0 ger ingen textur + logg.
    RenderTexture(int w, int h);

    bool valid() const { return d_ != nullptr; }
    int  width() const;
    int  height() const;

    // Vad texturen visar — körs lat (se ovan). `clear` är rensfärgen.
    void paint(PaintFn fn, gfx::Color clear = gfx::Color{0, 0, 0, 0});

    // Samma funktion igen — det den läser har ändrats.
    void repaint();

    // Samplingen när texturen ritas skalad (gfx/image.h). Texturen har en
    // MIP-KEDJA, omräknad efter varje målning: Linear är mjukt
    // också mellan nivåerna, Nearest skarpt i närmaste nivå.
    gfx::ImageFilter filter() const;
    void             setFilter(gfx::ImageFilter f);

private:
    std::shared_ptr<detail::RenderTextureData> d_;
    friend class ImageCache;    // fönstrets mål (src/image_cache.h)
    friend class ImageSource;   // ritytans "samma bild" (canvas/shape.h)
};

} // namespace glaze
