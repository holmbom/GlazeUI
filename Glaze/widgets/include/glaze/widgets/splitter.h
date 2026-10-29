#pragma once
#include "glaze/widget.h"

// Splitter — DELAREN (wxSplitterWindow, WinForms
// SplitContainer). Två RUTOR och ett SKIKT mellan dem som användaren drar i.
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/splitter.cpp med switch (c.ui) (hård regel 3); skiktets form
// är DELAD med dockningsytan och bor i den interna `src/sash.cpp`.
//
// RUTORNA ÄR DE TVÅ FÖRSTA BARNEN i adopt-ordning — oftast två `Panel` —
// och delaren PLACERAR dem själv (`placesChildren`/`placeChild`,
// Bar-prejudikatet): deras egna dock/anchors/x/y ignoreras. Skälet är regel
// 8: ett kedjat `split.first().adopt(x)` går inte att emittera i
// grammatiken, medan `split.adopt(vänster); split.adopt(höger);` är
// trädbyggets egen sats. Barn efter de två placeras inte (tom rekt) — lägg
// dem i en ruta. LAYOUTEN ÄR ALLTID TVÅ RUTOR OCH ETT SKIKT, oavsett hur
// många barn som finns och om de syns: en ruta som saknas eller är dold
// lämnar sitt fält tomt (ramverkets allmänna regel — en dold widget lämnar
// ett hål, se syskonankaret), och en nyplacerad delare utan barn visar
// ändå sitt skikt. Att FÄLLA IHOP en ruta (WinForms `Panel1Collapsed`) är
// INTE byggt — det är ett eget tillägg med egna egenskaper, inte en
// bieffekt av rutans synlighet.
//
// LÄGET ÄR TILLSTÅNDSLÖST: `paneSize` är den FASTA rutans storlek i
// logiska px, och den andra tar resten. När delaren växer behåller den
// fasta rutan sin storlek — WinForms `FixedPanel`, utan `None`-läget: ett
// andelsläge hade krävt att delaren mindes sin förra storlek.
//
// NAMNET ÄR `paneSize` OCH INTE `SplitterDistance`, och det är ett beslut.
// WinForms `SplitterDistance` mäter ALLTID från vänster/topp, alltså från
// första rutan — och med `FixedPanel = Panel2` skriver WinForms därför om
// talet vid varje storleksändring för att den andra rutan ska stå still.
// Det är TILLSTÅND i layouten, precis det vår layout inte har (den räknas
// fram per anrop ur fälten, som dock-carvingen). Här mäter talet den rutan
// som STÅR STILL, oavsett vilken det är, och då är det sant utan att någon
// skriver om det. Att låna WinForms namn med en annan betydelse hade varit
// en fälla för just den som kan WinForms.
//
// AUTO: `paneSize <= 0` (default) delar ytan lika — halva, formens
// definition och inget tal. Draget skriver ett riktigt mått.
//
// MINIMUM per ruta (`firstMin`/`secondMin`, logiska px, 0 = inget — WinForms
// `Panel1MinSize`/`Panel2MinSize`): rutan dras aldrig mindre, och ryms inte
// båda vinner den FASTA rutans (`sash::clampPane`).
//
// INTE FOKUSERBAR med flit: skiktet är en del av ytan, och dess enda
// handling är musens. (WinForms SplitContainer är ett tabbstopp med
// piltangenter; det kan läggas till som ett eget beslut.) `onChange` fyras
// när användaren SLÄPPT ett drag som ändrade läget — en gång per drag,
// WinForms `SplitterMoved` — aldrig av settrarna.
//
// DRAGET ÄR EN FÖRHANDSVISNING tills släppet: rutorna följer handen, men
// `paneSize()` svarar med det gamla läget ända tills knappen släpps, och
// först då skrivs det nya. Ett drag som AVBRYTS — fönstret tappar fokus,
// så släppet aldrig kommer — går därför tillbaka av sig självt, utan att
// appen fått ett läge den aldrig fick veta om. Samma sak gäller ett drag
// där delaren inaktiveras innan släppet.
//
// INAKTIVERAD delare: skiktet går inte att dra och tänds inte, men ser i
// övrigt likadant ut (se `sash.h` — en yta har inget läge). `enabled`
// kaskadar inte till rutorna, samma regel som Panel.

namespace glaze {

class Splitter : public Widget {
public:
    // Den FASTA rutan — den som behåller sin storlek när delaren ändrar
    // storlek. Första = vänster/övre.
    enum class Pane { First, Second };

    Splitter() = default;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar aldrig onChange.

    // Stående skikt (rutorna BREDVID varandra, ↔) eller liggande (rutorna
    // över varandra, ↕). Default stående — WinForms `Orientation.Vertical`
    // och samma betydelse som `Separator::vertical`: det är LINJEN som är
    // stående.
    void setVertical(bool on)
    {
        if (vertical_ == on)
            return;
        vertical_ = on;
        invalidate();
    }
    bool isVertical() const { return vertical_; }

    void setFixedPane(Pane p)
    {
        if (fixedPane_ == p)
            return;
        fixedPane_ = p;
        invalidate();
    }
    Pane fixedPane() const { return fixedPane_; }

    // Den FASTA rutans storlek i logiska px; <= 0 = AUTO (halva). Läsaren
    // svarar med det LAGRADE talet — efter ett släppt drag är det det
    // dragna, klampat mot minimimåtten; UNDER draget det gamla (headern).
    void setPaneSize(float logical)
    {
        if (paneSize_ == logical)
            return;
        paneSize_ = logical;
        invalidate();
    }
    float paneSize() const { return paneSize_; }

    void setFirstMin(float logical)
    {
        if (firstMin_ == logical)
            return;
        firstMin_ = logical;
        invalidate();
    }
    float firstMin() const { return firstMin_; }

    void setSecondMin(float logical)
    {
        if (secondMin_ == logical)
            return;
        secondMin_ = logical;
        invalidate();
    }
    float secondMin() const { return secondMin_; }

    // Användaren släppte ett drag som flyttade skiktet. Läs läget ur sender
    // (`paneSize()`), regel 6.
    Handler<ChangeArgs> onChange;

    // setProp-egenskaper: vertical (true/false), fixedPane (first/second),
    // paneSize/firstMin/secondMin (C++-literaler — `220`, `220.5f`).
    bool setProp(const std::string& key, const std::string& value) override;

    // Delaren placerar sina rutor själv (Widget-krokarna, Bar-formen).
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    // Rutorna klipps mot delaren — en ruta som vill vara större än sin
    // andel får aldrig rita över grannen.
    bool clipsChildren() const override { return true; }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float x, float y) override;
    void mouseDrag(const Ctx& c, float x, float y) override;
    bool mouseUp(const Ctx& c, float x, float y) override;
    void mouseMove(const Ctx& c, float x, float y) override;
    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;

private:
    // GEOMETRIN — EN sanning i widget-LOKALA LOGISKA px, och den tar ytan
    // `b` som argument (gruppramens form): ritningen skickar `c.bounds()`,
    // placeringen och musvägen `Draw::bounds(c, resolve(c))`.
    struct Layout {
        gfx::Rectf first{}, sash{}, second{};
    };
    Layout layout(const Ctx& c, const gfx::Rectf& b) const;
    // Rutan med index 0/1 (de två första barnen), eller nullptr.
    const Widget* pane(std::size_t i) const;
    // Skiktet i FYSISKA px, för musvägen (Ctx:t är ctxFor-Ctx:t).
    gfx::Rectf sashRect(const Ctx& c) const;
    bool hot() const;
    // Pågår ett drag? `dragging_` TILLSAMMANS med `pressed()` — se nedan.
    bool liveDrag() const { return dragging_ && pressed(); }

private:
    bool  vertical_  = true;
    Pane  fixedPane_ = Pane::First;
    float paneSize_  = 0.0f;   // <= 0 = AUTO (halva)
    float firstMin_  = 0.0f;
    float secondMin_ = 0.0f;

    // Draget. `dragging_` läses ALLTID tillsammans med `pressed()` (kontraktet
    // vid `Widget::mouseUp`): ett tryck som avbryts av fokusförlust får
    // inget släpp, och då är det `pressed()` som slocknar — och med det
    // förhandsvisningen, så layouten faller tillbaka på `paneSize_`.
    bool  dragging_ = false;
    bool  overSash_ = false;    // pekaren över skiktet (mouseMove)
    float grip_     = 0.0f;     // var i skiktet trycket tog (fysiska px)
    float dragSize_ = 0.0f;     // förhandsvisningens fasta ruta (logiska px)
    float startEff_ = 0.0f;     // den EFFEKTIVA storleken när draget började
};

} // namespace glaze
