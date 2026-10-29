#pragma once
#include "glaze/draw.h"   // Pen står i drawGlyphs/glyphPen-deklarationerna
#include "glaze/widget.h"

// CaptionButton — fönsterknapparna min/max/close:
// specialtypen för namnlistens knappar, i två FORM-varianter (Shape) —
// Glyph (Windows: ─ ▢ ✕ i temats chrome-färger) och Circle (Mac: traffic
// lights — röd/gul/grön + hover-glyf, med EGNA temaroller
// (trafficClose/Min/Max + Press + trafficGlyph), inte hårdkodade tal).
// Formen heter Shape
// och inte Style för att inte skugga glaze::Style (den app-globala
// stilen) inne i widgetens egen ritkod. Klicket fyrar
// vanliga onClick (regel 6) — kopplingen till fönstrets minimize()/
// maximize()/restore()/close() görs av den som äger knappen (TopBar-
// defaulten; AppWindow-API:t finns redan). Inte fokuserbar —
// fönsterknappar ligger utanför Tab-ordningen (som i OS:et).
//
// MÖNSTER: inbyggd widget — draw()/contentSize() bor i widgetens egen cpp
// (caption_button.cpp) och switchar på c.ui.

namespace glaze {

class CaptionButton : public Widget {
public:
    enum class Kind { Minimize, Maximize, Close };
    // Glyph = Windows fyrkantiga plattor, Circle = macOS trafikljus,
    // Capsule = HUSETS egen form: de tre knapparna är ETT
    // kluster med rundade ändar, och tillståndet tänds som ett fack inne i
    // det. Capsule är default i alla `buildTopBar`-grenar;
    // de två äldre står kvar för att `shape=glyph`/`circle` ska fortsätta
    // round-trippa ur äldre `.glazeui` (hård regel 4).
    enum class Shape { Glyph, Circle, Capsule };

    explicit CaptionButton(Kind k = Kind::Close, Shape s = Shape::Glyph)
        : kind_(k), shape_(s)
    {
    }

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setKind(Kind k)
    {
        if (kind_ == k)
            return;
        kind_ = k;
        invalidate();
    }
    Kind kind() const { return kind_; }

    void setShape(Shape s)
    {
        if (shape_ == s)
            return;
        shape_ = s;
        invalidate();
    }
    Shape shape() const { return shape_; }

    // Maximize-knappens "återställ"-utseende (två överlappande rutor).
    // Knappen känner inte fönstret — den som kopplar klicket speglar
    // fönstrets isMaximized() hit (TopBar-wiringen). Settern INVALIDERAR,
    // och det är bärande här: speglingen sker per repaint.
    void setRestoreGlyph(bool on)
    {
        if (restoreGlyph_ == on)
            return;
        restoreGlyph_ = on;
        invalidate();
    }
    bool isRestoreGlyph() const { return restoreGlyph_; }

    // setProp-egenskaper: kind (minimize/maximize/close), shape
    // (glyph/circle/capsule — nyckeln "style" lever kvar som äldre alias,
    // regel 4-arv), restoreGlyph; resten tar basen.
    //
    // `restoreGlyph` står i settertabellen (`setRestoreGlyph`) och känns
    // också av `setProp`, så previewn utför raden.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

private:
    Kind  kind_;
    Shape shape_;
    bool  restoreGlyph_ = false;

    // De två FORMERNA — privata metoder eftersom en glyf-knapp och ett
    // trafikljus verkligen är två olika saker man ser, inte två grenar i
    // samma ritning.
    void drawGlyphButton(const Ctx& c) const;
    void drawTrafficLight(const Ctx& c) const;
    void drawCapsule(const Ctx& c) const;

    // Själva TECKNET (─ / ▢ / ✕), utan platta. Delas av alla tre
    // ritningarna — glyferna är samma tecken överallt, det är UNDERLAGET
    // som skiljer familjerna (Slate fylld platta, Glaze laminerad platta,
    // Minimalistic hårlinjeruta). `restoreFill` är färgen återställ-glyfens
    // främre ruta fylls med för att dölja överlappet, och den MÅSTE vara
    // det som faktiskt ligger bakom knappen: `titlebar` i Slate/Glaze,
    // `windowBg` i Minimalistic (vars bar är fylld i just den).
    void drawGlyphs(const Ctx& c, const Pen& pen,
                    gfx::Color restoreFill) const;
    Pen  glyphPen(const Ctx& c) const;   // chrome-rollerna för kind/tillstånd

    bool       hot() const;                        // hovrad/nedtryckt + aktiv
    bool       clusterHot() const;                 // NÅGON cirkel i baren het

    // Fackets plats i kapselklustret: index bland de SYNLIGA
    // Capsule-syskonen och deras antal. Det är det som avgör vilken ände
    // som är rundad — döljs maximize-facket blir kapseln två fack, och
    // rundningen måste följa med. {0, 0} = inget kluster (fristående).
    struct ClusterPos { int index = 0; int count = 0; };
    ClusterPos clusterPos() const;
    gfx::Rectf glyphBox(const Ctx& c, float half) const;   // centrerad kvadrat
    gfx::Color circleColor(const Ctx& c) const;    // traffic-rollen för kind
};

} // namespace glaze
