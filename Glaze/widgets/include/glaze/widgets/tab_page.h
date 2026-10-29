#pragma once
#include "glaze/widgets/panel.h"

// TabPage — EN FLIKSIDA (WinForms TabPage, wxNotebook-
// sidan). En `Panel` vars `text` är FLIKENS RUBRIK: sidan bor i en
// `TabControl`, och flikkontrollen ritar rubriken i sin flikrad och visar
// den valda sidan i ytan under. MÖNSTER: widgeten ritar SIG SJÄLV —
// draw()/contentSize() bor i src/widgets/tab_page.cpp med switch (c.ui)
// (hård regel 3).
//
// SIDAN ÄR ETT RIKTIGT BARN, och dess innehåll är dess barn — det är hela
// formen, och skälet är regel 8: ett kedjat `flikar.page(0).adopt(knapp)`
// går inte att emittera i grammatiken, medan
//
//     flikar.adopt(sidaAllmant);
//     sidaAllmant.adopt(knapp);
//
// är trädbyggets egna satser. Rubriken är en vanlig setter-rad
// (`sidaAllmant.setText(glaze::tr(1050));`), och sidan är en Panel: den
// klipper, scrollar och tar `setBorder`/`showScrollbars` som vilken panel
// som helst (WinForms TabPage ärver Panel av samma skäl).
//
// YTAN ÄR FLIKKONTROLLENS. Sidan ritar ingenting själv — flikkontrollen
// fyller och ramar in hela visningsytan, också när sidan saknas — utom sin
// ram när `setBorder(true)` är satt, och då som en Panel. Minimalistics
// panelfyllning (panel.cpp) hade legat ovanpå flikkontrollens yta i samma
// färg.
//
// SYNLIGHETEN ÄGS AV FLIKKONTROLLEN: den VALDA sidan syns och de andra är
// dolda (`hide()`), och det är vad som gör att en dold sidas kontroller
// varken ritas, träffas eller nås med Tab. Välj sida med
// `TabControl::setSelectedIndex` — `show()` på en sida som inte är vald
// lägger den OVANPÅ den valda. En sida som tas bort ur flikkontrollen
// lämnar den synlig.
//
// En sida UTANFÖR en flikkontroll är en vanlig panel; rubriken syns inte då.
// INTE HÄR: en ikon i fliken, en tooltip på själva fliken (en tooltip satt
// på sidan visas över SIDANS yta, som på varje widget).

namespace glaze {

class TabPage : public Panel {
public:
    explicit TabPage(TrText t = {}) : text_(std::move(t)) {}

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR. Rubriken
    // ritas i FÖRÄLDERNS flikrad, så settern invaliderar föräldern — en flik
    // med ny text byter också bredd, och då flyttar flikarna efter den.
    //
    // TEXTEN ÄR ETT TOKEN — literal ELLER översättnings-id.
    // Getter-paret och skälen står i `label.h` (mallen).
    void setText(TrText t);
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // setProp-egenskaper: text, plus Panels border/clip och basens. Inget
    // `title`-alias — en ny widget har inga äldre filer att vara snäll mot.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT rubrik — regel 8:s tr-väg. Utan den blir
    // `setText(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text") { setText(value); return true; }
        return Panel::setTextToken(key, value);   // basens toolTip
    }

protected:
    void draw(const Ctx& c) override;

private:
    TrText text_;
};

} // namespace glaze
