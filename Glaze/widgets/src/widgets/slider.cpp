// Slider — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// ränna, fylld del, tumme, fokusring. Måtten kommer ur stilen
// (c.style), färgerna ur temat — inga tal och inga färger i den här
// filen. BETEENDET (drag, hjul, tangenter) bor också här och läser SAMMA
// geometri som ritningen, projicerad med Draw::localX/localY.
//
// Ingen explicit setPointerCapture: mouseDrag levereras redan medan
// knappen hålls (auto-capture) även utanför widgetens yta — samma
// resonemang som i scrollbar.cpp. Släppet når widgeten ändå (`mouseUp`),
// och där committas draget.

#include "glaze/widgets/slider.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("slider", Slider)

// ------------------------------------------------------- värdet

// KLAMPAT VID LÄSNING — kontraktet står i headern. En övre gräns under den
// undre räknas som den undre (talfältets och datumväljarens regel).
float Slider::value() const
{
    return std::clamp(value_, min_, std::max(min_, max_));
}

void Slider::setValue(float v)
{
    if (value_ == v)
        return;
    value_      = v;
    commitBase_ = value();   // appens värde är inte användarens ändring
    invalidate();
}

// En gräns som flyttar det EFFEKTIVA värdet är appens ändring av värdet —
// baslinjen följer med, annars hade nästa släpp committat den.
void Slider::setMin(float v)
{
    if (min_ == v)
        return;
    const float förut = value();
    min_              = v;
    if (value() != förut)
        commitBase_ = value();
    invalidate();
}

void Slider::setMax(float v)
{
    if (max_ == v)
        return;
    const float förut = value();
    max_              = v;
    if (value() != förut)
        commitBase_ = value();
    invalidate();
}

bool Slider::setProp(const std::string& key, const std::string& v)
{
    if (key == "min")   { setMin(float(std::atof(v.c_str())));   return true; }
    if (key == "max")   { setMax(float(std::atof(v.c_str())));   return true; }
    if (key == "value") { setValue(float(std::atof(v.c_str()))); return true; }
    if (key == "step")  { setStep(float(std::atof(v.c_str())));  return true; }
    if (key == "vertical") {
        setVertical(propparse::parseBool(v));
        return true;
    }
    if (key == "orientation") {   // Scrollbars form — samma ord, samma sak
        if (v == "horizontal")    setVertical(false);
        else if (v == "vertical") setVertical(true);
        else return false;
        return true;
    }
    return Widget::setProp(key, v);
}

gfx::Sizef Slider::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN, mätt i KROPPSRAMEN (= skärmens vid rotation 0):
        // satt axel vinner — ÄVEN tväraxeln.
        // AUTO tvärs = tummens tjocklek (den är bredare än rännan),
        // AUTO längs = stilens defaultlängd.
        const float t   = std::max(c.style->sliderThumb, c.style->sliderTrack);
        const float len = c.style->sliderDefaultLen;
        gfx::Sizef  s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = vertical_ ? t : len;
        if (s.h <= 0.0f) s.h = vertical_ ? len : t;
        return s;
    }
    }
}

// Rännan i widget-lokala LOGISKA px — indragen en halv tumme i vardera
// änden så tummens ytterlägen ryms inom widgetens yta.
gfx::Rectf Slider::trackBounds(const Ctx& c) const
{
    const gfx::Rectf b   = Draw::bounds(c, resolve(c));
    const auto&      m   = *c.style;
    const float      t   = m.sliderTrack;
    const float      pad = m.sliderPad;
    return vertical_
               ? gfx::Rectf{b.x + (b.w - t) / 2.0f, b.y + pad, t,
                            std::max(0.0f, b.h - 2.0f * pad)}
               : gfx::Rectf{b.x + pad, b.y + (b.h - t) / 2.0f,
                            std::max(0.0f, b.w - 2.0f * pad), t};
}

float Slider::fraction() const
{
    const float span = std::max(min_, max_) - min_;
    if (span <= 0.0f)
        return 0.0f;
    return std::clamp((value() - min_) / span, 0.0f, 1.0f);
}

// Tummen centrerad på värdets punkt längs rännan. Vertikalt ligger min
// NEDERST (OS-konventionen), därför den inverterade andelen.
gfx::Rectf Slider::thumbBounds(const Ctx& c) const
{
    const gfx::Rectf tr = trackBounds(c);
    const float      d  = c.style->sliderThumb;
    const float      f  = fraction();
    const float      cx = vertical_ ? tr.x + tr.w / 2.0f : tr.x + tr.w * f;
    const float      cy = vertical_ ? tr.y + tr.h * (1.0f - f)
                                   : tr.y + tr.h / 2.0f;
    return gfx::Rectf{cx - d / 2.0f, cy - d / 2.0f, d, d};
}

void Slider::draw(const Ctx& c)
{
    const gfx::Rectf tr  = trackBounds(c);
    const gfx::Rectf th  = thumbBounds(c);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;

    // Den passerade delen går upp till tummens MITT — då möts fyllning och
    // tumme utan glapp. Samma uträkning i alla tre ritningarna: det är
    // geometri och inte formspråk.
    const float mid = vertical_ ? th.y + th.h / 2.0f : th.x + th.w / 2.0f;
    const gfx::Rectf done =
        vertical_ ? gfx::Rectf{tr.x, mid, tr.w, tr.y + tr.h - mid}
                 : gfx::Rectf{tr.x, tr.y, mid - tr.x, tr.h};

    switch (c.ui) {
    // GLAZE — ett SPÅR med en LAMINERAD PLATTA på.
    //
    // ETT SPÅR LAMINERAS INTE. Rännan är en FÖRSÄNKNING, och ljus samlas
    // inte i botten av en fördjupning — den får sin försänkta yta och sin
    // bestämda kontur och inget mer. Fyllningen är `accentSurface`
    // (accenten som YTA) och inte `accent` (accenten som BETYDELSE) — i
    // mörkt läge är de två olika kulörer.
    //
    // DEN FYLLDA DELEN LAMINERAS INTE HELLER, trots att den är en kropp:
    // `sheen` är VITT, och ett vitt fält över fyllningen i LJUST läge sänker
    // den från 4.48:1 till **2.30:1** mot rännan — under 3:1, alltså under
    // kravet för precis den mätning som gör spåret läsbart. Regeln gäller
    // hela familjen:
    //
    //     LAMINERING KRÄVER EN KONTUR. Ett ljusfält drar ytan mot den ljusa
    //     änden, så en laminerad yta kan aldrig bära sin egen separation —
    //     den måste ha en kant som gör det. Knappen, kryssrutan, radion och
    //     TUMMEN har en; en fyllnad inne i ett spår har det inte.
    //
    // Det är samma sak som gäller knappen ("konturen är BÄRANDE, inte
    // dekoration") — generaliserad till varje laminerad yta.
    //
    // TUMMEN är den enda widget utom knappen som kastar SKUGGA. Regeln är
    // "skugga bara på fristående plattor": rännan ligger i ytan, tummen
    // ligger PÅ den. Måtten är `sliderShadowDy/Blur` och inte knappens —
    // 16 px tumme och 27 px knapp tål olika mycket.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + kontur `border`, passerad del
    //              `accentSurface` (4.48:1 / 3.99:1 mot rännan), tumme
    //              `button` + laminering + kontur `buttonBorder` + skugga
    //   hover      tummen går MOT accenten (`buttonHover`)
    //   tryck      tummens skugga OCH laminering försvinner, kroppen
    //              `buttonPress` — samma tryckrörelse som knappens
    //   fokus      fristående ring runt TUMMEN, inte runt hela remsan
    //   inaktiv    den passerade delen `textDim`; tummen behåller sin kropp
    //              (Slates val, ärvt med flit)
    case UI::Glaze: {
        const float trackRad = std::min(tr.w, tr.h) / 2.0f;
        const float doneRad  = std::min(done.w, done.h) / 2.0f;

        Draw::rect(c, tr, trackRad, col.appWorkspace,
                   Pen{col.border, m.borderWidth});

        Draw::rect(c, done, doneRad,
                   isEnabled() ? col.accentSurface : col.textDim);

        if (!pressed())
            Draw::shadow(c, th, th.w / 2.0f, 0.0f, m.sliderShadowDy,
                         m.sliderShadowBlur, col.shadow);

        Draw::circle(c, th, thumbFill(c));
        if (!pressed())
            Draw::laminate(c, th, th.w / 2.0f, col.sheen, m.sheenLower,
                           col.sheenHigh, m.sheenUpper);
        Draw::frame(c, th, th.w / 2.0f,
                    Pen{col.buttonBorder, m.borderWidth});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(th, -m.focusOffset),
                        th.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }

    // MINIMALISTIC — SPÅRET ÄR ETT STRECK, inte en ränna
    //. En hårlinje i full längd, den
    // passerade delen i `accent` ovanpå. Ingen försänkt yta, ingen kontur
    // runt den, ingen radie att tala om — det finns ingen ränna att runda.
    //
    // TUMMEN är en FYLLD `accentSurface`-cirkel och inte en outline. Det är
    // ett medvetet undantag: en ofylld tumme måste
    // maskera strecket under sig för att inte se genomborrad ut, och den
    // enda färg som kunde maskera vore "färgen bakom mig" — en roll
    // ramverket INTE har. Fyllde vi med `windowBg` blev masken en synlig
    // disk så fort slidern satt på en panel eller i en bar. En opak
    // accentcirkel behöver ingen maskering alls och följer dessutom regeln
    // "fyllning betyder tillstånd": tummen ÄR värdet.
    //
    // Men fyllningen ensam RÄCKER INTE, och det är mätt: `accentSurface`
    // mot bakgrunden faller till 2.28:1 (Indigo/mörk) över nio hues × två
    // lägen, alltså under 3:1 för en tillståndsmarkering. Tummen bär därför
    // en hårlinje i `buttonBorder` — den roll som ÄR grindad ≥ 3:1 mot
    // bakgrunden och som redan bär varje annan form i familjen. Kanten är
    // formen; det gäller den här widgeten också. (Försöket att i stället
    // grinda `accentSurface`/bg står dokumenterat i
    // tests/contrast_gate.cpp — det fällde fyra hues i Minimalistic och sju
    // i Slate.)
    //
    // De fyra tillstånden (princip 7):
    //   vila       streck `border`, passerad del `accent`, tumme
    //              `accentSurface` + hårlinje `buttonBorder`
    //   hover      tummen `accentSurfaceHover`
    //   tryck      tummen `accentSurfacePress` — aldrig en förskjutning
    //   fokus      fristående ring runt TUMMEN, halv tjocklek
    //   inaktiv    passerad del och tumme `textDim`, kant `border`
    case UI::Minimalistic: {
        const Pen rail{col.border, m.borderWidth};
        const Pen filled{isEnabled() ? col.accent : col.textDim,
                         m.borderWidth};
        if (vertical_) {
            const float x = tr.x + tr.w / 2.0f;
            Draw::line(c, x, tr.y, x, tr.y + tr.h, rail);
            Draw::line(c, x, done.y, x, done.y + done.h, filled);
        } else {
            const float y = tr.y + tr.h / 2.0f;
            Draw::line(c, tr.x, y, tr.x + tr.w, y, rail);
            Draw::line(c, done.x, y, done.x + done.w, y, filled);
        }

        Draw::circle(c, th,
                     !isEnabled()  ? col.textDim
                     : pressed()   ? col.accentSurfacePress
                     : hovered()   ? col.accentSurfaceHover
                                   : col.accentSurface,
                     Pen{isEnabled() ? col.buttonBorder : col.border,
                         m.borderWidth});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(th, -m.focusOffset),
                        th.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }

    // SLATE — ett SPÅR med en PLATTA på. Rännan är
    // en försänkt yta med en hårlinje som ANTYDAN — exakt Progress recept,
    // som är formspråkets förebild för spår. Rännan målas inte i `border`:
    // det är en LINJEfärg, och som yta hade den gjort sliderns ränna och
    // progressens till två olika svar på samma fråga.
    // Spårets synlighet ligger i FYLLNADEN (`accent` mot `appWorkspace`,
    // grindad ≥ 3:1 i tests/contrast_gate.cpp), inte i kanten — `border`
    // mot `appWorkspace` når 1.91–2.14:1 och kan aldrig bära formen.
    //
    // Tummen bär knappens grammatik — fylld kropp, hårlinjekant, färgen
    // växlar med tillståndet — men PLATT: bara Button lyfter, och det är ett
    // skrivet undantag från princip 5 som behåller sin betydelse bara så
    // länge det är ensamt.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + hårlinje, passerad del `accent`,
    //              tumme `button` + hårlinje `border`
    //   hover      tummen går till `buttonHover`, kanten till `accent`
    //   tryck      tummen går till `buttonPress` (skilt från hover)
    //   fokus      fristående ring runt TUMMEN, inte runt hela remsan
    //   inaktiv    den passerade delen dimmas till `textDim`; tummen står
    //              kvar (ett val, se SLATE.md "Öppet")
    case UI::Slate:
    default: {   // okänd ritning faller till Slate — de två andra står ovan
        Draw::rect(c, tr, std::min(tr.w, tr.h) / 2.0f, col.appWorkspace,
                   Pen{col.border, m.borderWidth});
        Draw::rect(c, done, std::min(done.w, done.h) / 2.0f,
                   isEnabled() ? col.accent : col.textDim);
        Draw::circle(c, th, thumbFill(c), Pen{hotBorder(c), m.borderWidth});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(th, -m.focusOffset),
                        th.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }
    }
}

// Tummens kropp — de tre gripbara tillstånden. Samma trappa som Buttons
// fillColor, utan dess inaktiva gren: en inaktiv slider säger det med den
// dimmade FYLLNADEN, och tummen lämnas orörd där.
gfx::Color Slider::thumbFill(const Ctx& c) const
{
    const auto& col = c.theme->colors;

    if (pressed()) return col.buttonPress;
    if (hovered()) return col.buttonHover;
    return col.button;
}

// Klampa, kvantisera mot step och avfyra — ETT ställe.
// Svarar om reglaget går att röra efteråt (`Widget::fireEvent`): en
// onChange-handler får riva det.
bool Slider::userSet(float v)
{
    const float hi = std::max(min_, max_);
    if (step_ > 0.0f)
        v = min_ + std::round((v - min_) / step_) * step_;
    v = std::clamp(v, min_, hi);
    if (v == value())   // det EFFEKTIVA — ett råvärde utanför räknas inte
        return true;
    value_ = v;
    invalidate();
    return fireEvent(onChange);
}

// KLAR — onCommit när värdet skiljer sig från förra commiten.
// Svarar om en handler kördes: släppet ger då fönstret en full omritning
// (`mouseUp`-kontraktet).
bool Slider::commitFromUser()
{
    if (value() == commitBase_)
        return false;
    commitBase_ = value();
    if (!onCommit)
        return false;
    fireEvent(onCommit);   // värdet läses ur sender (value())
    return true;
}

// Ett TANGENT- eller HJULSTEG är en hel handling: onChange och sedan
// onCommit, för samma steg — och commiten bara om reglaget lever efter
// onChange.
void Slider::stepFromUser(float v)
{
    if (userSet(v))
        commitFromUser();
}

// Musen -> värde: koordinaten görs LOGISK mot widgetens rect och mäts mot
// SAMMA ränna som ritningen använder.
void Slider::setValueFromLocal(const Ctx& c, float mx, float my)
{
    const gfx::Rectf base = resolve(c);
    const gfx::Rectf tr   = trackBounds(c);
    float            t    = 0.0f;
    if (vertical_)
        t = tr.h > 0.0f ? (tr.y + tr.h - Draw::localY(c, base, my)) / tr.h
                        : 0.0f;
    else
        t = tr.w > 0.0f ? (Draw::localX(c, base, mx) - tr.x) / tr.w : 0.0f;
    userSet(min_ + std::clamp(t, 0.0f, 1.0f) * (std::max(min_, max_) - min_));
}

void Slider::mouseDown(const Ctx& c, float mx, float my)
{
    setValueFromLocal(c, mx, my);   // hoppa till punkten (WinForms-beteendet)
}

void Slider::mouseDrag(const Ctx& c, float mx, float my)
{
    setValueFromLocal(c, mx, my);
}

// SLÄPPET committar draget — också ett som slutade utanför reglaget eller
// efter en inaktivering (släppet kommer ändå, widget.h).
bool Slider::mouseUp(const Ctx&, float, float)
{
    return commitFromUser();
}

// Steglöst reglage (step = 0) flyttas ändå av tangenter och hjul — då i
// hundradelar av intervallet. Definierad EN gång: hjul och tangentbord ska
// aldrig kunna glida isär.
float Slider::effectiveStep() const
{
    if (step_ > 0.0f)
        return step_;
    return (std::max(min_, max_) - min_) / 100.0f;   // hundra steg över hela
}

bool Slider::mouseWheel(const Ctx& c, float, float, float wheelX, float wheelY)
{
    // HJULET KRÄVER FOKUS: utan fokus bubblar det till närmaste
    // rullande förälder — kontraktet står i slider.h.
    if (!focused())
        return false;
    // Hjul upp = uppåt i värde (motsatt Scrollbar, där upp är mot början).
    const float w = wheelY != 0.0f ? wheelY : wheelX;
    if (w == 0.0f)
        return false;
    stepFromUser(value() + wheelNotches(c, w) * effectiveStep());
    return true;
}

bool Slider::keyDown(gfx::Key k, std::uint32_t)
{
    // PageUp/PageDown finns i gfx::Key, men reglaget har
    // inget STORSTEG än (WinForms `LargeChange`) — ett eget tillägg.
    // Numpaden når hit VIKT (`AppWindow`), så numpad-vänster är `Left`.
    const float s = effectiveStep();
    switch (k) {
    case gfx::Key::Left:
    case gfx::Key::Down:  stepFromUser(value() - s); return true;
    case gfx::Key::Right:
    case gfx::Key::Up:    stepFromUser(value() + s); return true;
    case gfx::Key::Home:  stepFromUser(min_);       return true;
    case gfx::Key::End:   stepFromUser(std::max(min_, max_)); return true;
    default:              return false;
    }
}

} // namespace glaze
