#pragma once
#include "glaze/widget.h"

// Slider — dragreglaget (wxSlider, WinForms TrackBar). MÖNSTER: widgeten
// ritar SIG SJÄLV — draw()/contentSize() bor i src/widgets/slider.cpp med
// switch (c.ui) och ritar via Draw:: (hård regel 3).
//
// Värdena är i VALFRIA enheter (som Scrollbar): value ∈ [min, max],
// kvantiserat mot `step` (0 = steglöst). Programmatisk ändring avfyrar
// inget; ANVÄNDAR-ändring klampar, kvantiserar och fyrar onChange. Läs
// värdet ur sender (regel 6).
//
// KLAMPAT VID LÄSNING (husets regel: SpinBox,
// DatePicker, TabControl): `setValue` lagrar talet som det är och `value()`
// svarar med det EFFEKTIVA, `clamp(lagrat, min, max(min, max))`. Ett värde
// utanför intervallet är alltså säkert, och det följer en senare
// `setMin`/`setMax` utan att satsordningen spelar roll — `.glazeui` kör
// satserna i källordning, så `setValue(150); setMax(200);` ger 150 i båda
// vägarna. Kvantiseringen mot `step` är användarens väg och ingen läsregel.
//
// Geometrin (ränna + tumme) definieras EN gång i LOGISKA px och
// projiceras med Draw::localX/localY när musen ska bli ett värde — aldrig
// två uppsättningar mått.
//
// HJULET KRÄVER FOKUS: med fokus flyttar ett
// hjulsteg värdet lika mycket som en piltangent (`effectiveStep`), uppåt
// för hjul upp. UTAN fokus svarar `mouseWheel` false, och hjulet BUBBLAR
// till närmaste rullande förälder — en sida som rullas förbi reglaget
// ändrar aldrig dess värde av misstag. Samma regel som `SpinBox`, den
// andra värdewidgeten: ett beslut för båda.
//
// TVÅ EVENTS: `onChange` är LIVE — varje läge under ett drag — och
// `onCommit` säger att användaren är KLAR: när musknappen SLÄPPS och värdet
// ändrats efter trycket, och vid varje tangent- och hjulsteg (då efter
// `onChange`, för samma steg). Baslinjen är värdet vid förra commiten; den
// sätts också när fokus kommer och av `setValue` när värdet byts. Ett tryck
// som avbryts utan släpp (fönstret tappar fokus) committar inte — nästa
// commit tar med ändringen.

namespace glaze {

class Slider : public Widget {
public:

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // Gränserna. En gräns som flyttar det EFFEKTIVA värdet flyttar också
    // commit-baslinjen (appens värde är inte användarens — nästa släpp ska
    // inte committa en ändring användaren aldrig gjorde).
    void setMin(float v);
    void setMax(float v);
    // Programmatisk sättning avfyrar ALDRIG onChange eller onCommit, och
    // talet LAGRAS som det är (klampat vid läsning, se ovan). Ett värde som
    // BYTS flyttar commit-baslinjen (appens värde är inte användarens).
    void setValue(float v);
    void setStep(float v)  { if (step_ != v)  { step_ = v;  invalidate(); } }
    void setVertical(bool on)
    {
        if (vertical_ == on)
            return;
        vertical_ = on;
        invalidate();
    }

    float min() const   { return min_; }
    float max() const   { return max_; }
    // Det EFFEKTIVA värdet — klampat till [min, max(min, max)] (se ovan).
    float value() const;
    float step() const  { return step_; }
    bool  isVertical() const { return vertical_; }

    Handler<ChangeArgs> onChange;   // LIVE — varje läge
    Handler<ChangeArgs> onCommit;   // KLAR — släppet, ett steg (se ovan)

    bool focusable() const override { return true; }

    // setProp-egenskaper: min, max, value, step, vertical (yes/no),
    // orientation (horizontal/vertical).
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;
    bool mouseUp(const Ctx& c, float mx, float my) override;   // commit
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void focusGained() override { commitBase_ = value(); }

private:
    // Rännan och tummen i widget-lokala LOGISKA px — ritningen OCH
    // musmappningen räknar i samma rymd.
    gfx::Rectf trackBounds(const Ctx& c) const;
    gfx::Rectf thumbBounds(const Ctx& c) const;

    // Tummens kropp per tillstånd (vila/hover/tryck) — samma trappa som
    // Buttons fillColor, för tummen är en PLATTA och bär den grammatiken.
    gfx::Color thumbFill(const Ctx& c) const;

    float fraction() const;                 // value → 0..1 längs rännan
    void  setValueFromLocal(const Ctx& c, float mx, float my);
    // klampa + kvantisera + onChange; false = en handler rev reglaget
    bool  userSet(float v);
    // onCommit om värdet ändrats efter baslinjen — true = handlern kördes.
    bool  commitFromUser();
    void  stepFromUser(float v);            // ett steg: userSet + commit

    // Steget tangenter och hjul faktiskt flyttar: `step` när den satts,
    // annars en hundradel av intervallet. EN sanning — hjulet och
    // tangentbordet MÅSTE flytta lika mycket, så uttrycket står på ETT ställe.
    float effectiveStep() const;

private:
    float min_   = 0.0f;
    float max_   = 100.0f;
    float value_ = 0.0f;      // RÅTT — klampas vid läsning (`value()`)
    float step_  = 1.0f;      // 0 = steglöst
    bool  vertical_ = false;  // vertikalt: min NEDERST (OS-konventionen)
    float commitBase_ = 0.0f; // det EFFEKTIVA värdet vid förra commiten
};

} // namespace glaze
