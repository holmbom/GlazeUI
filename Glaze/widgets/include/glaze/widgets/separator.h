#pragma once
#include "glaze/widget.h"

// Separator — skiljelinjen (WinForms/wx: wxStaticLine). Den enklaste
// widgeten i huset: ETT streck, ingen interaktion, inget tabbstopp.
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/separator.cpp med switch (c.ui) och ritar via Draw::
// (hård regel 3); tjockleken och luften kommer ur stilen, färgen ur temat.
//
// LÄNGDEN ägs av layouten: contentSize svarar bara på den TUNNA axeln
// (tjocklek + luft), så en vågrät separator vill normalt ha
// dock = Dock::Top eller anchors {left, right} — precis som en dockad
// remsa i WinForms.
//
// I EN `Bar` STÅR SEPARATORN TVÄRS BAREN, vilket värde
// `vertical` än har: lodrät i en liggande bar och vågrät i en stående. Ett
// verktygsfält som dras från överkanten till vänsterkanten i en dockningsyta
// hade annars fått liggande skiljelinjer på högkant. Där svarar den TUNT på
// båda axlarna (baren ger den sin fulla tvärsida), så samma mått gäller i
// båda riktningarna. Utanför en bar gäller `vertical` som den står.

namespace glaze {

class Separator : public Widget {
public:

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setVertical(bool on)
    {
        if (vertical_ == on)
            return;
        vertical_ = on;
        invalidate();
    }
    bool isVertical() const { return vertical_; }

    // setProp-egenskaper: vertical (yes/no), orientation
    // (horizontal/vertical — samma form som Scrollbar).
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

private:
    // Riktningen som RITAS: tvärs baren i en `Bar`, annars `vertical_`.
    bool drawnVertical() const;
    // Står separatorn i en `Bar`? Då är den tvärs baren och tunn längs den.
    bool inBar() const;

    bool vertical_ = false;   // false = vågrätt streck (vanligast)
};

} // namespace glaze
