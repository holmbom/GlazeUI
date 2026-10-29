#pragma once
#include <optional>

#include <gfx/color.h>

#include "glaze/draw.h"
#include "glaze/widget.h"

// Button — mallen för en klickbar, fokuserbar widget (onClick + Space/
// Enter-aktivering). MÖNSTER: widgeten ritar SIG SJÄLV — draw()/
// contentSize() bor i src/button.cpp med switch (c.ui) och ritar med
// Draw:: (hård regel 3). Måtten kommer ur stilen (c.style), färgerna ur
// temat: inga tal och inga färger i widget-filen. En EGEN widget utanför
// ramverket ser exakt likadan ut.
//
// Bild: img är ett `ImageSource` — en asset (`glaze::img("lista","nyckel")`)
// eller pixlar (`glaze::Bitmap`) — aldrig en textur: texturen laddas upp
// lazily per fönster (c.images). Tom text ⇒ bara ikon; ogiltig bild ⇒ bara
// text (tyst, som en knapp utan bild). Ikonens sida = assetens storleksklass
// i logiska px, och för en Bitmap stilens `iconFallback` — bilden skalas in
// med GPU:ns filter, så den som vill ha den skarp i skala s ger sidan × s
// pixlar (samma villkor som för en asset).
//
// DIALOGKNAPPEN (WinForms `Button.DialogResult` och
// `PerformClick`): `setDialogResult(r)` gör att ett klick — mus, Space/Enter
// eller fönstrets Enter/Escape (`AppWindow::setAcceptButton`/
// `setCancelButton`) — stänger fönstret med `r` EFTER `onClick`. Och
// STANDARDKNAPPEN RITAS SOM STANDARD i ett fönster som har en acceptknapp:
// accentytan med `onAccent` (Glaze utan laminering, Minimalistics kant och
// Slates kontur i ytans färg) — den knapp Enter trycker just nu
// (`AppWindow::isDefaultButton`, Win32-modellen: en fokuserad knapp är
// standard, annars acceptknappen). Ett fönster utan acceptknapp ser ut som
// en vanlig knapp.

namespace glaze {

class Button : public Widget {
public:
    explicit Button(TrText t = {}) : text_(std::move(t)) {}

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

    // Ikonens placering relativt texten (ignoreras när texten är tom —
    // då centreras ikonen ensam). Typen är Draw:s generella ImagePos —
    // samma placering behövs av varje widget som bär bild + text.
    void setImg(ImageSource img)
    {
        if (img_ == img)
            return;
        img_ = std::move(img);
        invalidate();
    }
    const ImageSource& img() const { return img_; }

    void setImagePos(ImagePos p)
    {
        if (imagePos_ == p)
            return;
        imagePos_ = p;
        invalidate();
    }
    ImagePos imagePos() const { return imagePos_; }

    // IKONENS SIDA i LOGISKA px — 0 = AUTO
    // (assetens storleksklass, annars `iconFallback`, som ovan). En stor
    // asset krymps, och en Bitmap får en riktig storlek. Negativt ⇒ logg +
    // no-op. Samma namn på `ToolButton` och tabellerna.
    void  setIconSize(float px);
    float iconSize() const { return iconSize_; }

    // TEXTFÄRGEN — knappens EGEN, Labels namn och
    // trädnodens form: `std::nullopt` (eller `{}`) ger temats textfärg. Bara
    // TEXTEN tar färgen; plattan och kanten följer temat. Standardknappens
    // accentyta vinner (`onAccent`, som markeringen i en lista), och en
    // inaktiv knapp dimmas till `textDim`. Kontrasten är appens ansvar.
    // OBS: `Label::setColor({})` är SVART (`gfx::Color{}`) — där tar settern
    // en färg och inte en valfri, och C++:s `{}` betyder olika saker.
    void setColor(std::optional<gfx::Color> col)
    {
        if (color_ == col)
            return;
        color_ = col;
        invalidate();
    }
    std::optional<gfx::Color> color() const { return color_; }

    // DIALOGRESULTATET. Inte `None` ⇒ ett klick stänger
    // knappens fönster med värdet (`AppWindow::closeWithResult`), EFTER
    // `onClick` och bara om knappen och fönstret lever och knappen sitter
    // kvar där — stängningen är uppskjuten som alltid, så `onClose`-vetot
    // gäller. `None` (förvalet) är en vanlig knapp. En OK som VALIDERAR
    // sätter inget resultat och anropar `closeWithResult` själv när värdena
    // håller (filväljarens mönster), eller validerar i fönstrets `onClose`.
    // Ritningen och fokus rörs inte, så settern invaliderar inte. I
    // `.glazeui`: `ok.setDialogResult(glaze::DialogResult::OK);`.
    void         setDialogResult(DialogResult r) { dialogResult_ = r; }
    DialogResult dialogResult() const { return dialogResult_; }

    // KLICKA PROGRAMMATISKT (WinForms `PerformClick`) — `onClick` och sedan
    // `dialogResult`, exakt det ett klick gör, och en no-op när knappen är
    // inaktiverad. Fönstrets Enter/Escape-steg går den här vägen, och en
    // grind som besvarar en dialog utan mus likaså. Kör användarkod: knappen
    // och fönstret kan vara rivna efteråt. Ingen `.glazeui`-form (den är en
    // handling, inte ett tillstånd) — previewn känner raden men utför den
    // inte.
    void performClick();

    bool focusable() const override { return true; }

    // setProp-egenskaper: text (alias title — äldre kod, regel 4-arv),
    // img ("lista/nyckel"), imagepos (left/right/above/below),
    // dialogResult (none/ok/cancel/yes/no/abort/retry/ignore — gemena, som
    // grammatikens `parseEnumTail` lämnar dem), iconSize (ett tal). Färgen
    // har ingen textform: `setColor(std::optional)` är en anropssats.
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
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;

    // KLICKET: `onClick` genom `fireEvent` (vakten), sedan `dialogResult`
    // (se ovan). Musen, Space/Enter och `performClick` går hit.
    void clicked() override;

    // Texten OVANPÅ fyllningen — `onAccent` på standardknappens accentyta,
    // annars appens `color()` på en aktiv knapp, annars temats.
    // Skyddad (inte privat) för en subklass som ritar EGET innehåll på
    // knappens yta: filväljarens ikonknapp ritar sin pil i den här färgen,
    // så den syns också i standardutseendet.
    gfx::Color labelColor(const Ctx& c) const;

private:
    Content    content(const Ctx& c) const;     // bild + text som en sak
    gfx::Color fillColor(const Ctx& c) const;   // vila/hover/tryck/inaktiv
    float      radius(const Ctx& c, const gfx::Rectf& b) const;  // piller?

    // Ritas knappen som STANDARD just nu? (`AppWindow::isDefaultButton`;
    // utan fönster aldrig.) Fyllningen, textfärgen och kanten frågar den.
    bool       shownAsDefault() const;

    // Glaze-formens EGNA rit-steg utöver kropp och kontur. Lamineringen är
    // `Draw::laminate`, eftersom fem widgets ritar samma ljusfält och en
    // effekt som delas av fem är en PRIMITIV, inte en knappdetalj.
    gfx::Rectf contentRect(const Ctx& c, const gfx::Rectf& b) const;

private:
    TrText       text_;
    ImageSource  img_{};                     // ogiltig = ingen bild
    ImagePos     imagePos_     = ImagePos::Left;
    DialogResult dialogResult_ = DialogResult::None;
    float        iconSize_     = 0.0f;       // 0 = AUTO
    std::optional<gfx::Color> color_;        // tom = temats textfärg
};

} // namespace glaze
