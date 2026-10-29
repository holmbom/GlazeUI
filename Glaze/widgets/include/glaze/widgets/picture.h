#pragma once
#include "glaze/widget.h"

// Picture — bildrutan (WinForms PictureBox, wxStaticBitmap). Ingen
// interaktion, inget tabbstopp: den visar EN bild. MÖNSTER: widgeten
// ritar SIG SJÄLV — draw()/contentSize() bor i src/widgets/picture.cpp med
// switch (c.ui) och ritar via Draw:: (hård regel 3).
//
// Bilden är ett `ImageSource` — en asset
// (`glaze::img("lista","nyckel")`, `.glazeui`:s form), pixlar
// (`glaze::Bitmap`, koddata — en miniatyr avkodad ur PNG-bytes) eller en
// `glaze::RenderTexture` — aldrig en textur: texturen laddas upp lazily per
// fönster (c.images), precis som Buttons img. Ogiltig bild ⇒ widgeten ritar
// ingenting (tyst). Ändras en Bitmaps pixlar medan den visas ritar
// `setImg(bild)` om (en ny version är en ändring) — eller `invalidate()`.
//
// DET NATURLIGA MÅTTET, som autostorleken och Fit läser, är bildens pixlar
// räknade som FYSISKA px (`Draw::imageSize`): logiskt = pixlar / skala. En
// oförändrad Bitmap blir alltså logiskt MINDRE på en skärm med högre skala —
// samma regel som för en asset — och den som vill ha en fast logisk storlek
// sätter `width`/`height`.
//
// NEDSKALNING: ritas bilden mindre än sitt naturliga mått (Fit, Fill
// eller Stretch i en ruta som är mindre än bilden) räknas den om till rutans
// storlek med ytans medelvärde (`Draw::glyphResampled`) i stället för att
// samplas av GPU:ns linjära filter, som hoppar över källpixlar och gör en
// stor bild i en liten ruta grynig. En omräknad textur per storlek och
// fönster; 1:1 och uppåt ritas oförändrat.
//
// Fit-lägena (WinForms SizeMode):
//   Fit     — hela bilden ryms, proportionerna bevaras (default)
//   Fill    — täcker hela ytan, proportionerna bevaras, överskottet KLIPPS
//   Stretch — dras ut till ytan, proportionerna offras
//   Center  — naturlig storlek, centrerad (klipps om den är för stor)

namespace glaze {

class Picture : public Widget {
public:
    enum class Fit { Fit, Fill, Stretch, Center };


    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setImg(ImageSource img)
    {
        if (img_ == img)
            return;
        img_ = std::move(img);
        invalidate();
    }
    const ImageSource& img() const { return img_; }

    void setFit(Fit f)
    {
        if (fit_ == f)
            return;
        fit_ = f;
        invalidate();
    }
    Fit fit() const { return fit_; }

    // setProp-egenskaper: img ("lista/nyckel"), fit (fit/fill/stretch/
    // center; alias "sizemode" — WinForms-namnet).
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

private:
    // Bildens målrect i LOGISKA px — EN sanning om geometrin: ritningen
    // och klippbeslutet läser samma uträkning.
    gfx::Rectf imageBounds(const Ctx& c) const;

private:
    ImageSource img_{};              // ogiltig = ingen bild
    Fit         fit_ = Fit::Fit;
};

} // namespace glaze
