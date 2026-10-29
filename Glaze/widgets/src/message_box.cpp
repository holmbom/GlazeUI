// Meddelanderutan — se glaze/message_box.h för API:t och besluten.
//
// Rutan är ett VANLIGT modalt AppWindow byggt av vanliga widgets. Nästan
// ingenting här är nytt: `run(owner)` + modalfiltret, `setContentSize`,
// storleksgränserna, `Button`s autostorlek, `Label`s radbrytning
// och `Draw::circle`/`text` fanns redan — och
// SVARET: varje knapp bär sitt `DialogResult` (`Button::setDialogResult`),
// och Enter och Escape är fönstrets standard- och avbrytknapp
// (`setAcceptButton`/`setCancelButton`), samma väg som varje dialog. Det som
// ÄR rutans eget är LAYOUTEN och RESULTATMAPPNINGEN.

#include "glaze/message_box.h"

#include <cmath>
#include <string>
#include <vector>

#include <gfx/platform.h>

#include "glaze/draw.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/picture.h"
#include "glaze/widgets/button.h"

#include "dialog_layout.h"
#include "message_box_table.h"
#include "status_badge.h"

namespace glaze {
namespace {

// Knapptabellen (uppsättning -> knappar, resultat och avbryt-ekvivalent)
// bor i `message_box_table.h`: den är ren std och grindas headless av
// tests/msgbox_gate.cpp. Allt annat här kräver ett fönster.
using msgbox::ButtonSpec;
using msgbox::buttonSpecs;
using msgbox::cancelEquivalent;
using msgbox::hasCancelPath;

// IKON-BADGEN bor i den interna `status_badge.h` (samma mapp, samma
// prejudikat som tabellen ovan). `StatusBadge::contentSize()` är
// LEVANDE mätkod — `Widget::resolve` frågar den ovillkorligt, och
// implementationen läser inte `width`/`height` alls — och klassen står
// därför i en header, så att den går att grinda.

// ------------------------------------------------------------- fönstret

class MessageBoxWindow final : public AppWindow {
public:
    MessageBoxWindow(const std::string& text, const std::string& caption,
                     MessageBoxButtons buttons, MessageBoxType type,
                     const ImageSource& customIcon)
        : AppWindow(caption)
    {
        setModal(true);
        // Mitt över ägaren, som WinForms MessageBox —
        // utan en synlig ägare mitt på skärmen.
        setStartPosition(StartPosition::CenterOwner);

        // Krysset släcks när uppsättningen saknar avbryt — annars hade
        // fönstret erbjudit en utväg som inte har något svar.
        if (!hasCancelPath(buttons))
            setControlBox(false);
        setMinimizeBox(false);
        setMaximizeBox(false);
        // Ramverkets dialoger visar ingen programikon i namnlisten
        // — OS-ikonen står kvar, `hide()` är ortogonalt mot den.
        appIcon.hide();

        if (type == MessageBoxType::Custom) {
            if (customIcon.valid()) {
                pic_ = add<Picture>();
                pic_->setImg(customIcon);
                pic_->setFit(Picture::Fit::Fit);
            }
        } else if (customIcon.valid()) {
            // Egen bild vinner över default-badgen även för en typad ruta:
            // appen har sagt exakt vad den vill se.
            pic_ = add<Picture>();
            pic_->setImg(customIcon);
            pic_->setFit(Picture::Fit::Fit);
        } else {
            badge_ = add<StatusBadge>(type);
        }

        msg_ = add<Label>(text);
        msg_->setWrap(true);

        // KNAPPARNA BÄR SVARET: ett klick — mus, Space/
        // Enter på den fokuserade eller fönstrets Enter/Escape — stänger med
        // knappens resultat. AVBRYTKNAPPEN är den vars svar är
        // avbryt-ekvivalenten, och bara när uppsättningen HAR en väg dit:
        // YesNo och AbortRetryIgnore får ingen, och Escape gör där ingenting
        //. OK-uppsättningens enda knapp blir både standard- och
        // avbrytknapp.
        for (const ButtonSpec& spec : buttonSpecs(buttons)) {
            auto* b = add<Button>(tr(spec.textId));
            b->anchors = {false, false, true, true};
            b->setDialogResult(spec.result);
            if (hasCancelPath(buttons) &&
                spec.result == cancelEquivalent(buttons))
                setCancelButton(*b);
            btns_.push_back(b);
        }
        // STANDARDKNAPPEN är den FÖRSTA — fokus står där när rutan öppnas
        // (`layoutAndSize`), och Enter trycker den så länge fokus inte flyttats
        // till en annan knapp, som då själv tar sitt Enter.
        if (!btns_.empty())
            setAcceptButton(*btns_.front());

        // Startlogik hör hemma i onLoad — mätningen kräver en font, och
        // fonten finns först när fönstret öppnats (`reloadFont` i
        // `showOwned`, som kör FÖRE den här avfyrningen).
        onLoad = [this] { layoutAndSize(); };
    }

    // SPRÅKBYTET. Rutan mäter och sätter EXPLICITA
    // bredder EN gång, i `onLoad` — så `App::invalidateAll()` ritar om
    // men layoutar INTE om, och en finsk knapptext hade klippts i en
    // ruta som redan står öppen. Kroken kör om hela mätningen.
    //
    // Knapparnas TEXT behöver inte röras: de bär `TrText`-TOKEN och
    // löser upp sig själva. Det är MÅTTEN som är frusna.
    void onLanguageChanged() override
    {
        layoutAndSize();
    }

private:
    // LAYOUTEN. Allt i LOGISKA px ur `c.style`; talen finns inte här.
    void layoutAndSize()
    {
        const Ctx    c = makeCtx();
        const Style& m = *c.style;

        // 1. Texten mäts radbruten mot stilens brytbredd. Höjden faller ut.
        const gfx::Sizef t = Draw::measureWrapped(c, msg_->text(),
                                                  m.msgTextMaxW);
        const float textW = std::ceil(t.w);

        // 2. Ikonen. IKONLÖST reserverar ingen ikonhöjd — en ruta utan ikon
        //    ska vara exakt så hög som sin text kräver.
        const bool  hasIcon = (badge_ != nullptr) || (pic_ != nullptr);
        const float iconW   = hasIcon ? m.msgIconSize + m.msgGap : 0.0f;
        const float bodyH   = std::max(t.h, hasIcon ? m.msgIconSize : 0.0f);

        // 3. Knappraden — mätt OCH placerad (högerställd, kumulativa
        //    `right`) av dialogernas gemensamma `placeButtonRow`
        //    (dialog_layout.h), som inmatningsrutan och färgdialogen delar.
        //
        //    Mätvägen är logisk, så `Button`s mått hade gått att fråga direkt;
        //    formeln räknas ändå om i hjälparen (golvet `msgBtnMinW` är
        //    dialogernas, inte knappens).
        const dialoglayout::ButtonRow rad =
            dialoglayout::placeButtonRow(c, btns_);

        // 4. Innehållsytan.
        const float contentW = std::max(2.0f * m.msgPad + iconW + textW,
                                        2.0f * m.msgPad + rad.w);
        const float contentH =
            2.0f * m.msgPad + bodyH + m.msgGap + rad.h;

        // 5. Placeringen. Ikon och text ligger mot ÖVERKANTEN av kroppen —
        //    en flerradig text ska börja i höjd med ikonen, inte glida ner
        //    under den.
        if (badge_) {
            badge_->x      = m.msgPad;
            badge_->y      = m.msgPad;
            badge_->width  = m.msgIconSize;
            badge_->height = m.msgIconSize;
        }
        if (pic_) {
            pic_->x      = m.msgPad;
            pic_->y      = m.msgPad;
            pic_->width  = m.msgIconSize;
            pic_->height = m.msgIconSize;
        }
        msg_->x     = m.msgPad + iconW;
        msg_->y     = m.msgPad;
        // BREDDEN sätts EXPLICIT, och det är kravet för radbrytningens
        // auto-höjd (se tabellen i label.h): `resolve()` frågar
        // `contentSize()` innan den räknar fram en sträckt bredd, så en
        // etikett utan satt bredd får ingen auto-höjd. Talet är den
        // mätta textbredden avrundad UPPÅT — samma brytpunkter som
        // mätningen gav, eftersom ingen rad är bredare än så.
        msg_->width = textW;

        // 6. Storleken — innehållsytan, låst till klientstorleken: de TVÅ
        //    ENHETERNA och avrundningen uppåt står vid `lockSize`
        //    (dialog_layout.h).
        dialoglayout::lockSize(*this, contentW, contentH);

        // 7. Fokus på FÖRSTA knappen — standardknappen. `Button::keyDown`
        //    gör Return till ett klick, så rutan går att svara på utan att
        //    röra musen, och Tab till en annan knapp gör DEN till standard.
        //    Escape är fönstrets avbrytknapp (konstruktorn).
        if (!btns_.empty())
            btns_.front()->focus();
    }

    StatusBadge*         badge_    = nullptr;
    Picture*             pic_      = nullptr;
    Label*               msg_      = nullptr;
    std::vector<Button*> btns_;
};

} // namespace

// ------------------------------------------------------------ API:t

DialogResult messageBox(AppWindow& owner, const std::string& text,
                        const std::string& caption,
                        MessageBoxButtons buttons, MessageBoxType type,
                        const ImageSource& customIcon)
{
    MessageBoxWindow win(text, caption, buttons, type, customIcon);
    win.run(owner);
    // `None` läcker aldrig ut: en stängning utan knapp (krysset, ägaren som
    // kaskadstänger) blir uppsättningens avbryt-ekvivalent — Escape är
    // avbrytknappen och bär sitt svar själv. Se message_box.h för tabellen.
    return win.dialogResult() == DialogResult::None
               ? msgbox::cancelEquivalent(buttons)
               : win.dialogResult();
}

DialogResult messageBox(const std::string& text, const std::string& caption,
                        MessageBoxButtons buttons, MessageBoxType type,
                        const ImageSource& customIcon)
{
    // ÄGARLÖS: `AppWindow::run()` (utan ägare) returnerar en exit-kod och
    // inte ett resultat — men modalstacken pushas ändå, och svaret läses ur
    // `dialogResult()`.
    MessageBoxWindow win(text, caption, buttons, type, customIcon);
    win.run();
    return win.dialogResult() == DialogResult::None
               ? msgbox::cancelEquivalent(buttons)
               : win.dialogResult();
}

} // namespace glaze
