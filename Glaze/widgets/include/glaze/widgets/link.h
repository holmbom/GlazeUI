#pragma once
#include "glaze/widget.h"

// Link — klickbar länk (wxHyperlinkCtrl, WinForms LinkLabel). MÖNSTER:
// widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/link.cpp med switch (c.ui) och ritar via Draw:: (hård
// regel 3). Färgen är en egen temaROLL (link/linkHover), inte `accent`:
// vad en länk ser ut som är palettens val, inte widgetens.
//
// `url` tom = ren klickhanterare (bara onClick). Är den satt öppnas den i
// systemets standardhanterare via gfx::openUrl EFTER att onClick körts —
// appens handler får alltid chansen först.
//
// Fokuserbar som en knapp: Space/Enter aktiverar, fokusringen ritas runt
// hela ytan.

namespace glaze {

class Link : public Widget {
public:
    explicit Link(TrText t = {}) : text_(std::move(t)) {}

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

    // Tom url = ingen OS-handling, bara onClick.
    void setUrl(std::string u) { url_ = std::move(u); }
    const std::string& url() const { return url_; }

    bool focusable() const override { return true; }

    // setProp-egenskaper: text (alias title — regel 4-arv), url.
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

    // HANDEN över hela länken — samma yta som klicket träffar
    // (`clicked()` fyras för widgetens rect, inte bara för textens).
    gfx::PointerShape cursorAt(const Ctx&, float, float) const override
    {
        return gfx::PointerShape::Hand;
    }

private:
    // Textens rect i LOGISKA px — understrykningen följer TEXTENS bredd,
    // inte widgetens, så båda räknas ur samma sanning.
    gfx::Rectf textBounds(const Ctx& c) const;
    gfx::Color linkColor(const Ctx& c) const;   // vila/hover/inaktiverad

private:
    TrText      text_;
    // Ingen invalidering: url:en RITAS inte — den är ren handling.
    std::string url_;
};

} // namespace glaze
