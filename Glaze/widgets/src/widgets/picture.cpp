// Picture — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER. Hela geometrin bor i EN privat metod (imageBounds, logiska px);
// draw() är bara "klipp om det behövs, rita bilden". Måtten kommer ur
// bilden och widgetens yta, färgerna behövs inte alls — inga tal och inga
// färger i den här filen.

#include "glaze/widgets/picture.h"

#include <algorithm>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("picture", Picture)

bool Picture::setProp(const std::string& key, const std::string& value)
{
    if (key == "img") {
        // "lista/nyckel" — samma par som glaze::img (Buttons form). Miss ⇒
        // ogiltigt handtag (loggas av img, första gången) = ingen bild.
        AssetHandle h;
        if (!propparse::parseImg(value, h))
            return false;
        setImg(h);
        return true;
    }
    if (key == "fit" || key == "sizemode" || key == "sizeMode") {
        if (value == "fit")          setFit(Fit::Fit);
        else if (value == "fill")    setFit(Fit::Fill);
        else if (value == "stretch") setFit(Fit::Stretch);
        else if (value == "center")  setFit(Fit::Center);
        else return false;
        return true;
    }
    return Widget::setProp(key, value);
}

gfx::Sizef Picture::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;   // helt satt storlek vinner alltid

        // Autostorlek BARA för axlar användaren lämnat på 0 — bildens
        // naturliga mått (logiska px ur Draw) blir widgetens, och det är
        // samma enhet hela vägen ut.
        const gfx::Sizef nat = Draw::imageSize(c, img_);
        if (s.w <= 0.0f) s.w = nat.w;
        if (s.h <= 0.0f) s.h = nat.h;
        return s;
    }
    }
}

// Målrecten i LOGISKA px: bildens naturliga mått projicerade in i
// widgetens yta enligt Fit-läget. Utan bild (eller utan cache) är den
// tom — ritningen blir då en no-op.
gfx::Rectf Picture::imageBounds(const Ctx& c) const
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Sizef nat = Draw::imageSize(c, img_);
    if (nat.w <= 0.0f || nat.h <= 0.0f)
        return gfx::Rectf{b.x, b.y, 0.0f, 0.0f};
    if (fit_ == Fit::Stretch)
        return b;

    // Fit = ryms (minsta faktorn), Fill = täcker (största), Center = 1:1.
    const float k = fit_ == Fit::Center
                        ? 1.0f
                        : (fit_ == Fit::Fill ? std::max(b.w / nat.w, b.h / nat.h)
                                            : std::min(b.w / nat.w, b.h / nat.h));
    const gfx::Sizef s{nat.w * k, nat.h * k};
    return gfx::Rectf{b.x + (b.w - s.w) / 2.0f, b.y + (b.h - s.h) / 2.0f,
                      s.w, s.h};
}

void Picture::draw(const Ctx& c)
{
    const gfx::Rectf b  = c.bounds();
    const gfx::Rectf ib = imageBounds(c);

    switch (c.ui) {
    // GLAZE och MINIMALISTIC faller igenom till Slate, och det är ett
    // BESLUT.
    //
    // Bilden är APPENS material, inte ramverkets. En Glaze-kontur eller ett
    // laminerat ljusfält över en bild vore ramverket som ritar på innehållet
    // — precis det princip 1 säger att chromet aldrig får göra. Och
    // Minimalistics "kanten är formen" har inget att göra på något som redan
    // HAR en form: bilden ÄR sin egen avgränsning.
    //
    // Det finns alltså inte bara ingen anledning att avvika — det vore
    // aktivt fel i två av tre familjer att göra det.
    case UI::Glaze:
    case UI::Minimalistic:

    // SLATE — INNEHÅLL: en bildruta ritar BILDEN. En ram runt den hade
    // konkurrerat med innehållet (princip 1) och lagt en Glaze-kant på
    // material appen äger.
    //
    // De fyra tillstånden (princip 7): inget av dem finns — Picture är inte
    // en kontroll. Inaktiv dimmas INTE heller: en halvt genomskinlig bild
    // säger "trasig asset", inte "avstängd".
    case UI::Slate:
    default: {
        // Fill (och en för stor Center) spiller utanför ytan — klipp då,
        // annars inte: ett klipp kostar en flush i renderern.
        const bool clipped = ib.w > b.w || ib.h > b.h;
        if (clipped)
            Draw::clip(c, b);
        // NEDSKALAD: ritas bilden mindre än sitt naturliga mått
        // räknas den om till rutans storlek med ytans medelvärde — GPU:ns
        // linjära sampling hoppar annars över källpixlar och ger en grynig
        // bild. 1:1 och uppåt är GPU:ns filter rätt (en Bitmaps `setFilter`,
        // Nearest för pixelgrafik, gäller där).
        const gfx::Sizef nat = Draw::imageSize(c, img_);
        if (ib.w < nat.w || ib.h < nat.h)
            Draw::glyphResampled(c, ib, img_);
        else
            Draw::glyph(c, ib, img_);
        if (clipped)
            Draw::endClip(c);   // klippstacken MÅSTE balanseras
        break;
    }
    }
}

} // namespace glaze
