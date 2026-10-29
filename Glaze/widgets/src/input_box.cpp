// Inmatningsrutan — se glaze/input_box.h för API:t och besluten.
//
// Rutan är ett VANLIGT modalt AppWindow byggt av vanliga widgets, och den är
// meddelanderutans syskon rakt igenom: mitt över ägaren, måtten `msg*`,
// mätningen i `onLoad`, knapparna med `setDialogResult` och Enter/Escape som
// fönstrets standard- och avbrytknapp. Det som ÄR rutans eget är fältet och
// svaret ur det.

#include "glaze/input_box.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/button.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/text_area.h"
#include "glaze/widgets/textinput.h"

#include "dialog_layout.h"
#include "input_box_window.h"

namespace glaze {

InputBoxWindow::InputBoxWindow(const TrText& prompt, const TrText& caption,
                               const std::string& initial, InputBoxMode mode)
    : AppWindow(caption)
{
    setModal(true);
    // Mitt över ägaren, som meddelanderutan.
    setStartPosition(StartPosition::CenterOwner);
    setMinimizeBox(false);
    setMaximizeBox(false);
    // Ingen programikon i dialogens namnlist.
    appIcon.hide();

    prompt_       = add<Label>(prompt);
    prompt_->name = "prompt";
    prompt_->setWrap(true);
    // Fältet efter läget: ett `TextInput` (Enter är OK) eller ett `TextArea`
    // (Enter är en ny rad — rutan stängs med OK-knappen). Samma namn i båda.
    if (mode == InputBoxMode::MultiLine) {
        area_  = add<TextArea>(initial);
        field_ = area_;
    } else {
        line_  = add<TextInput>(initial);
        field_ = line_;
    }
    field_->name = "field";

    // KNAPPARNA BÄR SVARET: OK är standardknappen och
    // Avbryt avbrytknappen — Enter i fältet committar det och trycker OK,
    // Escape trycker Avbryt. (I flerradsläget tar `TextArea` Enter för en ny
    // rad, och OK trycks med knappen.)
    ok_       = add<Button>(tr(str::Ok));
    ok_->name = "ok";
    ok_->setDialogResult(DialogResult::OK);
    cancel_       = add<Button>(tr(str::Cancel));
    cancel_->name = "cancel";
    cancel_->setDialogResult(DialogResult::Cancel);
    for (Button* b : {ok_, cancel_})
        b->anchors = {false, false, true, true};
    setAcceptButton(*ok_);
    setCancelButton(*cancel_);

    // Mätningen kräver en font, och fonten finns först när fönstret öppnats
    // — meddelanderutans skäl.
    // FÖRVALET MARKERAT med fokus i fältet — man skriver över det eller
    // trycker Enter. Bara vid öppningen: ett språkbyte lägger om rutan men
    // rör inte det användaren skrivit.
    onLoad = [this] {
        layoutAndSize();
        field_->focus();
        if (area_)
            area_->selectAll();
        else
            line_->selectAll();
    };
}

InputBoxWindow::~InputBoxWindow() = default;

const std::string& InputBoxWindow::text() const
{
    return area_ ? area_->text() : line_->text();
}

void InputBoxWindow::onLanguageChanged()
{
    layoutAndSize();
}

// LAYOUTEN — ledtexten radbruten mot brytbredden, fältet lika brett under
// den, knapparna högerställda med KUMULATIVA `right` (dialogernas
// gemensamma knapprad, dialog_layout.h). Allt i LOGISKA px ur stilen.
void InputBoxWindow::layoutAndSize()
{
    const Ctx    c     = makeCtx();
    const Style& m     = *c.style;
    const float  bredd = m.msgTextMaxW;   // fältet ÄR brytbredden brett

    // Ledtexten — BREDDEN satt, så radbrytningens höjd följer den (label.h).
    const gfx::Sizef t = Draw::measureWrapped(c, prompt_->text(), bredd);
    prompt_->x     = m.msgPad;
    prompt_->y     = m.msgPad;
    prompt_->width = bredd;

    // Fältet — höjden SATT till fältets egen AUTO-höjd (textinput.cpp:
    // radhöjden och `fieldPadY`; flerradsfältets `heightForRows` för stilens
    // `textAreaRows`), så summan nedan är fältets mått och ingen andra
    // uträkning av det.
    const float fältH = area_ ? area_->heightForRows(c, m.textAreaRows)
                              : Draw::lineHeight(c) + 2.0f * m.fieldPadY;
    field_->x      = m.msgPad;
    field_->y      = m.msgPad + std::ceil(t.h) + m.msgGap;
    field_->width  = bredd;
    field_->height = fältH;

    // Knappraden — dialogernas gemensamma (dialog_layout.h).
    const dialoglayout::ButtonRow rad =
        dialoglayout::placeButtonRow(c, {ok_, cancel_});

    // Storleken — innehållsytan, låst till klientstorleken (dialog_layout.h).
    const float contentW = std::max(2.0f * m.msgPad + bredd,
                                    2.0f * m.msgPad + rad.w);
    const float contentH = field_->y + fältH + m.msgGap + rad.h + m.msgPad;
    dialoglayout::lockSize(*this, contentW, contentH);
}

// ------------------------------------------------------------ API:t

std::optional<std::string> inputBox(AppWindow& owner, const TrText& prompt,
                                    const TrText&      caption,
                                    const std::string& initial,
                                    InputBoxMode       mode)
{
    InputBoxWindow w(prompt, caption, initial, mode);
    w.run(owner);
    if (!w.accepted())
        return std::nullopt;   // Avbryt, Escape, krysset, kaskaden
    return w.text();
}

std::optional<std::string> inputBox(const TrText&      prompt,
                                    const TrText&      caption,
                                    const std::string& initial,
                                    InputBoxMode       mode)
{
    // ÄGARLÖS: `run()` svarar med en exit-kod, och svaret läses ur
    // `dialogResult()` (meddelanderutans väg).
    InputBoxWindow w(prompt, caption, initial, mode);
    w.run();
    if (!w.accepted())
        return std::nullopt;
    return w.text();
}

} // namespace glaze
