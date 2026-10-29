// Separator — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och den
// gör det med EN rad: ett streck mitt i widgetens yta. Tjockleken och
// luften kommer ur stilen (c.style), färgen ur temat — inga tal och inga
// färger i den här filen.

#include "glaze/widgets/separator.h"

#include "glaze/draw.h"
#include "glaze/widgets/bar.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("separator", Separator)

bool Separator::inBar() const
{
    return dynamic_cast<const Bar*>(parent()) != nullptr;
}

// TVÄRS BAREN: lodrät i en liggande bar, vågrät i en stående.
bool Separator::drawnVertical() const
{
    if (const auto* b = dynamic_cast<const Bar*>(parent()))
        return !b->isVertical();
    return vertical_;
}

bool Separator::setProp(const std::string& key, const std::string& value)
{
    if (key == "vertical") {
        setVertical(propparse::parseBool(value));
        return true;
    }
    if (key == "orientation") {   // Scrollbars form — samma ord, samma sak
        if (value == "horizontal")    setVertical(false);
        else if (value == "vertical") setVertical(true);
        else return false;
        return true;
    }
    return Widget::setProp(key, value);
}

gfx::Sizef Separator::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN, mätt i KROPPSRAMEN (= skärmens vid rotation 0):
        // satt axel vinner — ÄVEN tväraxeln.
        // AUTO tvärs = streck + luft på båda sidor, AUTO längs =
        // stilens defaultlängd (dock/anchors sträcker den när de finns).
        const auto& m    = *c.style;
        const float thin = m.separatorThickness + 2.0f * m.separatorInset;
        const float len  = m.separatorDefaultLen;
        gfx::Sizef  s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        // I en BAR är längden barens tvärsida, som baren ger den — AUTO är
        // då tunn på båda axlarna, så att måttet längs baren är detsamma i
        // en liggande och en stående.
        const bool lodrät = drawnVertical();
        if (s.w <= 0.0f) s.w = lodrät || inBar() ? thin : len;
        if (s.h <= 0.0f) s.h = !lodrät || inBar() ? thin : len;
        return s;
    }
    }
}

void Separator::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const Pen        pen{c.theme->colors.border, c.style->separatorThickness};

    switch (c.ui) {
    // GLAZE och MINIMALISTIC faller igenom till Slate, och det är ett
    // BESLUT.
    //
    // Separatorn ÄR redan bådas formspråk. Minimalistics regel är "ytor
    // separeras med en LINJE, inte med en fyllning" — widgeten är den
    // linjen, och det finns inget att ta bort. Glazes regel är "bestämd
    // kontur" — en kontur ÄR en linje, och att laminera eller skugga ett
    // streck är meningslöst: ett streck har ingen yta att bära ljus på.
    // Det som skiljer familjerna är FÄRGEN (`border` ur respektive palett)
    // och TJOCKLEKEN (`separatorThickness` ur respektive stil), och båda
    // axlarna är redan bytta.
    case UI::Glaze:
    case UI::Minimalistic:

    // SLATE — INNEHÅLL: `border` som LINJE är rollens
    // rätta användning — som YTA är den fel (sliderns ränna).
    //
    // De fyra tillstånden (princip 7): inget av dem finns. En skiljelinje
    // har varken hover, tryck, fokus eller inaktivt läge — den är ren
    // struktur, och det är avsiktligt.
    case UI::Slate:
    default:   // okänd ritning faller till Slate
        if (drawnVertical())
            Draw::line(c, b.x + b.w / 2.0f, b.y, b.x + b.w / 2.0f, b.y + b.h,
                       pen);
        else
            Draw::line(c, b.x, b.y + b.h / 2.0f, b.x + b.w, b.y + b.h / 2.0f,
                       pen);
        break;
    }
}

} // namespace glaze
