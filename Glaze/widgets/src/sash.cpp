// SKIKTET — se sash.h. Ritningen följer widget-filernas regler fullt ut
// (hård regel 3): inga tal och inga färger här, måtten ur `c.style`
// (`splitter*`), färgerna ur `c.theme->colors`, ritandet ur `Draw`.

#include "sash.h"

#include <algorithm>

namespace glaze::sash {

// De fyra tillstånden — skiktet har två av dem:
//   vila       en hårlinje mitt i skiktet (Glaze: plus ett grepp)
//   het        linjen (eller greppet) går till `accent` — pekaren står
//              över skiktet eller det dras just nu
//   tryck      = het: draget ÄR trycket, och skiktet följer handen
//   inaktiv    = vila (se sash.h — en yta har inget läge)
void draw(const Ctx& c, const gfx::Rectf& r, bool vertical, bool hot)
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;

    // Linjen mitt i skiktet, längs hela dess längd.
    const float cx = r.x + r.w / 2.0f;
    const float cy = r.y + r.h / 2.0f;
    auto linje = [&](const Pen& pen) {
        if (vertical)
            Draw::line(c, cx, r.y, cx, r.y + r.h, pen);
        else
            Draw::line(c, r.x, cy, r.x + r.w, cy, pen);
    };

    switch (c.ui) {
    // GLAZE — "bestämd kontur" på det enda stället
    // som går att ta i: en hårlinje i FÖNSTERKANTENS roll (`frame` — skiktet
    // är en kant mellan två ytor, inte en kontroll) och mitt på den ett
    // GREPP, en kort pille i kontrollkanten `border` som tänds i `accent`.
    // Greppet är halva skiktets tjocklek brett — formens definition, inte
    // ett valt tal — och stilens längd, kapad mot skiktets.
    case UI::Glaze: {
        linje(Pen{col.frame, m.borderWidth});
        if (m.splitterGripLen <= 0.0f)
            break;
        const float tjock = (vertical ? r.w : r.h) / 2.0f;
        const float längd = std::min(m.splitterGripLen, vertical ? r.h : r.w);
        const gfx::Rectf grepp =
            vertical ? gfx::Rectf{cx - tjock / 2.0f, cy - längd / 2.0f, tjock,
                                  längd}
                     : gfx::Rectf{cx - längd / 2.0f, cy - tjock / 2.0f, längd,
                                  tjock};
        Draw::rect(c, grepp, tjock / 2.0f, hot ? col.accent : col.border);
        break;
    }

    // MINIMALISTIC — "spår är streck": bara
    // linjen, och tillståndet i linjens FÄRG. Ritas som Slate — skrivet här
    // och inte underförstått: ett skikt i en outline-familj har ingen yta
    // att avstå från, så de två formspråken landar i samma streck.
    case UI::Minimalistic:

    // SLATE — en YTA delad av en hårlinje i
    // kontrollkanten. Het: linjen blir `accent` i fokusringens tjocklek, så
    // skiktet läser som "greppbart nu" utan att få en egen platta.
    case UI::Slate:
    default:
        linje(hot ? Pen{col.accent, m.focusWidth}
                  : Pen{col.border, m.borderWidth});
        break;
    }
}

gfx::PointerShape cursor(bool vertical)
{
    return vertical ? gfx::PointerShape::ResizeWE : gfx::PointerShape::ResizeNS;
}

float paneFromPointer(float p, float edge, bool before, float grip,
                      float thick, float scale)
{
    const float skiktStart = p - grip;
    return before ? (skiktStart - edge) / scale
                  : (edge - (skiktStart + thick)) / scale;
}

float clampPane(float want, float ownMin, float otherMin, float avail)
{
    float v = std::min(want, avail - std::max(0.0f, otherMin));
    v       = std::max(v, std::max(0.0f, ownMin));
    return std::clamp(v, 0.0f, std::max(0.0f, avail));
}

} // namespace glaze::sash
