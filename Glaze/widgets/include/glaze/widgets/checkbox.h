#pragma once
#include "glaze/widget.h"

// Checkbox — mallen för en widget med eget värde (checked) och onChange-
// event (värdet läses ur sender, inte ur args). MÖNSTER: widgeten ritar
// SIG SJÄLV — draw()/contentSize() bor i src/checkbox.cpp med switch
// (c.ui) och ritar via c.r + de delade idiomen i glaze/draw.h (hård
// regel 3). En EGEN widget utanför ramverket ser exakt likadan ut.
//
// TANGENTEN är SPACE. Enter växlar inte (som
// i Win32-dialoger, WinForms och macOS): den bubblar till fönstrets
// standardknapp, så Enter i en dialog inte beror på vilken kontroll som
// råkar ha fokus.
//
// TRE LÄGEN (WinForms `ThreeState`/`CheckState`):
// rutan är ikryssad, tom eller i ett MELLANLÄGE (ett streck — "några av dem",
// "blandat"). `setThreeState(true)` styr BARA användarens klick, som i
// WinForms: utan det växlar ett klick mellan tom och ikryssad, och ett
// mellanläge appen satt blir tomt; med det går klicket
//     Unchecked → Checked → Indeterminate → Unchecked
// (WinForms ordning, inte Qt:s). Appen får sätta mellanläget i båda fallen.
// `isChecked()` är sann för Checked OCH Indeterminate — WinForms och Qt säger
// båda att en mellanlägesruta är "checked" — och `checkState()` skiljer dem.
// `setChecked(på)` är `setCheckState(på ? Checked : Unchecked)`.
//
// I `.glazeui` är båda setter-fält:
//     kryss.setThreeState(true);
//     kryss.setCheckState(glaze::CheckState::Indeterminate);

namespace glaze {

// Kryssrutans läge — WinForms `System.Windows.Forms.CheckState`, på
// namnrymdsnivå så att en lista med kryss kan låna den.
enum class CheckState { Unchecked, Checked, Indeterminate };

class Checkbox : public Widget {
public:
    explicit Checkbox(TrText t = {}) : text_(std::move(t)) {}

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // TEXTEN ÄR ETT TOKEN — literal ELLER
    // översättnings-id. `setText("Hej")` kompilerar oförändrat;
    // `setText(glaze::tr(1042))` är översättningsformen. Getter-paret och
    // skälen står utskrivna i `label.h` (mallen).
    void setText(TrText t)
    {
        if (text_ == t)
            return;
        text_ = std::move(t);
        invalidate();
    }
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // Programmatisk sättning avfyrar ALDRIG onChange — det gäller varje
    // widget (kontraktet stod i textinput.h och generaliseras här).
    // `setChecked(på)` är `setCheckState(på ? Checked : Unchecked)`, och
    // `isChecked()` är sann också i mellanläget (filhuvudet).
    void setChecked(bool on)
    {
        setCheckState(on ? CheckState::Checked : CheckState::Unchecked);
    }
    bool isChecked() const { return state_ != CheckState::Unchecked; }

    void setCheckState(CheckState s)
    {
        if (state_ == s)
            return;
        state_ = s;
        invalidate();
    }
    CheckState checkState() const { return state_; }

    // Går ANVÄNDARENS klick genom mellanläget? (filhuvudet) Ändrar inte
    // läget — bara vad nästa klick gör.
    void setThreeState(bool on) { threeState_ = on; }
    bool isThreeState() const { return threeState_; }

    // Avfyras när användaren bytt läge — läs det ur sender (`checkState()`,
    // eller `isChecked()` för en ruta med två lägen).
    Handler<ChangeArgs> onChange;
    // ANVÄNDAREN ÄR KLAR — fyras direkt efter `onChange` för samma
    // växling: en kryssruta har inget halvfärdigt läge. Finns på varje
    // inmatande widget, så en app kan lyssna på samma event överallt.
    Handler<ChangeArgs> onCommit;

    // TEXTFÄRGEN — `Button::setColor`s form och
    // regel: `std::nullopt`/`{}` ger temat, bara TEXTEN tar färgen (rutan och
    // märket följer temat — en röd bock hade lästs som ett fel i krysset),
    // och en inaktiv ruta dimmas till `textDim`.
    void setColor(std::optional<gfx::Color> col)
    {
        if (color_ == col)
            return;
        color_ = col;
        invalidate();
    }
    std::optional<gfx::Color> color() const { return color_; }

    bool focusable() const override { return true; }

    // setProp-egenskaper: text (alias title — regel 4-arv), checked,
    // threeState och checkState (`unchecked`/`checked`/`indeterminate` —
    // enumets namn gemena, den form verktygsvägen levererar; ett annat namn
    // ⇒ false).
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT text — regel 8:s tr-väg. Utan den blir
    // `setText(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text" || key == "title") { setText(value); return true; }
        return Widget::setTextToken(key, value);   // basens toolTip
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    void       clicked() override;
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;

private:
    gfx::Rectf boxRect(const Ctx& c) const;   // rutan (logiska px)

    // Rutans kropp per tillstånd (vila/hover/tryck/inaktiv) — plattans
    // grammatik, samma trappa som Buttons fillColor.
    gfx::Color boxFill(const Ctx& c) const;

    // MÄRKET i rutan — bocken (Checked) eller strecket (Indeterminate) i
    // pennans färg; ingenting när rutan är tom. Alla tre ritningarna delar
    // det, så märket ser likadant ut i varje familj.
    void drawMark(const Ctx& c, const gfx::Rectf& box, gfx::Color col) const;

    // Textens färg: appens `color()` på en aktiv ruta, annars basens
    // `textColor` (temat, `textDim` inaktiv). Bara texten frågar.
    gfx::Color labelColor(const Ctx& c) const
    {
        return isEnabled() && color_ ? *color_ : textColor(c);
    }

private:
    TrText      text_;
    CheckState  state_      = CheckState::Unchecked;
    bool        threeState_ = false;
    std::optional<gfx::Color> color_;   // tom = temats textfärg
};

} // namespace glaze
