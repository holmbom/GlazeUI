#pragma once
#include "glaze/widget.h"

// Spacer — grow-vikt-barnet i en Bar: sväljer PASSIVT det lediga utrymmet
// (trycker/maximerar inget, tar bara luckan); flera spacers delar luckan
// efter vikt — två lika spacers centrerar det som ligger mellan dem (som
// VS Codes titelrad). Ingen ritning: luckan är barens bakgrund. width > 0
// med grow = 0 ger en FAST lucka (marginal/mellanrum).
//
// MÖNSTER: inbyggd widget utan utseende — draw() är tom, ingen ritkod.

namespace glaze {

class Spacer : public Widget {
public:
    explicit Spacer(float g = 1.0f) : grow_(g) {}

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // Andel av barens lediga utrymme (delas med övriga växande barn efter
    // vikt); 0 = bara den fasta bredden (width, logiska px).
    void setGrow(float g)
    {
        if (grow_ == g)
            return;
        grow_ = g;
        invalidate();
    }
    float grow() const { return grow_; }

    float flexGrow() const override { return grow_; }

    // setProp-egenskaper: grow; resten (width m.fl.) tar basen.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    void draw(const Ctx&) override {}   // ingen ritning — luckan är pixlarna

private:
    float grow_ = 1.0f;
};

} // namespace glaze
