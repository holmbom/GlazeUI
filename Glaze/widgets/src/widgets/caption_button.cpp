// CaptionButton — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat bär färgerna — inga
// tal och inga färger i den här filen (trafikljusens nyanser är EGNA
// temaroller). De två FORM-varianterna (Shape::Glyph = Windows-glyfer,
// Shape::Circle = Mac traffic lights) är privata metoder — de är verkligen
// två olika saker man ser, inte två grenar i samma ritning. Beteendet är
// bara kind/shape/restoreGlyph-fälten; klicket är basens clicked() →
// onClick, och kopplingen till fönstrets minimize()/maximize()/close()
// görs av ägaren (TopBar-wiringen i app_window.cpp).

#include "glaze/widgets/caption_button.h"

#include <gfx/platform.h>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerat typnamn — paletten/preview och headless-tester.
GLAZE_REGISTER_WIDGET("captionbutton", CaptionButton)

bool CaptionButton::setProp(const std::string& key, const std::string& value)
{
    if (key == "kind") {
        if (value == "minimize" || value == "min")      setKind(Kind::Minimize);
        else if (value == "maximize" || value == "max") setKind(Kind::Maximize);
        else if (value == "close")                      setKind(Kind::Close);
        else
            gfx::log("setProp(%s): ogiltig kind (förväntar minimize/"
                     "maximize/close): %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "shape" || key == "style") {
        // C++-API:t heter alltid shape; "style" lever kvar som äldre alias
        // så äldre filer fortsätter läsas (hård regel 4).
        if (value == "glyph")        setShape(Shape::Glyph);
        else if (value == "circle")  setShape(Shape::Circle);
        else if (value == "capsule") setShape(Shape::Capsule);
        else
            gfx::log("setProp(%s): ogiltig shape (förväntar glyph/circle/"
                     "capsule): %s (ignoreras)", name.c_str(), value.c_str());
        return true;
    }
    if (key == "restoreGlyph") {
        // Samma bool-form som varje annan bool-egenskap (checked/border/
        // clip): allt som inte är yes/true/1 är false — ingen egen felgren.
        setRestoreGlyph(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);
}

gfx::Sizef CaptionButton::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // I en Bar ges höjden av flexen (barens fulla höjd) — defaults
        // gäller fristående användning och GlazeBuilder-preview.
        //
        // Kapseln svarar med FACKETS bredd och barens FULLA höjd, inte
        // `capsuleH`: klustret är 26 px högt men klickytan är hela remsan,
        // precis som Windows-formens platta. En liten knapp med stor
        // träffyta är hela poängen med att chromet äger sin höjd.
        const auto& m      = *c.style;
        const bool  circle = shape_ == Shape::Circle;
        const float dw     = circle              ? m.trafficSize
                             : shape_ == Shape::Capsule ? m.capsuleSeg
                                                       : m.captionBtnW;
        const float dh     = circle ? m.trafficSize : m.barHeight;
        return gfx::Sizef{width > 0.0f ? width : dw,
                          height > 0.0f ? height : dh};
    }
    }
}

void CaptionButton::draw(const Ctx& c)
{
    // KAPSELN ligger FÖRE familjeswitchen och ritas alltså likadant i alla
    // tre. Skälet är inte trafikljusens — de speglar en
    // konvention vi inte äger — utan att namnlisten ÄR chrome: den ska se
    // likadan ut oavsett vilken ritning appens innehåll använder. Ett
    // fönster byter inte skepnad för att widgetarna inuti gör det.
    if (shape_ == Shape::Capsule) {
        drawCapsule(c);
        return;
    }

    switch (c.ui) {
    // SLATE — CHROME, och därför utanför de fyra klasserna
    //. Knapparna ligger i namnlisten och har en EGEN
    // tillståndsmatris i temats chrome-/traffic-roller, som speglar hur
    // backendens native chrome ritar. Att lägga `button`-familjens plattor
    // här hade brutit den speglingen — namnlisten ska läsa som namnlist,
    // inte som en rad knappar.
    //
    // De fyra tillstånden (princip 7), GLYPH-formen (Windows):
    //   vila       ingen platta alls — bara glyfen i `chromeGlyph`
    //   hover      platta `chromeHover`, stängknappen `chromeClose` (röd
    //              av hävd) med `chromeCloseGlyph`
    //   tryck      platta `chromePress` (stängknappen är röd redan i hover)
    //   fokus      — (INTE fokuserbar; fönsterknappar tar aldrig tabbstopp)
    //   inaktiv    ingen platta (hot() kräver isEnabled)
    // CIRKEL-formen (Mac): vila/tryck i traffic-rollerna, och glyfen tänds
    // vid GRUPP-hover (clusterHot) — macOS-beteendet.
    //
    // TRAFIKLJUSEN (Shape::Circle) ritas LIKADANT i alla tre familjerna, och
    // det är ett beslut: de är macOS-namnlistens cirklar och deras form är
    // en KONVENTION vi speglar, inte en design vi äger. Att laminera dem
    // eller göra dem till outlines hade gjort dem obegripliga — en gul ring
    // är inte "minimera".
    case UI::Slate:
    default:
        if (shape_ == Shape::Circle)
            drawTrafficLight(c);
        else
            drawGlyphButton(c);
        break;

    // GLAZE — glyfknapp med LAMINERAD hover-platta OCH en BESTÄMD KANT
    //. Kanten är inte dekoration utan det som gör
    // hovern synlig alls: `chromeHover` mot `titlebar` mäter **1.14:1** i
    // ljust läge (och 1.03:1 laminerad — lamineringen ensam hade nästan
    // släckt hovern), medan hårlinjen i `border` mäter 3.53:1 / 4.24:1.
    // Chromets hover-tvätt är alltså för svag för att vara en markering i
    // den här paletten, precis som familjens knappkropp är för blek för att
    // bära sin egen form — och lösningen är densamma: konturen bär.
    //
    // Det är också varför lamineringen FÅR finnas här: familjens regel är
    // att laminering kräver en kontur, och här finns en.
    //
    // Stängknappen får varken kant eller laminering: dess röda platta är en
    // VARNING och mäter starkt av sig själv; ett ljusfält gör den mjukare,
    // alltså mindre varning.
    //
    // De fyra tillstånden (princip 7), GLYPH-formen:
    //   vila       ingen platta — bara glyfen i `chromeGlyph`
    //   hover      platta `chromeHover` + laminering + hårlinje `border`;
    //              stängknappen `chromeClose` utan bådadera, glyf
    //              `chromeCloseGlyph`
    //   tryck      platta `chromePress` + kant, ingen laminering
    //   fokus      — (INTE fokuserbar)
    //   inaktiv    ingen platta (hot() kräver isEnabled)
    case UI::Glaze: {
        if (shape_ == Shape::Circle) {
            drawTrafficLight(c);
            break;
        }
        const auto& col = c.theme->colors;
        const auto& m   = *c.style;
        if (hot()) {
            const gfx::Rectf b = c.bounds();
            const bool isClose = kind_ == Kind::Close;
            Draw::rect(c, b, 0.0f,
                       isClose     ? col.chromeClose
                       : pressed() ? col.chromePress
                                   : col.chromeHover);
            if (!isClose) {
                if (!pressed())
                    Draw::laminate(c, b, 0.0f, col.sheen, m.sheenLower,
                                   col.sheenHigh, m.sheenUpper);
                Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
            }
        }
        drawGlyphs(c, glyphPen(c),
                   hot() ? col.chromeHover : col.titlebar);
        break;
    }

    // MINIMALISTIC — glyfknapp UTAN platta:
    // bara glyfen, och hover ritas som en HÅRLINJERUTA. Familjens grundsats
    // ("ingen egen yta i vila, kanten är formen") gäller chromet också — en
    // fylld hover-platta i en namnlist som själv är ofylld hade varit den
    // enda färgade ytan i hela fönstret.
    //
    // Återställ-glyfens överlapp fylls i `windowBg` och inte i `titlebar`:
    // Minimalistics bar ÄR `windowBg`, och `titlebar` där hade lämnat en
    // färgplätt mitt i den. Bar, CaptionButton och chromets färgsättning är
    // ETT paket (se bar.cpp).
    //
    // De fyra tillstånden (princip 7), GLYPH-formen:
    //   vila       bara glyfen i `chromeGlyph`
    //   hover      hårlinjeruta `buttonBorder`; stängknappen `chromeClose`
    //              (både ruta och glyf — rött av hävd, utan fyllning)
    //   tryck      rutan går till `accent`, som knappens tryckta kant
    //   fokus      — (INTE fokuserbar)
    //   inaktiv    ingen ruta
    case UI::Minimalistic: {
        if (shape_ == Shape::Circle) {
            drawTrafficLight(c);
            break;
        }
        const auto& col = c.theme->colors;
        if (hot())
            Draw::frame(c, c.bounds(), 0.0f,
                        Pen{kind_ == Kind::Close ? col.chromeClose
                            : pressed()         ? col.accent
                                                : col.buttonBorder,
                            c.style->borderWidth});
        drawGlyphs(c,
                   Pen{(kind_ == Kind::Close && hot()) ? col.chromeClose
                                                      : col.chromeGlyph,
                       c.style->captionStroke},
                   col.windowBg);
        break;
    }
    }
}

// Knappen är HET när den lyser upp: hovrad eller nedtryckt och aktiv.
bool CaptionButton::hot() const
{
    return isEnabled() && (hovered() || pressed());
}

// Glyfens ruta: centrerad kvadrat med stilens halva sida som radie.
gfx::Rectf CaptionButton::glyphBox(const Ctx& c, float half) const
{
    const gfx::Rectf b = c.bounds();
    return gfx::Rectf{b.x + b.w / 2.0f - half, b.y + b.h / 2.0f - half,
                      2.0f * half, 2.0f * half};
}

// Glyf-varianten (Windows): spegel av backend-chromets drawButton —
// hover/tryck-platta (close röd av hävd), linje-glyfer utan font,
// restore = två överlappande rutor (restoreGlyph — wiringen speglar
// fönstrets isMaximized() hit).
void CaptionButton::drawGlyphButton(const Ctx& c) const
{
    const auto& col = c.theme->colors;

    if (hot())
        Draw::rect(c, c.bounds(), 0.0f,
                   kind_ == Kind::Close ? col.chromeClose
                   : pressed()         ? col.chromePress
                                       : col.chromeHover);

    drawGlyphs(c, glyphPen(c), hot() ? col.chromeHover : col.titlebar);
}

// Glyfens penna i Slates och Glazes chrome-roller — den röda stängplattan
// kräver en egen glyffärg för att texten ska hålla mot rött.
Pen CaptionButton::glyphPen(const Ctx& c) const
{
    const auto& col = c.theme->colors;
    return Pen{(kind_ == Kind::Close && hot()) ? col.chromeCloseGlyph
                                              : col.chromeGlyph,
               c.style->captionStroke};
}

// SJÄLVA TECKNET — ─ / ▢ / ✕ — och ingenting annat. Plattan (eller rutan,
// eller ingenting) ritas av respektive UI-gren innan; det här steget är
// gemensamt, för glyferna ÄR samma tecken i alla tre familjerna. En kopia
// per gren hade varit tre chanser att rita ett snett kryss.
//
// `restoreFill` är färgen den FRÄMRE rutan i återställ-glyfen fylls med för
// att dölja överlappet mot den bakre. Den måste vara VAD SOM FAKTISKT LIGGER
// BAKOM knappen — `titlebar` i Slate/Glaze, `windowBg` i Minimalistic vars
// bar är fylld i just den färgen. Får den fel värde blir det en färgplätt
// mitt i namnlisten.
void CaptionButton::drawGlyphs(const Ctx& c, const Pen& pen,
                               gfx::Color restoreFill) const
{
    const auto&      m = *c.style;
    const gfx::Rectf g = glyphBox(c, m.captionGlyph);

    switch (kind_) {
    case Kind::Minimize:
        Draw::minus(c, g, pen);
        break;
    case Kind::Maximize:
        if (restoreGlyph_) {
            // Två rutor: den bakre skjuts upp/höger, den främre fylls i
            // knappens egen bottenfärg så överlappet döljs.
            const gfx::Rectf back{g.x + m.restoreOffset, g.y,
                                  g.w - m.restoreOffset, g.h - m.restoreOffset};
            const gfx::Rectf front{g.x, g.y + m.restoreOffset,
                                   g.w - m.restoreOffset,
                                   g.h - m.restoreOffset};
            Draw::frame(c, back, 0.0f, pen);
            Draw::rect(c, front, 0.0f, restoreFill, pen);
        } else {
            Draw::frame(c, g, 0.0f, pen);
        }
        break;
    case Kind::Close:
        Draw::cross(c, g, pen);
        break;
    }
}

// KAPSEL-varianten: de tre knapparna är ETT kluster med rundade ändar, och
// tillståndet tänds som ett FACK inne i det.
//
// De fyra tillstånden (princip 7):
//   vila       bara kapselkroppen `capsuleBody` + konturen `capsuleBorder`
//   hover      facket fylls `capsuleHover` — DÄMPAT, inte full accentyta;
//              stängfacket `chromeClose` med `chromeCloseGlyph`
//   tryck      facket fylls `capsulePress`
//   fokus      — (INTE fokuserbar; fönsterknappar tar aldrig tabbstopp)
//   inaktiv    inget tänt fack (hot() kräver isEnabled)
//
// Glyfen byter INTE färg vid HOVER (utom på det röda stängfacket): med en
// dämpad fyllning håller `chromeGlyph` mätbart mot den i båda lägena, och
// en glyf som byter färg samtidigt som ytan tänds är två signaler för en
// händelse. Vid TRYCK gör den det ändå — då är fyllningen hela
// `accentSurfacePress` och glyfen måste vara `onAccent` för att synas
// (grindat som `onAcc/press`). Hovern är tyst, trycket är helt.
void CaptionButton::drawCapsule(const Ctx& c) const
{
    const auto&      col = c.theme->colors;
    const auto&      m   = *c.style;
    const ClusterPos p   = clusterPos();

    // Facket är hela remsan (klickytan); kapseln är `capsuleH` hög och
    // v-centrerad i den.
    const gfx::Rectf b = c.bounds();
    const gfx::Rectf seg{b.x, b.y + (b.h - m.capsuleH) / 2.0f, b.w,
                         m.capsuleH};
    const float      r = m.capsuleH / 2.0f;

    // KROPPEN. Rundningen hör till klustrets ÄNDAR, och en per-hörn-radie
    // finns inte i `Draw` — så den rundade recten sträcks `r` FÖRBI facket
    // åt de håll som är inre skarvar och klipps till facket. Bågen hamnar
    // då utanför klippet där den inte ska synas, medan över- och
    // underkanten fortsätter obrutna genom hela kapseln. Ingen ny primitiv.
    const bool first = p.index == 0;
    const bool last  = p.count <= 1 || p.index == p.count - 1;
    const gfx::Rectf body{seg.x - (first ? 0.0f : r), seg.y,
                          seg.w + (first ? 0.0f : r) + (last ? 0.0f : r),
                          seg.h};
    Draw::clip(c, seg);
    Draw::rect(c, body, r, col.capsuleBody,
               Pen{col.capsuleBorder, m.borderWidth});
    Draw::endClip(c);

    // DET TÄNDA FACKET — insatt `capsuleInset` så det aldrig tar i
    // konturen; annars läser klustret som en enda knapp
    // vars kant flyttar sig.
    const bool isClose = kind_ == Kind::Close;
    gfx::Color under    = col.capsuleBody;
    gfx::Color glyph    = col.chromeGlyph;
    if (hot()) {
        under = isClose     ? col.capsuleClose
                : pressed() ? col.capsulePress
                            : col.capsuleHover;
        if (isClose)
            glyph = col.chromeCloseGlyph;
        // GLYFEN FLIPPAR INTE vid tryck: med en NEUTRAL kapsel är
        // `capsulePress` en chrome-ton, och `onAccent` — den av
        // nästan-vit/nästan-svart som mäter högst mot ACCENTytan — har ingen
        // relation till den (1.31–2.20:1). `chromeGlyph` är rätt tecken på en
        // chrome-yta, och det är vad `glyph` redan är.
        const gfx::Rectf lit = Draw::inset(seg, m.capsuleInset);
        Draw::rect(c, lit, lit.h / 2.0f, under);
    }

    // `under` är det som FAKTISKT ligger bakom glyfen — återställ-glyfens
    // främre ruta fylls med den.
    drawGlyphs(c, Pen{glyph, m.capsuleStroke}, under);
}

// Fackets plats i klustret — bara SYNLIGA kapsel-syskon räknas, så att
// dölja maximize (setMaximizeBox(false)) ger en två-facks kapsel med rätt
// rundade ändar i stället för en lucka. Utan förälder eller ensam widget:
// {0, 0} ⇒ båda ändarna rundade, alltså ett helt piller.
CaptionButton::ClusterPos CaptionButton::clusterPos() const
{
    ClusterPos p{};
    if (!parent())
        return p;
    for (const Widget* sib : parent()->children()) {
        const auto* cb = dynamic_cast<const CaptionButton*>(sib);
        if (!cb || cb->shape_ != Shape::Capsule || !cb->isVisible())
            continue;
        if (cb == this)
            p.index = p.count;
        ++p.count;
    }
    return p;
}

// Cirkel-varianten (Mac traffic lights): cirkeln v-centrerad i ytan med
// stilens diameter, färgen ur temats traffic-roller (vila/tryck), och en
// glyf som tänds vid GRUPP-hover — macOS-beteendet: rör man en cirkel
// tänds alla. TopBar-wiringen invalidaterar syskonen vid varje hover-byte
// så de faktiskt ritas om (per-widget-damagen ser bara den hovrade).
void CaptionButton::drawTrafficLight(const Ctx& c) const
{
    const auto&      col = c.theme->colors;
    const auto&      m   = *c.style;
    const gfx::Rectf dot = glyphBox(c, m.trafficSize / 2.0f);

    Draw::circle(c, dot, circleColor(c));

    if (!clusterHot())
        return;

    // Hover-glyfen: − / + / ×, FET nog att synas på retina (hårlinjer
    // försvinner i 14 pt-cirkeln).
    const gfx::Rectf g = glyphBox(c, m.trafficGlyph);
    const Pen        pen{col.trafficGlyph, m.trafficStroke};
    switch (kind_) {
    case Kind::Minimize: Draw::minus(c, g, pen); break;
    case Kind::Maximize: Draw::plus(c, g, pen);  break;
    case Kind::Close:    Draw::cross(c, g, pen); break;
    }
}

gfx::Color CaptionButton::circleColor(const Ctx& c) const
{
    const auto& col = c.theme->colors;
    switch (kind_) {
    case Kind::Close:
        return pressed() ? col.trafficClosePress : col.trafficClose;
    case Kind::Minimize:
        return pressed() ? col.trafficMinPress : col.trafficMin;
    default:
        return pressed() ? col.trafficMaxPress : col.trafficMax;
    }
}

// Grupp-hovern: någon cirkel-knapp i samma bar är het.
bool CaptionButton::clusterHot() const
{
    if (hot())
        return true;
    if (!parent())
        return false;
    for (const Widget* sib : parent()->children()) {
        const auto* cb = dynamic_cast<const CaptionButton*>(sib);
        if (cb && cb->shape_ == Shape::Circle && cb->hot())
            return true;
    }
    return false;
}

} // namespace glaze
