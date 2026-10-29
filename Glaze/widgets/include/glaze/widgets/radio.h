#pragma once
#include "glaze/widget.h"

// Radio — envalsknappen (wxRadioButton, WinForms RadioButton). MÖNSTER:
// widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/radio.cpp med switch (c.ui) och ritar via Draw:: (hård
// regel 3). Speglar checkbox.h rad för rad, med en RING i stället för en
// ruta.
//
// GRUPPEN kräver ingen ramverksmekanik: en grupp = SYSKON under samma
// förälder med samma `group`-sträng (tom sträng = alla radios under den
// föräldern — WinForms gör precis så). Två grupper i samma panel kräver
// alltså olika `group`; radios i olika paneler är alltid olika grupper.
//
// Gruppen är ETT tabbstopp: focusable() svarar DYNAMISKT — stoppet bärs av
// den FÖRSTA ikryssade, annars av den första i gruppen. Bara SYNLIGA och
// AKTIVERADE syskon räknas: en inaktiverad radio filtreras ändå bort av
// AppWindow::collectFocusable, så räknade den som ägare skulle gruppen
// tappa sitt tabbstopp helt. Piltangenter flyttar valet inom gruppen och
// SPACE väljer; Enter väljer inte (kryssrutans regel)
// utan bubblar till fönstrets standardknapp.
//
// ENVALET ÄR HÄRLETT. Varje radio bär en egen
// FLAGGA och en STÄMPEL: `setChecked(true)` sätter flaggan och en ny stämpel,
// `setChecked(false)` släcker flaggan — och `setProp("checked")` är samma
// sättare. Gruppen VISAR den flaggade med den SENAST satta stämpeln:
// `isChecked()`, ritningen och tabbstoppet läser det läget, aldrig flaggan
// rakt. Skälet är `.glazeui`: i den kompilerade filen körs `setChecked` FÖRE
// trädbygget (adopt står sist), alltså utan syskon, medan previewn kör samma
// satser i ett färdigt träd. En sättare som kryssade ur syskonen hade sett
// dem bara i previewn — två prickar i appen, en i previewn. Det härledda
// läget beror bara på satserna och deras ordning, inte på när syskonen kom.
//
// ANVÄNDARENS VAL (klick, Space, pilarna) släcker syskonens flaggor, som
// WinForms — också ett val av den radio som redan visas (det fyrar inget
// event). PRISET, skrivet: utan ett användarval emellan gör
// `setChecked(false)` på den VISADE radion att den som var satt före den syns
// igen — dess flagga står kvar. Vill koden ha en tom grupp släcker den alla.

namespace glaze {

class Radio : public Widget {
public:
    explicit Radio(TrText t = {}) : text_(std::move(t)) {}

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

    // Programmatisk sättning avfyrar ALDRIG onChange. `true` gör radion till
    // gruppens VISADE (den senast satta vinner, se filhuvudet) utan att röra
    // syskonens flaggor; `false` släcker den egna. Gruppen ritas om bara när
    // det visade läget ändras.
    void setChecked(bool on);
    // Det VISADE läget: flaggan satt och inget syskon i gruppen satt senare.
    bool isChecked() const;

    void setGroup(std::string g)
    {
        if (group_ == g)
            return;
        invalidateGroup();   // den gamla gruppens visade radio kan bytas
        group_ = std::move(g);
        invalidateGroup();   // … och den nyas
    }
    const std::string& group() const { return group_; }

    // Avfyras när DEN HÄR radion blev vald — läs värdet ur sender.
    // Syskonet som kryssades ur fyrar ingenting (WinForms-semantiken).
    Handler<ChangeArgs> onChange;
    // ANVÄNDAREN ÄR KLAR — direkt efter `onChange`, för samma val.
    Handler<ChangeArgs> onCommit;

    // TEXTFÄRGEN — kryssrutans tvilling, samma
    // rad: `std::nullopt`/`{}` ger temat, bara TEXTEN tar färgen (ringen och
    // pricken följer temat), och en inaktiv radio dimmas till `textDim`.
    void setColor(std::optional<gfx::Color> col)
    {
        if (color_ == col)
            return;
        color_ = col;
        invalidate();
    }
    std::optional<gfx::Color> color() const { return color_; }

    // Dynamiskt: gruppen ska vara ETT tabbstopp (se filhuvudet).
    bool focusable() const override;

    // setProp-egenskaper: text (alias title — regel 4-arv), checked, group.
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
    gfx::Rectf boxRect(const Ctx& c) const;   // ringen (logiska px)
    gfx::Rectf dotRect(const Ctx& c) const;   // pricken inuti ringen

    // Ringens kropp per tillstånd (vila/hover/tryck/inaktiv) — plattans
    // grammatik, samma trappa som Checkbox::boxFill.
    gfx::Color boxFill(const Ctx& c) const;

    bool sameGroup(const Radio& r) const { return r.group_ == group_; }

    // Ritar om mig och gruppens syskon — det visade läget är gruppens.
    void   invalidateGroup();
    // ANVÄNDARENS val: flaggan och en ny stämpel, syskonens flaggor
    // släckta (alltid), därefter onChange/onCommit — bara när det VISADE valet
    // ändrades. false = en handler rev eller flyttade radion, och den får
    // inte röras igen (`Widget::firePair`).
    bool   select();
    Radio* neighbour(int dir) const; // nästa/föregående i gruppen (wrap)

    // Textens färg: appens `color()` på en aktiv radio, annars basens
    // `textColor` (temat, `textDim` inaktiv). Bara texten frågar.
    gfx::Color labelColor(const Ctx& c) const
    {
        return isEnabled() && color_ ? *color_ : textColor(c);
    }

private:
    TrText        text_;
    bool          checked_ = false;   // FLAGGAN — det visade läget är isChecked()
    std::uint64_t stamp_   = 0;       // när flaggan senast sattes (växande)
    std::string   group_;   // tom = "alla radios under min förälder"
    std::optional<gfx::Color> color_;   // tom = temats textfärg
};

} // namespace glaze
