// Progress — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: ränna + fyllning (bestämt) eller ränna + glidande bit
// (obestämt). Måtten kommer ur stilen (c.style), färgerna ur temat —
// inga tal och inga färger i den här filen.
//
// REGEL 1 är det känsliga här. Obestämt läge animerar — men widgeten rör
// ALDRIG fönstrets setAnimating: draw() invalidaterar SIG SJÄLV så länge
// läget är obestämt, och nästa frame ritar om precis den här rectet.
// Fönsterflaggan är en bool utan ägarskap: slog vi på den kunde vi sedan
// slå av appens EGEN animering (det hände i demon). Självinvalideringen
// har inte det problemet, stannar av sig själv när widgeten göms (dolt ⇒
// ritas aldrig ⇒ invalidaterar aldrig) och ritar bara om remsan.

#include "glaze/widgets/progress.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

#include <gfx/platform.h>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("progress", Progress)

bool Progress::setProp(const std::string& key, const std::string& v)
{
    if (key == "min")   { setMin(float(std::atof(v.c_str())));   return true; }
    if (key == "max")   { setMax(float(std::atof(v.c_str())));   return true; }
    if (key == "value") { setValue(float(std::atof(v.c_str()))); return true; }
    if (key == "indeterminate") {
        setIndeterminate(propparse::parseBool(v));
        return true;
    }
    return Widget::setProp(key, v);
}

void Progress::setIndeterminate(bool on)
{
    if (indeterminate_ == on)
        return;
    indeterminate_ = on;
    invalidate();   // påslaget startar animationen: draw() håller den vid liv
}

gfx::Sizef Progress::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner, axel <= 0 är AUTO — längden ur
        // stilens defaultlängd, tjockleken ur progressHeight.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = c.style->progressDefaultLen;
        if (s.h <= 0.0f) s.h = c.style->progressHeight;
        return s;
    }
    }
}

gfx::Rectf Progress::fillBounds(const Ctx& c) const
{
    const gfx::Rectf b    = c.bounds();
    const float      span = std::max(min_, max_) - min_;
    const float      f    = span > 0.0f
                                ? std::clamp((value_ - min_) / span, 0.0f, 1.0f)
                                : 0.0f;
    return gfx::Rectf{b.x, b.y, b.w * f, b.h};
}

// Den glidande biten: en triangelvåg ur den monotona klockan, så fasen är
// deterministisk per tidpunkt (draw() får ritas om flera gånger per frame
// vid partiell omritning utan att biten hoppar).
//
// Modulon görs i HELTAL, före konverteringen till float: en float har 24
// bitars mantissa och ticksMs() passerar 2^24 ms efter ~4,7 timmars
// drift — därefter hade fasen kvantiserats och animationen hackat.
gfx::Rectf Progress::chunkBounds(const Ctx& c) const
{
    const gfx::Rectf    b      = c.bounds();
    const float         len    = b.w * c.style->progressChunk;
    const std::uint64_t period = static_cast<std::uint64_t>(
        std::max(1, c.style->progressPeriodMs));
    const float t = static_cast<float>(gfx::ticksMs() % period) /
                    static_cast<float>(period);
    const float f = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;
    return gfx::Rectf{b.x + (b.w - len) * f, b.y, len, b.h};
}

void Progress::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;

    // Remsan som visar förloppet — bestämt läge fyller från vänster,
    // obestämt glider fram och tillbaka. Geometrin är densamma i alla tre
    // ritningarna; det är MATERIALET som skiljer dem.
    const gfx::Rectf fill = indeterminate_ ? chunkBounds(c) : fillBounds(c);

    switch (c.ui) {
    // GLAZE — ett SPÅR, samma recept som sliderns:
    // försänkt ränna med BESTÄMD kontur, fyllningen i `accentSurface`
    // (accenten som YTA — samma roll som sliderns passerade del, för de två
    // är samma sak sedd två gånger).
    //
    // VARKEN RÄNNAN ELLER FYLLNINGEN LAMINERAS. Rännan är en försänkning och
    // ljus samlas inte i botten av en fördjupning; fyllningen har ingen
    // KONTUR, och familjens mätta regel är att laminering kräver en. Ett
    // vitt ljusfält över fyllningen sänker den från 4.48:1 till 2.30:1 mot
    // rännan i LJUST läge — under 3:1, alltså under kravet för den mätning
    // som ensam gör spåret läsbart. Se slider.cpp för hela härledningen;
    // progress och slider måste svara likadant, de är samma klass.
    //
    // Skillnaden mot Slate är alltså inte ett lager utan en ROLL: `accent`
    // (betydelse) → `accentSurface` (yta), vilket i mörkt läge är två olika
    // kulörer och i ljust samma.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + kontur `border`, fyllning
    //              `accentSurface`
    //   hover      — (en förloppsindikator tar inte emot mus)
    //   tryck      —
    //   fokus      — (inget tabbstopp)
    //   inaktiv    fyllningen `textDim`; obestämt läge animerar inte
    case UI::Glaze:
        Draw::rect(c, b, m.progressRadius, col.appWorkspace,
                   Pen{col.border, m.borderWidth});
        Draw::rect(c, fill, m.progressRadius,
                   isEnabled() ? col.accentSurface : col.textDim);
        break;

    // MINIMALISTIC — SPÅRET ÄR ETT STRECK,
    // ordagrant samma form som sliderns: hårlinje i full längd, den
    // passerade delen i `accent` ovanpå. Ingen ränna, ingen kontur, ingen
    // radie — en förloppsindikator är en LINJE som fylls, och det räcker.
    //
    // Att strecket är tunt är inte ett problem för avläsbarheten: `accent`
    // mot bakgrunden är grindad ≥ 3:1 (min 4.05 över nio hues × två lägen),
    // och det är FYLLNADEN som bär spårets betydelse — precis som Slates
    // spår-recept säger, bara utan rännan runt.
    //
    // De fyra tillstånden (princip 7):
    //   vila       streck `border`, fyllning `accent`
    //   hover/tryck/fokus  —
    //   inaktiv    fyllningen `textDim`; obestämt läge animerar inte
    case UI::Minimalistic: {
        const float y = b.y + b.h / 2.0f;
        Draw::line(c, b.x, y, b.x + b.w, y, Pen{col.border, m.borderWidth});
        Draw::line(c, fill.x, y, fill.x + fill.w, y,
                   Pen{isEnabled() ? col.accent : col.textDim, m.borderWidth});
        break;
    }

    // SLATE — ett SPÅR, och förebilden för de andra spåren: försänkt yta
    // `appWorkspace`, hårlinje `border`
    // som antydan, betydelsen i FYLLNADEN. Slidern och Scrollbaren följer
    // den här.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + hårlinje, fyllning `accent`
    //   hover      — (en förloppsindikator tar inte emot mus)
    //   tryck      —
    //   fokus      — (inget tabbstopp)
    //   inaktiv    fyllningen `textDim`; obestämt läge ANIMERAR INTE
    //              (villkoret nedan) — en avstängd remsa som rör sig
    //              hade sagt att något pågår
    case UI::Slate:
    default:   // okänd ritning faller till Slate — de två andra står ovan
        Draw::rect(c, b, m.progressRadius, col.appWorkspace,
                   Pen{col.border, m.borderWidth});
        Draw::rect(c, fill, m.progressRadius,
                   isEnabled() ? col.accent : col.textDim);
        break;
    }

    // Animationens motor: begär nästa frame av MIG SJÄLV. draw() körs bara
    // för synliga widgets, så göms remsan slutar den av sig själv — och
    // fönstrets setAnimating (appens) rörs aldrig.
    if (indeterminate_ && isEnabled())
        invalidate();
}

} // namespace glaze
