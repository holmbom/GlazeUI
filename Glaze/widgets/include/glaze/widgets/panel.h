#pragma once
#include "glaze/draw.h"

// Panel — container-PRIMITIVET (VISION: "Två sorters förälder"): en
// rektangel i en förälder som kan ha barn, klippa och scrolla. Toolbar/
// Statusbar/tab-sidor byggs som paneler. MÖNSTER: inbyggd widget —
// draw()/contentSize() bor i src/panel.cpp med switch (c.ui), och
// innehållsytan innerRect() är panelens EGEN geometri. En EGEN
// container utanför ramverket ärver Widget, överrider draw() själv och
// childArea/clipsChildren/clipRect för barnens koordinater/klippning.
//
// Barn läggs med panel->add<T>(...) eller adopt (genererad kod:
// panel.adopt(barn)) och placeras FÖRÄLDER-RELATIVT (x/y/dock/anchors
// räknas mot panelens innehållsyta).

namespace glaze {

class Panel : public Widget {
public:
    Panel() = default;


    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // Klipp barnen mot innehållsytan (och hit-testen). LÄSAREN är den
    // befintliga virtuella `clipsChildren()` — att lägga till en
    // `isClip()` bredvid hade gett två sanningar om samma bool.
    void setClip(bool on)
    {
        if (clip_ == on)
            return;
        clip_ = on;
        invalidate();
    }

    // Rita panelyta + ram (annars en osynlig placeringsyta).
    void setBorder(bool on)
    {
        if (border_ == on)
            return;
        border_ = on;
        invalidate();
    }
    bool isBorder() const { return border_; }

    // setProp-egenskaper: border + clip (builderns preview sätter fälten
    // via nyckel/värde-text); resten tar basen.
    bool setProp(const std::string& key, const std::string& value) override;

    // Synliga overlay-rullningslister (opt-in): ritas OVANPÅ barnen i
    // klippytans kanter när innehållet svämmar över — indikator + dragbar
    // tumme + rännklick på samma scrollX_/scrollY_ som hjulet. Overlay ⇒
    // ingen layoutförändring (childArea/klipp orörda); pixlarna delas med
    // Scrollbar-widgeten (Draw::ScrollMetrics-vägen). OBS: barn
    // som ligger under remsan vinner hit-testen — layouta innehållet så
    // att interaktiva barn inte täcker kanten.
    void showScrollbars(bool on)
    {
        if (scrollbars_ != on) {
            scrollbars_ = on;
            invalidate();
        }
    }
    bool scrollbarsShown() const { return scrollbars_; }

    // Scroll-offset i LOGISKA px (som alla widget-mått). Klampas mot
    // innehållets utsträckning vid användning. Panelens yta invalideras —
    // EN rect, aldrig per barn (avgjord fråga 11).
    void  setScroll(float x, float y);
    float scrollX() const { return scrollX_; }
    float scrollY() const { return scrollY_; }

    // Innehållsytan. innerBounds = SANNINGEN i widget-lokala LOGISKA px
    // (recten minus ramtjockleken när border är på) — det ritkoden och
    // remsgeometrin räknar i; innerRect = dess FYSISKA projektion, som
    // klipp, childArea, hit-test och subklasserna (PropertyGrids rader)
    // utgår från. Aldrig två uppsättningar mått. Tvärs-widget-geometri som
    // denna löses som en metod på ÄGAREN, inte som en Draw-primitiv (det
    // är inte ritning).
    gfx::Rectf innerBounds(const Ctx& c) const;
    gfx::Rectf innerRect(const Ctx& c) const;
    // Innehållsytans hörnradie i LOGISKA px — koncentrisk mot den ritade
    // ytan (`Style::panelRadius` minus ramens indrag). Parhäst till
    // innerBounds på exakt samma sätt som `clipRadius` är det till
    // `clipRect`, och EN sanning: subklasserna (PropertyGrid) klipper sina
    // egna rader mot samma form.
    float      innerRadius(const Ctx& c) const;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Trädets koordinatmodell: barnen resolvar mot innehållsytan minus
    // scroll; klippning mot innerRect().
    gfx::Rectf childArea(const Ctx& c) const override;
    bool       clipsChildren() const override { return clip_; }
    gfx::Rectf clipRect(const Ctx& c) const override;
    // Klippets radie, KONCENTRISK mot den ritade ytan — se panel.cpp.
    float      clipRadius(const Ctx& c) const override;

    // Hjul-routing: scrolla om innehållet är större än ytan, annars
    // bubbla vidare (false).
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;

    // Autoskrollen under ett drag: inom
    // `Style::dragScrollZone` från rullningsytans kant, och bara åt ett håll
    // med spelrum kvar — annars false, och kedjan går vidare uppåt. Varje
    // subklass (TreeView, tabellerna, ListBox, PropertyGrid) ärver den.
    bool dragScroll(const Ctx& c, float x, float y, bool apply) override;

    // Overlay-listerna: ritning ovanpå barnen + remse-interaktion
    // (tumdrag/rännklick/hover). Aktiva bara med showScrollbars(true)
    // och faktiskt överflöd.
    void drawAfterChildren(const Ctx& c) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;

    // Innehållets omslutande utsträckning (fysiska px, relativt OSCROLLAD
    // innehållsyta) — grunden för scrollklampningen och listernas mått.
    // VIRTUELL: subklasser vars innehåll är DATA (PropertyGrids rader)
    // överrider och maxar mot basens barn-mått.
    virtual gfx::Sizef contentExtent(const Ctx& c) const;

    // Ligger punkten (fysiska px) i en synlig list-remsa? Subklasser med
    // egen mouseDown-logik frågar FÖRST (remsorna ligger överst) och
    // lämnar då trycket till Panel::mouseDown.
    bool inScrollStrip(const Ctx& c, float mx, float my) const;

    // Står punkten (fysiska px) inom innehållsytans BREDD och utanför
    // remsorna? Radlistornas träffväg (ListBox, TreeView):
    // remsan och sidramen är "ingen rad" för både snabbmenyns
    // träff och högertrycket. Höjdled vaktar radträffen själv.
    bool inRowColumn(const Ctx& c, float mx, float my) const;

    // Scrollens spelrum per axel i LOGISKA px (0 = axeln svämmar inte
    // över) — scroll, hjul, remsor och klampning delar uträkningen.
    gfx::Sizef scrollRange(const Ctx& c) const;

    // RULLNINGSYTAN — den del av innehållsytan som SCROLLAS, i widget-lokala
    // LOGISKA px. Default `innerBounds(c)`: hela innehållsytan rullar, och då
    // är allt nedan oförändrat mot en vanlig Panel.
    //
    // En subklass med en FAST REMSA överst — tabellens kolumnhuvud
    // (`Table`) — svarar med ytan UNDER remsan. Spelrummet räknas
    // då mot den (`contentExtent` − rullningsytan), overlay-remsornas
    // ränna börjar under huvudet och tummens sida är det man faktiskt ser
    // av raderna. Utan kroken hade den lodräta remsan ritats över huvudet
    // och sidmåttet varit huvudets höjd för stort. `childArea`/`clipRect`
    // rörs INTE av den — barnens koordinater och klipp är subklassens
    // egna beslut (`Table` klipper sina editorer mot samma yta).
    virtual gfx::Rectf scrollViewport(const Ctx& c) const;

private:
    // Remsans rect i widget-lokala LOGISKA px (tom när axeln inte svämmar
    // över) + axelns rullningsmått: ritningen OCH greppet läser samma två.
    gfx::Rectf          track(const Ctx& c, bool vertical) const;
    Draw::ScrollMetrics scrollMetrics(const Ctx& c, bool vertical) const;
    gfx::Pointf         localPoint(const Ctx& c, float mx, float my) const;

    // Overlay-tummens RITNING per UI (Glaze lamineras, Minimalistic går i
    // textfärgerna, Slate i `border`/`textDim`). Egen metod så remsloopens
    // geometri och grepp bara finns EN gång — och färgerna är ett KONTRAKT
    // med scrollbar.cpp: samma färg för samma tillstånd, bytta i samma
    // ändring.
    void overlayThumb(const Ctx& c, const gfx::Rectf& tr,
                      const Draw::ScrollMetrics& sm, bool down,
                      bool hot) const;

    bool  clip_   = true;
    bool  border_ = false;
    float scrollX_ = 0.0f;
    float scrollY_ = 0.0f;
    bool  scrollbars_ = false;
    int   dragAxis_   = 0;      // 0 = inget, 1 = V, 2 = H (nollas per tryck)
    float dragOff_    = 0.0f;   // greppunkt i tummen (logiska px)
    int   hotAxis_    = 0;      // hovrad remsa — bara ritkosmetik
};

} // namespace glaze
