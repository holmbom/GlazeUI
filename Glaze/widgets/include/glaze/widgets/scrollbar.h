#pragma once
#include "glaze/draw.h"
#include "glaze/widget.h"

// Scrollbar — fristående rullningslist (WinForms HScrollBar/VScrollBar).
// MÖNSTER: widgeten ritar SIG SJÄLV (draw()/contentSize() i
// src/widgets/scrollbar.cpp), men tumgeometrin är DELAD: Draw::scrollThumb/
// scrollThumbDraw (glaze/draw.h) ritar exakt samma pixlar som Panels
// inbyggda overlay-lister — två renderare av samma sak MÅSTE ha en
// sanning. BETEENDET (tumdrag, rännklick sidvis, hjul) bor HÄR.
//
// Värdena är i VALFRIA enheter (rader, px, procent — appens val):
// value ∈ [min, max], page = synlig andel (styr tumlängden). Programmatisk
// ändring avfyrar inget, och värdet är KLAMPAT VID LÄSNING (`Slider`s regel): `setValue` lagrar talet som det är och
// `value()` svarar med `clamp(lagrat, min, max(min, max))`, så satsordningen
// mot `setMin`/`setMax` aldrig spelar roll. onChange fyras vid
// varje ANVÄNDAR-ändring — LIVE under ett tumdrag; läs värdet ur sender
// (regel 6): sb->value(). onCommit säger att användaren är KLAR: när
// musknappen SLÄPPS och värdet ändrats efter förra commiten (ett tumdrag, ett
// rännklick), och vid varje hjulsteg (då efter onChange). `Slider`s regel;
// listen är inte fokuserbar, så baslinjen är förra commiten och `setValue`.

namespace glaze {

class Scrollbar : public Widget {
public:
    enum class Orientation { Horizontal, Vertical };


    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setOrientation(Orientation o)
    {
        if (orientation_ == o)
            return;
        orientation_ = o;
        invalidate();
    }
    // Gränserna — en gräns som flyttar det EFFEKTIVA värdet flyttar också
    // commit-baslinjen (`Slider`s regel).
    void setMin(float v);
    void setMax(float v);
    // Programmatisk sättning avfyrar ALDRIG onChange eller onCommit — talet
    // LAGRAS som det är, och ett värde som BYTS flyttar commit-baslinjen.
    void setValue(float v);
    void setPage(float v)  { if (page_ != v)  { page_ = v;  invalidate(); } }

    Orientation orientation() const { return orientation_; }
    float min() const   { return min_; }
    float max() const   { return max_; }
    // Det EFFEKTIVA värdet — klampat till [min, max(min, max)] (se ovan).
    float value() const;
    float page() const  { return page_; }

    Handler<ChangeArgs> onChange;   // LIVE — varje läge
    Handler<ChangeArgs> onCommit;   // KLAR — släppet, ett hjulsteg

    // setProp-egenskaper: orientation (horizontal/vertical), min, max,
    // value, page.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;
    bool mouseUp(const Ctx& c, float mx, float my) override;   // commit
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;

private:
    // Widgetens tillstånd som råa rullningsmått — ritningen OCH beteendet
    // (träff, dragmappning) läser samma värden.
    Draw::ScrollMetrics metrics() const;

    // Rännan, tummen och musens läge längs axeln — allt i widget-lokala
    // LOGISKA px, så ritningen och greppet räknar i samma rymd.
    gfx::Rectf track(const Ctx& c) const;
    gfx::Rectf thumb(const Ctx& c) const;
    float      localPos(const Ctx& c, float mx, float my) const;

    // klampa + onChange + invalidate; false = en handler rev listen
    bool  userSet(float v);
    // onCommit om värdet ändrats efter baslinjen — true = handlern kördes.
    bool  commitFromUser();

    Orientation orientation_ = Orientation::Vertical;
    float min_   = 0.0f;
    float max_   = 100.0f;
    float value_ = 0.0f;                // RÅTT — klampas vid läsning
    float page_  = 10.0f;
    bool  dragging_ = false;            // nollas vid nästa mouseDown
    float dragOff_  = 0.0f;             // greppunkt inne i tummen (logiska px)
    float commitBase_ = 0.0f;           // det EFFEKTIVA vid förra commiten
};

} // namespace glaze
