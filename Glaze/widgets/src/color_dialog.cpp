// Färgdialogen — se glaze/color_dialog.h för API:t och besluten.
//
// Dialogen är ett VANLIGT modalt AppWindow byggt av vanliga widgets, och
// nästan ingenting här är nytt: ytan är färgväljarens (`ColorPanel`,
// ../color_panel.h, i dialogens läge), knapparna och Enter/Escape är
// fönstrets standard- och avbrytknapp, och måtten och layouten är
// meddelanderutans (`msg*`). Det som ÄR dialogens eget är FÖRHANDSRUTAN och
// att OK committar ett skrivet hexvärde innan färgen läses.

#include "glaze/color_dialog.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"

#include "color_dialog_window.h"
#include "color_panel.h"
#include "dialog_layout.h"

namespace glaze {

// ------------------------------------------------------- förhandsrutan

// Den VALDA färgen, så stor som en knapp och i knappraden — en dialog har
// inget fält bakom sig att se valet i. Över ett RUTMÖNSTER när alfan
// tillåts, som färgväljarens ruta.
class ColorSample final : public Widget {
public:
    void setColor(gfx::Color c, bool alfa)
    {
        if (c == färg_ && alfa == alfa_)
            return;
        färg_ = c;
        alfa_ = alfa;
        invalidate();
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            // AXELREGELN: satt axel vinner; AUTO är en knapp (dialogens
            // layout sätter båda, så det här är bara reserven).
            const auto& m = *c.style;
            gfx::Sizef  s{width, height};
            if (s.w > 0.0f && s.h > 0.0f)
                return s;
            if (s.w <= 0.0f) s.w = m.msgBtnMinW;
            if (s.h <= 0.0f) s.h = Draw::lineHeight(c) + 2.0f * m.buttonPadY;
            return s;
        }
        }
    }

    void draw(const Ctx& c) override
    {
        const gfx::Rectf b   = c.bounds();
        const auto&      m   = *c.style;
        const auto&      col = c.theme->colors;
        switch (c.ui) {
        // ALLA TRE FAMILJERNA RITAR RUTAN LIKADANT — ett beslut, paletten
        // och färgväljarens ruta bär skälet: den är INNEHÅLL och ska visa
        // exakt den färg man får. Konturen är kontrollkanten, så en vit ruta
        // syns mot en vit yta.
        case UI::Glaze:
        case UI::Minimalistic:
        case UI::Slate:
        default:
            if (alfa_)
                Draw::checker(c, b, m.colorSwatchRadius, m.colorCheckerCell,
                              col.panel, col.appWorkspace);
            Draw::rect(c, b, m.colorSwatchRadius, färg_,
                       Pen{col.buttonBorder, m.borderWidth});
            break;
        }
    }

private:
    gfx::Color färg_{};
    bool       alfa_ = false;
};

// -------------------------------------------------------------- fönstret

namespace {

TrText titelFör(const ColorDialog& cfg)
{
    return cfg.title().empty() ? tr(str::ColorDialogTitle) : cfg.titleToken();
}

} // namespace

ColorDialogWindow::ColorDialogWindow(ColorDialog& cfg)
    : AppWindow(titelFör(cfg)), cfg_(cfg), chosen_(cfg.color_)
{
    setModal(true);
    // Mitt över ägaren, som meddelanderutan och filväljaren.
    setStartPosition(StartPosition::CenterOwner);
    setMinimizeBox(false);
    setMaximizeBox(false);
    // Ingen programikon i dialogens namnlist.
    appIcon.hide();

    panel_ = add<ColorPanel>();
    panel_->setDialogMode(true);
    panel_->setAllowAlpha(cfg.allowAlpha());
    // Varje väg in i ytan är ett VAL — inget stänger dialogen (headern).
    panel_->onPick  = [this](gfx::Color f) { choose(f); };
    panel_->onColor = [this](gfx::Color f) { choose(f); };
    panel_->onHex   = [this](gfx::Color f, bool) { choose(f); };

    sample_       = add<ColorSample>();
    sample_->name = "sample";

    // OK FÖRE Avbryt i tabbordningen (adopt-ordningen), som i
    // inmatningsrutan; raden står OK · Avbryt, högerställd (layouten).
    ok_       = add<Button>(tr(str::Ok));
    ok_->name = "ok";
    ok_->setDialogResult(DialogResult::OK);
    ok_->onClick = [this] { accept(); };   // FÖRE stängningen (button.h)
    cancel_       = add<Button>(tr(str::Cancel));
    cancel_->name = "cancel";
    cancel_->setDialogResult(DialogResult::Cancel);
    for (Button* b : {ok_, cancel_})
        b->anchors = {false, false, true, true};
    setAcceptButton(*ok_);
    setCancelButton(*cancel_);

    choose(chosen_);
    // Mätningen kräver en font, och fonten finns först när fönstret öppnats
    // — meddelanderutans skäl.
    // Tangenterna börjar i paletten, som i färgväljarens popup — bara vid
    // öppningen: ett språkbyte lägger om rutan men flyttar inte fokus.
    onLoad = [this] {
        layoutAndSize();
        panel_->focusPalette();
    };
}

ColorDialogWindow::~ColorDialogWindow() = default;

gfx::Color ColorDialogWindow::selected() const
{
    return cfg_.allowAlpha() ? chosen_ : colorpanel::opak(chosen_);
}

void ColorDialogWindow::choose(gfx::Color c)
{
    chosen_ = cfg_.allowAlpha() ? c : colorpanel::opak(c);
    panel_->sync(chosen_);
    sample_->setColor(chosen_, cfg_.allowAlpha());
}

// OK — det SKRIVNA i hexfältet committas först (fokus står kvar där när
// knappen trycks med Enter eller `performClick`), sedan skrivs färgen.
void ColorDialogWindow::accept()
{
    commitEdits();
    cfg_.color_ = chosen_;
}

void ColorDialogWindow::onLanguageChanged()
{
    layoutAndSize();
}

// LAYOUTEN — ytan överst (dess egen luft står kvar; den placeras så att
// innehållet står `msgPad` från kanten), förhandsrutan och knapparna i en rad
// under, knapparna högerställda med KUMULATIVA `right` (dialogernas
// gemensamma knapprad, dialog_layout.h). Allt i LOGISKA px ur stilen.
void ColorDialogWindow::layoutAndSize()
{
    const Ctx        c   = makeCtx();
    const Style&     m   = *c.style;
    const gfx::Sizef yta = panel_->layout(c);
    const float      in  = std::max(0.0f, m.msgPad - m.colorPopupPad);
    panel_->x      = in;
    panel_->y      = in;
    panel_->width  = yta.w;
    panel_->height = yta.h;

    // Knappraden — dialogernas gemensamma (dialog_layout.h).
    const dialoglayout::ButtonRow rad =
        dialoglayout::placeButtonRow(c, {ok_, cancel_});

    const float radY = in + yta.h - m.colorPopupPad + m.msgGap;
    sample_->x      = m.msgPad;
    sample_->y      = radY;
    sample_->width  = m.msgBtnMinW;
    sample_->height = rad.h;

    const float contentW = std::max(2.0f * in + yta.w,
                                    2.0f * m.msgPad + m.msgBtnMinW +
                                        m.msgGap + rad.w);
    const float contentH = radY + rad.h + m.msgPad;
    dialoglayout::lockSize(*this, contentW, contentH);
}

// -------------------------------------------------------------- API:t

gfx::Color ColorDialog::color() const
{
    return allowAlpha_ ? color_ : colorpanel::opak(color_);
}

DialogResult ColorDialog::run(AppWindow& owner)
{
    ColorDialogWindow w(*this);
    w.run(owner);
    return w.accepted() ? DialogResult::OK : DialogResult::Cancel;
}

DialogResult ColorDialog::run()
{
    ColorDialogWindow w(*this);
    w.run();
    return w.accepted() ? DialogResult::OK : DialogResult::Cancel;
}

} // namespace glaze
