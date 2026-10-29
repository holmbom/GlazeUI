#pragma once
#include <utility>

#include "glaze/widgets/panel.h"

// Bar — flex-RAD-motorn bakom TopBar/StatusBar: en
// horisontell container där barnen packas vänster→höger i ADD-ORDNING;
// varje barn tar sin mätta bredd och barens fulla höjd (widgetarna
// v-centrerar sitt innehåll i recten). Spacer-barn sväljer det lediga
// utrymmet. Layouten är GEOMETRI och bor HÄR (placeChild via Widget-
// krokarna), aldrig i ritkoden — och den är skild från Dock/Anchor:
// Dock placerar BAREN i fönstret, flexen placerar barens BARN (deras
// egna dock/anchors ignoreras).
//
// STÅENDE (WinForms `ToolStrip.Dock = Left`): en bar med
// `dock` Left eller Right packar barnen UPPIFRÅN och ned, och varje barn får
// barens fulla BREDD och sin mätta höjd. Riktningen följer `dock` och är
// ingen egen egenskap — ett verktygsfält som dras till vänsterkanten i en
// dockningsyta står därmed av sig självt. TopBar, StatusBar och MenuBar står
// Top/Bottom; den liggande grenen är orörd. Riktningsfrågan `isVertical()` är
// VIRTUAL: ett FLYTANDE verktygsfält ligger alltid, och
// dess `dock` är bara minnet för var det dockas tillbaka — flexen,
// mätningen, hårlinjen och separatorns riktning frågar hit och följer med.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() i src/bar.cpp
// (temats chrome-färg, switch (c.ui)).

namespace glaze {

class Spacer;

class Bar : public Panel {
public:
    Bar() = default;   // height 0 = stilens barHeight (32, lika på Windows och macOS)

    // Bekvämlighet: bar.addSpacer() = bar.add<Spacer>() (ägd, grow-vikt 1).
    Spacer* addSpacer(float grow = 1.0f);

    // REN RAD — enbart flexen: INGEN fyllning och INGEN hårlinje mot
    // innehållet, och barnen behåller sin MÄTTA mått på tväraxeln (höjden i en
    // liggande rad, bredden i en stående) centrerade i baren i stället för att
    // fylla den. En knapprad i en dialog ska inte se ut som ett verktygsfält.
    // Ett barn utan eget mått på tväraxeln (en Spacer, en tom Panel) fyller
    // baren som i en vanlig rad. Baren är lika hög som en vanlig bar
    // (`height`, annars stilens `barHeight`) — en rad med högre barn behöver
    // en satt höjd. Av som
    // förval; gäller en Bar, och TopBar/StatusBar/ToolBar är chrome som inte
    // är gjort för det.
    void setPlain(bool on)
    {
        if (plain_ == on)
            return;
        plain_ = on;
        invalidate();
    }
    bool isPlain() const { return plain_; }

    // setProp-egenskaper: height (logiska px), plain (true/false); resten
    // tar Panel/basen.
    bool setProp(const std::string& key, const std::string& value) override;

    // STÅR baren? `dock` Left eller Right — en subklass får svara
    // annorlunda (`ToolBar`: aldrig när den flyter).
    virtual bool isVertical() const
    {
        return dock == Dock::Left || dock == Dock::Right;
    }

    // Flex-kontraktet (Widget-krokarna): baren placerar sina barn själv.
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Radien på barens ÖVRE hörn i LOGISKA px. En vanlig bar har inga (0);
    // TopBar överrider och läser FÖNSTRETS radie, så namnlisten följer
    // hörnet i stället för att sticka ut ur det.
    virtual float topRadius(const Ctx& c) const;

    // LUCKAN MELLAN BARNEN i LOGISKA px. En vanlig
    // bar packar barnen kant i kant (0) — namnlistens och statusradens
    // barn är redan sin egen luft. `ToolBar` överrider med stilens
    // `toolGap`: verktygsknappar vars hover-plattor nuddar varandra läser
    // som EN knapp.
    //
    // Den bor HÄR och inte som en egen flex i ToolBar: flexen ska finnas
    // på ETT ställe, och med luckan 0 packas barnen kant i kant.
    // Luckan ligger bara MELLAN två SYNLIGA barn — ett dolt
    // barn tar ingen plats, och därmed ingen lucka heller.
    virtual float childGap(const Ctx& c) const;

    // DELTAR BARNET I FLEXEN? — förvalet är att varje SYNLIGT barn
    // gör det. Båda passen i båda riktningarna frågar här, så ett barn utanför
    // flödet varken tar plats eller lucka, precis som ett dolt, och får en
    // rekt utan längd. Ett verktygsfält håller sin egen överflödsknapp utanför
    // och placerar den själv (tool_bar.h).
    virtual bool inFlow(const Widget& child) const { return child.isVisible(); }

    // HÅRLINJENS LUCKOR i LOGISKA px: hur långt in från
    // vänster respektive höger ände linjen mot innehållet börjar. En vanlig
    // bar drar den hela vägen (0, 0); fönstrets egen TopBar hoppar över den
    // STORA programikonen, som täcker namnlist + menyrad och annars hade fått
    // en linje tvärs genom bilden.
    struct EdgeInsets {
        float left  = 0.0f;
        float right = 0.0f;
    };
    virtual EdgeInsets innerEdgeInsets(const Ctx& c) const;

    // RITAS HÅRLINJEN mot innehållet över huvud taget? En
    // vanlig bar gränsar mot innehållet (true). Ett FLYTANDE verktygsfält står
    // ensamt under sin rubrik i ett eget fönster och vetter inte mot något —
    // och med `dock` som minne hade linjen hamnat på fel sida.
    virtual bool drawsInnerEdge() const { return true; }

private:
    // Den STÅENDE flexen — `placeChild` för `dock` Left/Right.
    gfx::Rectf placeChildVertical(const Widget& child, const Ctx& c) const;

    // Barnets spann (början, längd) på TVÄRAXELN i FYSISKA px — y/höjd i den
    // liggande raden (`row`), x/bredd i den stående: barens hela, eller i en
    // REN rad barnets eget mått centrerat i den (och hela baren för ett barn
    // utan eget mått). Mäter barnet bara i en ren rad.
    std::pair<float, float> crossSpan(const Widget& child, const Ctx& c,
                                      bool row) const;

    // Barens YTA — med de övre hörnen rundade när `topRadius()` säger det.
    // Delad av alla tre ritgrenarna: en kopia per familj hade varit tre
    // chanser att missa avrundningen i en av dem.
    void fillBody(const Ctx& c, gfx::Color fill) const;

    // Hårlinjen mot INNEHÅLLET — nedåt för en topplist, uppåt för en
    // statusrad, åt höger för en vänsterlist och åt vänster för en högerlist
    // (`dock` avgör). Alla tre familjerna ritar den.
    void innerEdge(const Ctx& c, gfx::Color col) const;

    bool plain_ = false;   // REN RAD
};

} // namespace glaze
