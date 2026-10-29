#pragma once
#include <cstdint>

#include "glaze/app_window.h"

// TOOLTIPENS INTERNA DELAR — INTERN header, som
// `status_badge.h`: inget här är publikt API. Det publika är
// `Widget::setToolTip` (widget.h) och `AppWindow::toolTipWidget()`
// (app_window.h); mekaniken bor i tooltip.cpp.
//
// Klasserna står i en header och inte TU-lokalt av meddelanderutans skäl:
// `ToolTipBody::contentSize()` är LEVANDE mätkod som avgör fönstrets
// storlek, och den ska gå att grinda (tests/tooltip_gate.cpp når den här
// filen precis som msgbox_gate når `status_badge.h`). `registeredWidgetTypes`
// når den inte — den registreras aldrig, för den finns aldrig i en
// `.glazeui`.

namespace glaze {

// Ramverkets timer-id:n ligger HÖGT (caret-blinket är 0x80000001) — appens
// egna `Window::startTimer`-id ska ligga under.
inline constexpr std::uint32_t kToolTipTimerId = 0x80000002;

// PLATTAN MED TEXTEN — fyller tooltipfönstret helt. Lyder regel 3 fullt ut
// trots att den är intern: `switch (c.ui)`, mått ur Style, färger ur Theme.
class ToolTipBody final : public Widget {
public:
    ToolTipBody() { dock = Dock::Fill; }

    void setText(TrText t)
    {
        if (text_ == t)
            return;
        text_ = std::move(t);
        invalidate();
    }
    const std::string& text() const { return text_.str(); }

    // Plattans mått i LOGISKA px för en text: textblocket brutet mot
    // `toolTipMaxW` plus luften runt om. SAMMA uträkning som `draw()`
    // bryter efter, så fönstret och texten aldrig blir oense — publik för
    // fönstret, som måste känna storleken INNAN popupen visas (en
    // xdg-popup får sin storlek vid mappningen och kan inte växa efteråt).
    static gfx::Sizef measure(const Ctx& c, const std::string& text);

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

private:
    TrText text_;
};

// TOOLTIPFÖNSTRET — `Dropdown::ListWindow`s form: Chrome::None + popup-
// attributet (noActivate + toolWindow, och App stänger det vid ett tryck i
// ett annat fönster och vid fokusförlust). AppWindow-OBJEKTET återanvänds;
// OS-fönstret skapas per visning, eftersom en popup inte kan byta storlek
// efter att den visats.
class ToolTipWindow final : public AppWindow {
public:
    explicit ToolTipWindow(AppWindow& ägare);

    ToolTipBody& body() { return *body_; }

protected:
    // Ett tryck MITT I tooltipen (pekaren flyttades upp på den) stänger den
    // — tooltipen är information, aldrig ett mål.
    bool event(const gfx::Event& e) override;

private:
    ToolTipBody* body_ = nullptr;
};

} // namespace glaze
