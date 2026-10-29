#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/widgets/button.h"

// MODALDIALOGERNAS GEMENSAMMA LAYOUT — INTERN header. Meddelanderutan,
// inmatningsrutan och färgdialogen ställer knappraden och låser storleken på
// EXAKT samma sätt; de två stegen bor här i stället för i tre kopior.
// Inget tillstånd (dll_gate): bara funktioner av sina argument.

namespace glaze::dialoglayout {

// KNAPPRADENS MÅTT — bredden (knapparna och luckorna mellan dem) och höjden.
struct ButtonRow {
    float w = 0.0f;
    float h = 0.0f;
};

// KNAPPRADEN — bredderna i LOGISKA px med `Button`s egen formel (textens mått
// + 2 × `buttonPadX`, höjden radhöjden + 2 × `buttonPadY`), golvad av
// `msgBtnMinW` så "OK" inte blir en prick. Knapparna HÖGERSTÄLLS mot
// höger-/nederkanten med KUMULATIVA offsets — samma `right` på alla hade lagt
// dem OVANPÅ varandra — och anroparen har satt deras ankare (höger + nedåt).
// `rad` står i visuell ordning, vänster till höger.
inline ButtonRow placeButtonRow(const Ctx& c, const std::vector<Button*>& rad)
{
    const Style& m = *c.style;
    ButtonRow    r;
    r.h = Draw::lineHeight(c) + 2.0f * m.buttonPadY;
    std::vector<float> bredd;
    bredd.reserve(rad.size());
    for (Button* b : rad) {
        bredd.push_back(std::max(
            Draw::measure(c, b->text()).w + 2.0f * m.buttonPadX, m.msgBtnMinW));
        r.w += bredd.back();
    }
    if (!rad.empty())
        r.w += static_cast<float>(rad.size() - 1) * m.msgBtnGap;
    float right = m.msgPad;
    for (std::size_t i = rad.size(); i-- > 0;) {
        rad[i]->width  = bredd[i];
        rad[i]->height = r.h;
        rad[i]->right  = right;
        rad[i]->bottom = m.msgPad;
        right += bredd[i] + m.msgBtnGap;
    }
    return r;
}

// STORLEKEN, och de TVÅ ENHETERNA. `setContentSize` talar INNEHÅLLSYTAN (den
// räknar baklänges genom chromet) medan `setMinimumSize`/`setMaximumSize`
// talar KLIENTSTORLEK. Samma tal i båda hade låst fönstret till en namnlists
// höjd för lite — låsningen går därför via `clientSize()` EFTER
// `setContentSize`. Avrundningen UPPÅT är inte kosmetisk: truncering kan ta
// en pixel från sista raden och klippa en glyf. min == max ⇒ ej ändringsbar.
inline void lockSize(AppWindow& w, float contentW, float contentH)
{
    w.setContentSize(static_cast<int>(std::ceil(contentW)),
                     static_cast<int>(std::ceil(contentH)));
    const gfx::Sizef klient = w.clientSize();
    const int        cw     = static_cast<int>(klient.w);
    const int        ch     = static_cast<int>(klient.h);
    w.setMinimumSize(cw, ch);
    w.setMaximumSize(cw, ch);
}

} // namespace glaze::dialoglayout
