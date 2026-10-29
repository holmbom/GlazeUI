#pragma once
#include "glaze/widget.h"

// GroupBox — GRUPPRAMEN (wxStaticBox, WinForms
// GroupBox). En ram med en RUBRIK I KANTEN: linjen bryts där rubriken står,
// och barnen bor innanför. MÖNSTER: widgeten ritar SIG SJÄLV —
// draw()/contentSize() bor i src/widgets/group_box.cpp med switch (c.ui) och
// ritar via Draw:: (hård regel 3); mallen är `button.h`/`panel.h`.
//
// EN CONTAINER, MEN INTE EN PANEL — och det är ett beslut:
//   * Panel är en SCROLLBAR placeringsyta (`setScroll`, hjulet, overlay-
//     remsorna, `setClip`/`setBorder`). En gruppram scrollar inte, klipper
//     alltid och har alltid sin ram — WinForms GroupBox ärver av samma skäl
//     Control och inte Panel/ScrollableControl. Som Panel-ättling hade den
//     ärvt ett hjul som flyttar barnen och två settrar som inte betyder
//     något här.
//   * Innehållsytan är en ANNAN form än panelens: överkanten är rubrikbandet,
//     inte en ramtjocklek — så `childArea`/`clipRect` hade fått skrivas om
//     ändå, och Panels `innerBounds` är inte virtuell.
//
// RUBRIKEN är `text` (TrText, regel 6 — texten heter `text` på alla
// widgets) och tar `glaze::tr(id)` via `setTextToken`. Tom text = ingen lucka
// i linjen; ramens läge och barnens yta är desamma ändå, så att tömma
// rubriken aldrig flyttar innehållet.
//
// RADIEN är `Style::groupRadius` och inte `panelRadius` — panelens radie är
// 0 i alla tre tabellerna och hör till stilvalen; att ge
// den ett värde hade rundat varje ramad Panel i varje app.
//
// AUTOSTORLEK UR BARNEN (axelregeln): en gruppram VET vad den innehåller —
// till skillnad från Panel, som med flit är 0×0 — så en AUTO-axel fylls ur
// `childrenExtent()` plus ramens luft och är minst så bred som rubriken.
// Det är kompositstödets opt-in, och gruppramen är dess första användare.
//
// `enabled` KASKADAR INTE (samma regel som Panel): en inaktiverad gruppram
// dimmar sin rubrik, och appen släcker barnen själv om den vill.

namespace glaze {

class GroupBox : public Widget {
public:
    explicit GroupBox(TrText t = {}) : text_(std::move(t)) {}

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern är
    // den som INVALIDERAR. LAYOUT-fälten är fortsatt RÅA, se widget.h.
    //
    // TEXTEN ÄR ETT TOKEN — literal ELLER översättnings-id.
    // Getter-paret och skälen står i `label.h` (mallen).
    void setText(TrText t)
    {
        if (text_ == t)
            return;
        text_ = std::move(t);
        invalidate();
    }
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // setProp-egenskaper: text. Inget `title`-alias — en ny widget har inga
    // äldre filer att vara snäll mot.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT rubrik — regel 8:s tr-väg. Utan den blir
    // `setText(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text") { setText(value); return true; }
        return Widget::setTextToken(key, value);   // basens toolTip
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Containerkrokarna — PUBLIKA på `Widget` (previewvandringen når dem
    // därigenom), protected här som i Panel. Barnen bor i innerytan och
    // klipps alltid mot den.
    gfx::Rectf childArea(const Ctx& c) const override;
    bool       clipsChildren() const override { return true; }
    gfx::Rectf clipRect(const Ctx& c) const override;
    float      clipRadius(const Ctx& c) const override;

private:
    // GEOMETRIN — EN sanning i widget-lokala LOGISKA px, och den tar ytan
    // `b` som argument: ritningen skickar `c.bounds()`, beteendet
    // (`childArea`/`clipRect`) skickar `Draw::bounds(c, resolve(c))`.
    //   rubrikband  [0, radhöjd] — rubriken står mitt i det
    //   ram         från bandets MITT och nedåt (linjen går genom rubriken)
    //   lucka       rubriken + `groupTitleGap` på båda sidor, i x
    //   innerytan   ramen minus `groupPad`, under rubrikbandet
    gfx::Rectf frameRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf titleRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf innerRect(const Ctx& c, const gfx::Rectf& b) const;

private:
    TrText text_;
};

} // namespace glaze
