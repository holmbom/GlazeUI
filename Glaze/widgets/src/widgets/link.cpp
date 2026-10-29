// Link — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// text, streck under texten, fokusring. Måtten kommer ur stilen
// (c.style), färgerna ur temats link-roller — inga tal och inga färger i
// den här filen. Beteendet (klick → onClick → gfx::openUrl, Space/Enter)
// bor också här: hela widgeten i EN fil + headern.

#include "glaze/widgets/link.h"

#include <algorithm>

#include <gfx/platform.h>

#include "glaze/draw.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("link", Link)

bool Link::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {   // title = äldre alias (regel 4-arv)
        setText(value);
        return true;
    }
    if (key == "url") {
        setUrl(value);
        return true;
    }
    return Widget::setProp(key, value);
}

gfx::Sizef Link::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN (som Label): satt axel vinner, axel <= 0 är AUTO.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        // AUTO är som Labels, plus plats för strecket under textraden.
        // `Draw::measure` och INTE `c.font->measure`:
        // fonten talar FYSISKA px, mätvägen LOGISKA. Draw är bryggan.
        const auto&      m = *c.style;
        const gfx::Sizef t = Draw::measure(c, text());
        if (s.w <= 0.0f) s.w = t.w;
        if (s.h <= 0.0f)
            s.h = t.h + m.linkGap + m.linkUnderline;
        return s;
    }
    }
}

// Textraden i logiska px — EN sanning: både texten och understrykningen
// ritas ur den här. Texten centreras i ytan MINUS strecket (gap +
// tjocklek), inte i hela ytan: annars hamnar strecket utanför widgetens
// höjd så fort gap > tjocklek, och autostorleken i contentSize() räknar
// med exakt det utrymmet.
gfx::Rectf Link::textBounds(const Ctx& c) const
{
    const gfx::Rectf b = c.bounds();
    const auto&      m = *c.style;
    const gfx::Sizef t = Draw::measure(c, text());
    const float band = std::max(t.h, b.h - (m.linkGap + m.linkUnderline));
    return gfx::Rectf{b.x, b.y + (band - t.h) / 2.0f, t.w, t.h};
}

gfx::Color Link::linkColor(const Ctx& c) const
{
    const auto& col = c.theme->colors;
    if (!isEnabled())
        return col.textDim;
    return hovered() ? col.linkHover : col.link;
}

void Link::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf t   = textBounds(c);
    const auto&      m   = *c.style;
    const gfx::Color col = linkColor(c);

    switch (c.ui) {
    // MINIMALISTIC — texten bär färgen i VILA, strecket kommer först vid
    // hover. Det är samma rörelse som resten
    // av familjen gör: dekorationen tas bort och tillståndet får bära
    // skillnaden. En permanent understrykning är en LINJE, och en linje per
    // länk i en tät lista blir brus.
    //
    // Det är också passets ENDA plats där något syns först vid hover, och
    // undantaget håller bara därför att länken fortfarande är fullt synlig
    // utan den: färgen `link` är grindad ≥ 4.5:1 mot bakgrunden och är
    // ENSAM tillräcklig för att skilja den från vanlig text. Samma sak
    // gjord på en KNAPP höll inte för en knapp ("kant bara vid hover") — en
    // kontroll får inte behöva musen för att visa att den finns, men en länk
    // som redan har egen färg gör inte det.
    //
    // De fyra tillstånden (princip 7):
    //   vila       `link`, INGEN understrykning
    //   hover      `linkHover` + understrykning
    //   tryck      — (klicket öppnar; inget mellanläge att visa)
    //   fokus      fristående ring runt hela ytan
    //   inaktiv    `textDim`, ingen understrykning
    case UI::Minimalistic:
        Draw::text(c, {b.x, t.y, b.w, t.h}, text(), col);
        if (hovered() && isEnabled())
            Draw::line(c, t.x, t.y + t.h + m.linkGap, t.x + t.w,
                       t.y + t.h + m.linkGap, Pen{col, m.linkUnderline});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset), m.cornerRadius,
                        Pen{c.theme->colors.accent, m.focusWidth});
        break;

    // GLAZE — faller igenom till Slate, och det är ett BESLUT.
    // En länk är TEXT, och Glazes formspråk handlar om YTOR: laminering,
    // kontur, skugga. Ingen av dem har något att göra på en textrad — en
    // laminerad länk vore en knapp, och en konturerad länk likaså. Det som
    // gör länken till Glazes länk är PALETTEN (`link`/`linkHover` i
    // Lavender), och den axeln är redan bytt.
    case UI::Glaze:

    // SLATE — INNEHÅLL: en länk är TEXT, och dess
    // affordance är strecket och färgen. En yta eller kant hade gjort den till en knapp.
    //
    // De fyra tillstånden (princip 7):
    //   vila       `link` + understrykning i samma färg
    //   hover      `linkHover` (både text och streck — linkColor)
    //   tryck      — (klicket öppnar; det finns inget mellanläge att visa)
    //   fokus      fristående ring runt hela ytan
    //   inaktiv    `textDim`
    case UI::Slate:
    default:   // okänd ritning faller till Slate
        // Texten ritas i textBounds höjd — Draw::text v-centrerar i det
        // rect den får, så raden hamnar exakt där strecket räknar med.
        Draw::text(c, {b.x, t.y, b.w, t.h}, text(), col);
        Draw::line(c, t.x, t.y + t.h + m.linkGap, t.x + t.w,
                   t.y + t.h + m.linkGap, Pen{col, m.linkUnderline});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset), m.cornerRadius,
                        Pen{c.theme->colors.accent, m.focusWidth});
        break;
    }
}

// Appens handler först (den kan vara HELA poängen — url:en är valfri),
// sedan OS-handlingen.
void Link::clicked()
{
    Widget::clicked();
    if (!url_.empty())
        gfx::openUrl(url_);
}

// Space/Enter aktiverar en fokuserad länk — Button-mönstret.
bool Link::keyDown(gfx::Key k, std::uint32_t)
{
    if (k == gfx::Key::Space || k == gfx::Key::Return) {
        clicked();
        return true;
    }
    return false;
}

} // namespace glaze
