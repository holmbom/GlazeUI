#pragma once
#include <gfx/sdf.h>   // gfx::TextAnchor — ritytans förankring (canvas/shape.h)

#include "glaze/widgets/panel.h"

// FloatingPanel — DEN FLYTTBARA PANELEN: en Panel med en RUBRIK
// (text och en valfri stängknapp) som ANVÄNDAREN flyttar genom att dra
// rubriken och storleksändrar i ett GREPP i nedre högra hörnet — ett
// minifönster INNE i ett fönster: en anteckning eller en inforuta på en
// karta, ett verktygsfönster över en arbetsyta. MÖNSTER: widgeten ritar SIG
// SJÄLV — draw()/contentSize() bor i src/widgets/floating_panel.cpp med
// switch (c.ui) och ritar via Draw:: (hård regel 3); mallen är `button.h`.
//
// RUBRIKEN ÄR DOCKNINGSPANELENS: samma rad (`dockHeaderH`), samma indrag,
// samma stängknapp med samma tända platta och samma ytor per UI — en panel
// med rubrik är samma sak i dockningen och fritt, och två sorters rubriker i
// samma fönster hade varit en för mycket. Ritningen är därför EN: samma kod
// som `DockPanel`s (src/dock/dock_header.h). Det som är eget är GREPPET, en
// remsa längst ned (`floatingPanelGrip`) med greppet i högra änden: barnen
// klipps ovanför den, så ett barn som fyller kroppen (`Dock::Fill`) aldrig
// tar greppet ifrån användaren.
//
// DRAGET flyttar panelen i FÖRÄLDERNS logiska px: `x`/`y` (och `right`/
// `bottom` för en kant som är bunden från slutet) ändras så att den lösta
// ytan följer pekaren, och greppet ändrar `width`/`height` med det övre
// vänstra hörnet stilla — aldrig mindre än `floatingPanelMinW`/`MinH`. En
// DOCKAD panel (`dock`) och en som föräldern placerar själv (en `Bar`, en
// delare — `placesChild`) flyttas och ändras inte. PÅ RITYTAN med en
// förankring (`Canvas::setChildAnchor`) flyttar draget ANKARET: världspunkten
// räknas om ur kameran, så panelen står kvar där användaren släppte den när
// kartan därefter panoreras. Draget börjar först efter stilens `dragThreshold`
// (dockningens `pastDragThreshold`) — ett klick på rubriken är inget drag.
//
// `onChange` fyras när ANVÄNDAREN släppt ett drag som flyttade eller
// ändrade panelen, och när stängknappen dolt den (läs `x`/`y`/`width`/
// `height`, `isVisible()` eller ritytans `childAnchor` ur sender). Ett
// AVBRUTET drag (fokusförlusten, `MouseCaptureLost` — `mouseCancel`s regel:
// avbryt, spara inte) återgår till läget vid trycket och fyrar ingenting.
// Programmatisk sättning fyrar den aldrig.
//
// STORLEKEN är appens som en Panels (axelregeln, panel.cpp): `width`/
// `height` — en auto-axel får panelens minsta mått.

namespace glaze {

class Canvas;

class FloatingPanel : public Panel {
public:
    // Barnen KLIPPS mot kroppen från start (`setClip(false)` släpper dem) —
    // annars når ett barn över rubriken och greppet.
    explicit FloatingPanel(TrText t = {}) : text_(std::move(t)) { setClip(true); }

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

    // Stängknappen i rubriken — av som förval (en inforuta stängs oftast av
    // appen, inte av användaren). Ett tryck DÖLJER panelen och fyrar
    // `onChange`.
    void setClosable(bool on)
    {
        if (closable_ == on)
            return;
        closable_ = on;
        invalidate();
    }
    bool isClosable() const { return closable_; }

    Handler<ChangeArgs> onChange;

    // setProp-egenskaper: text, closable; resten tar Panel.
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

    // Barnen bor i KROPPEN — mellan rubriken och greppets remsa — och klipps
    // där; rullningen räknas mot samma yta.
    gfx::Rectf childArea(const Ctx& c) const override;
    gfx::Rectf clipRect(const Ctx& c) const override;
    gfx::Rectf scrollViewport(const Ctx& c) const override;

    TrText toolTipAt(const Ctx& c, float x, float y) const override;
    void   mouseDown(const Ctx& c, float x, float y) override;
    void   mouseDrag(const Ctx& c, float x, float y) override;
    bool   mouseUp(const Ctx& c, float x, float y) override;
    void   mouseMove(const Ctx& c, float x, float y) override;
    bool   mouseCancel(gfx::MouseButton b) override;

private:
    // Delarna i widget-lokala LOGISKA px — ritningen, containerkrokarna och
    // musvägen läser samma.
    enum class Part : std::uint8_t { None, Header, Close, Grip };
    gfx::Rectf headerRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf closeRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf titleRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf gripRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf bodyBounds(const Ctx& c) const;   // kroppen ur `innerBounds`
    Part       partAt(const Ctx& c, float x, float y) const;
    bool       partHot(Part p) const;
    bool       partDown(Part p) const;

    // Draget: förskjutningen (dx, dy) i LOGISKA px från trycket, på det läge
    // panelen hade vid trycket — (0, 0) är tillbaka dit (avbrottet).
    void moveTo(float dx, float dy);
    void resizeTo(float dx, float dy, float minW, float minH);
    // Ritytan panelen är förankrad på, eller nullptr.
    Canvas* anchorCanvas() const;
    // Flyttar draget något (inte dockad, inte placerad av föräldern)?
    bool    movable() const;
    // Användarens ändring: `onChange` — true när en handler kördes.
    bool fireChange();

    TrText text_;
    bool   closable_    = false;
    Part   hoverPart_   = Part::None;
    Part   pressPart_   = Part::None;
    bool   pressInside_ = false;
    bool   drag_        = false;
    float  pressX_ = 0.0f, pressY_ = 0.0f;   // fysiska px, krokens Ctx
    // Läget vid trycket: layoutfälten (och syskonankarnas luft), den lösta
    // storleken i logiska px och — på ritytan — förankringens punkt i
    // ritytans lokala logiska px och dess hörn.
    float start_[6]    = {};   // x, y, right, bottom, width, height
    float startGap_[4] = {};   // anchorLeft/Top/Right/Bottom.gap
    float startW_ = 0.0f, startH_ = 0.0f;
    bool            anchored_ = false;
    double          startWorldX_ = 0.0, startWorldY_ = 0.0;
    double          startLocalX_ = 0.0, startLocalY_ = 0.0;
    gfx::TextAnchor startAnchor_ = gfx::TextAnchor::Center;
};

} // namespace glaze
