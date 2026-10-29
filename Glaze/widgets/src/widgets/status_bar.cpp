// StatusBar — typidentiteten, registreringen, höjden och FÄLTEN.
// Ritningen och flexen är Bars (bar.cpp); fältet på
// fönstret bor i ../app_window.cpp. Fälten är barn av en intern typ (`Cell`)
// som Bar-flexen placerar — inga tal och inga färger här: luften är
// `statusPanelPadX`, skiljelinjen separatorns penna och texten temats.

#include "glaze/widgets/status_bar.h"

#include <algorithm>

#include <gfx/platform.h>

#include "glaze/draw.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("statusbar", StatusBar)

// ------------------------------------------------------------ ett fält

// FÄLTET — en text i en ruta av raden. Bredden är widgetens EGEN axel
// (`width`, axelregeln: satt vinner, AUTO är texten och luften), och
// fyllningen är Bar-flexens grow-vikt. En ny text, bredd eller fyllning
// flyttar grannarna, så fältet säger till föräldern (`relayoutSiblings`).
class StatusBar::Cell final : public Widget {
public:
    explicit Cell(StatusPanel p) : text_(std::move(p.text)), fill_(p.fill)
    {
        width = p.width > 0.0f ? p.width : 0.0f;   // <= 0 är AUTO
    }

    void set(StatusPanel p)
    {
        const float bredd = p.width > 0.0f ? p.width : 0.0f;
        if (p.text == text_ && bredd == width && p.fill == fill_)
            return;
        text_ = std::move(p.text);
        width = bredd;
        fill_ = p.fill;
        invalidate();
        relayoutSiblings();
    }

    void setText(TrText t)
    {
        if (t == text_)
            return;
        text_ = std::move(t);
        invalidate();
        if (width <= 0.0f)
            relayoutSiblings();   // AUTO-bredden följer texten — också golvet
                                  // för ett FYLLANDE fält (contentSize)
    }

    StatusPanel data() const { return StatusPanel{text_, width, fill_}; }
    const std::string& text() const { return text_.str(); }

    float flexGrow() const override { return fill_ ? 1.0f : 0.0f; }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            // AXELREGELN: satt bredd vinner. AUTO är texten och luften — OCKSÅ
            // för ett FYLLANDE fält, där det är MINSTA bredden: Bar-flexen lägger det
            // lediga ovanpå den mätta bredden, så ett fyllande fält kapar
            // aldrig sin text när raden rymmer den (annars fick två
            // fyllande fält med olika långa texter lika mycket och den
            // längre kortades). Höjden ger baren (radens hela).
            gfx::Sizef s{width, height};
            if (s.w <= 0.0f)
                s.w = Draw::measure(c, text()).w +
                      2.0f * c.style->statusPanelPadX;
            if (s.h <= 0.0f)
                s.h = Draw::lineHeight(c);
            return s;
        }
        }
    }

    void draw(const Ctx& c) override
    {
        const gfx::Rectf b   = c.bounds();
        const auto&      m   = *c.style;
        const gfx::Rectf yta = {b.x + m.statusPanelPadX, b.y,
                                std::max(0.0f, b.w - 2.0f * m.statusPanelPadX),
                                b.h};
        switch (c.ui) {
        // ALLA TRE FAMILJERNA RITAR FÄLTET LIKADANT, och det är ett BESLUT
        // (Labels): texten är text i varje formspråk, och skiljelinjen är
        // separatorns — samma penna och luft som en `Separator` i en `Bar`,
        // så ett fält och en separator i samma rad ser likadana ut.
        case UI::Glaze:
        case UI::Minimalistic:
        case UI::Slate:
        default:
            if (grannFöre())
                Draw::line(c, b.x, b.y + m.separatorInset, b.x,
                           b.y + b.h - m.separatorInset,
                           Pen{c.theme->colors.border, m.separatorThickness});
            Draw::textElided(c, yta, text(), textColor(c));
            break;
        }
    }

private:
    // Står ett SYNLIGT barn före mig i raden? Då skiljer en linje oss.
    bool grannFöre() const
    {
        if (!parent())
            return false;
        for (const Widget* b : parent()->children()) {
            if (b == this)
                return false;
            if (b->isVisible())
                return true;
        }
        return false;
    }

    TrText text_;
    bool   fill_ = false;
};

// ------------------------------------------------------------ fälten

StatusBar::Cell* StatusBar::cell(int index) const
{
    if (index < 0)
        return nullptr;
    int i = 0;
    for (Widget* b : children())
        if (auto* f = dynamic_cast<Cell*>(b)) {
            if (i == index)
                return f;
            ++i;
        }
    return nullptr;
}

int StatusBar::panelCount() const
{
    int n = 0;
    for (const Widget* b : children())
        if (dynamic_cast<const Cell*>(b))
            ++n;
    return n;
}

void StatusBar::addCell(StatusPanel p)
{
    Cell* f = add<Cell>(std::move(p));
    if (!isEnabled())
        f->disable();   // ett fält i en inaktiverad rad är inaktiverat
}

void StatusBar::addPanel(TrText text, float width, bool fill)
{
    addCell(StatusPanel{std::move(text), width, fill});
}

void StatusBar::setPanel(int index, TrText text, float width, bool fill)
{
    Cell* f = cell(index);
    if (!f) {
        gfx::log("StatusBar(%s): setPanel(%d) utanför intervallet (%d fält) "
                 "— ignoreras", name.c_str(), index, panelCount());
        return;
    }
    f->set(StatusPanel{std::move(text), width, fill});
}

// PÅ PLATS (headern): de befintliga fälten får de nya värdena, överskottet
// tas bort (ett ägt barn förstörs) och det som saknas läggs till sist — så
// ordningen mot appens egna barn står kvar.
void StatusBar::setPanels(std::vector<StatusPanel> panels)
{
    std::vector<Cell*> gamla;
    for (Widget* b : children())
        if (auto* f = dynamic_cast<Cell*>(b))
            gamla.push_back(f);
    const std::size_t lika = std::min(gamla.size(), panels.size());
    for (std::size_t i = 0; i < lika; ++i)
        gamla[i]->set(std::move(panels[i]));
    for (std::size_t i = lika; i < gamla.size(); ++i)
        unadopt(*gamla[i]);
    for (std::size_t i = lika; i < panels.size(); ++i)
        addCell(std::move(panels[i]));
}

// `enabled` kaskadar inte i ramverket — men fälten är INREDNING, och en
// inaktiverad rad vars fält lyste hade ljugit (talfältets regel). Bara
// fälten: appens egna barn i raden släcker appen själv.
void StatusBar::enabledChanged()
{
    for (Widget* b : children())
        if (auto* f = dynamic_cast<Cell*>(b)) {
            if (isEnabled())
                f->enable();
            else
                f->disable();
        }
}

void StatusBar::setPanelText(int index, TrText text)
{
    Cell* f = cell(index);
    if (!f) {
        gfx::log("StatusBar(%s): setPanelText(%d) utanför intervallet (%d "
                 "fält) — ignoreras", name.c_str(), index, panelCount());
        return;
    }
    f->setText(std::move(text));
}

StatusPanel StatusBar::panel(int index) const
{
    const Cell* f = cell(index);
    return f ? f->data() : StatusPanel{};
}

const std::string& StatusBar::panelText(int index) const
{
    static const std::string tom;
    const Cell* f = cell(index);
    return f ? f->text() : tom;
}

// ------------------------------------------------------------ höjden

gfx::Sizef StatusBar::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // Samma form som Bar::contentSize, men default-höjden är
        // statusradens egen (`Style::statusBarH`, inte ett tal i widget-filen).
        const float h = height > 0.0f ? height : c.style->statusBarH;
        return gfx::Sizef{width, h};
    }
    }
}

} // namespace glaze
