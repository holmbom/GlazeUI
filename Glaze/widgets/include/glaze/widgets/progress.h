#pragma once
#include "glaze/widget.h"

// Progress — förloppsindikatorn (wxGauge, WinForms ProgressBar). MÖNSTER:
// widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/progress.cpp med switch (c.ui) och ritar via Draw:: (hård
// regel 3).
//
// TVÅ lägen:
//   bestämt    — value ∈ [min, max] fyller rännan. INGEN animation alls.
//   obestämt   — en bit glider fram och tillbaka.
//
// Animationen drivs av widgeten SJÄLV och rör ALDRIG fönstrets
// setAnimating: draw() invalidaterar sig själv medan läget är obestämt,
// så nästa frame ritar om exakt den här widgetens rect. Tre vinster mot
// fönsterflaggan: appens egen animering kan inte slås av av misstag (en
// bool utan ägarskap gick inte att dela), en dold widget slutar animera
// av sig själv (dolt ⇒ ritas aldrig ⇒ invalidaterar aldrig), och bara
// remsan ritas om i stället för hela fönstret. Det är precis hård regel 1:
// tillståndet ändras varje frame, alltså invalidateras det varje frame.
//
// indeterminate går ändå via en SETTER så bytet invalidaterar (annars
// syns det inte förrän något annat råkar väcka fönstret).

namespace glaze {

class Progress : public Widget {
public:

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setMin(float v)   { if (min_ != v)   { min_ = v;   invalidate(); } }
    void setMax(float v)   { if (max_ != v)   { max_ = v;   invalidate(); } }
    void setValue(float v) { if (value_ != v) { value_ = v; invalidate(); } }

    float min() const   { return min_; }
    float max() const   { return max_; }
    float value() const { return value_; }

    // Obestämt läge (setter, inte fält — se filhuvudet).
    void setIndeterminate(bool on);
    bool indeterminate() const { return indeterminate_; }

    // setProp-egenskaper: min, max, value, indeterminate.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

private:
    // Fyllningen respektive den glidande biten i LOGISKA px — EN sanning
    // om geometrin (bara ritningen läser den; widgeten har inget beteende).
    gfx::Rectf fillBounds(const Ctx& c) const;
    gfx::Rectf chunkBounds(const Ctx& c) const;

    float min_   = 0.0f;
    float max_   = 100.0f;
    float value_ = 0.0f;
    bool indeterminate_ = false;
};

} // namespace glaze
