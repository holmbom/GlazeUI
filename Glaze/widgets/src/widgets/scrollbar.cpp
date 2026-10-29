// Scrollbar — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER, men tumgeometrin är DELAD: Draw::scrollThumb/scrollThumbDraw
// ritar exakt samma pixlar som Panels inbyggda overlay-lister — två
// renderare av samma sak MÅSTE ha en sanning. HÄR bor beteendet: tumdrag,
// rännklick sidvis, hjul. Ingen mouseUp-krok behövs: mouseDrag levereras
// bara medan knappen hålls, och nästa mouseDown räknar om greppet.

#include "glaze/widgets/scrollbar.h"

#include <algorithm>
#include <cstdlib>

#include "glaze/draw.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("scrollbar", Scrollbar)

bool Scrollbar::setProp(const std::string& key, const std::string& v)
{
    if (key == "orientation") {
        if (v == "horizontal")    setOrientation(Orientation::Horizontal);
        else if (v == "vertical") setOrientation(Orientation::Vertical);
        else return false;
        return true;
    }
    if (key == "min")   { setMin(float(std::atof(v.c_str())));   return true; }
    if (key == "max")   { setMax(float(std::atof(v.c_str())));   return true; }
    if (key == "value") { setValue(float(std::atof(v.c_str()))); return true; }
    if (key == "page")  { setPage(float(std::atof(v.c_str())));  return true; }
    return Widget::setProp(key, v);
}

gfx::Sizef Scrollbar::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN, mätt i KROPPSRAMEN (= skärmens vid rotation 0):
        // satt axel vinner — ÄVEN tväraxeln.
        // AUTO tvärs = listens tjocklek, AUTO längs = stilens
        // defaultlängd (dock/anchors sträcker den när de finns).
        const float t   = c.style->scrollThick;
        const float len = c.style->scrollDefaultLen;
        const bool  v   = orientation_ == Orientation::Vertical;
        gfx::Sizef  s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = v ? t : len;
        if (s.h <= 0.0f) s.h = v ? len : t;
        return s;
    }
    }
}

void Scrollbar::draw(const Ctx& c)
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;

    switch (c.ui) {
    // GLAZE — ett SPÅR (försänkt yta + bestämd kontur) med en ACCENTYTA som
    // tumme.
    //
    // Tummen är `accentSurface` och inte `button`, och det är MÄTT och inte
    // valt. Knappkroppen mäter 1.76:1 mot en panel och 1.39:1 mot rännan i
    // ljust läge — samma osynlighet som fällde Slates tumme, bara av en
    // annan orsak (Lavenders knapp är ljus mot ljust, Slates ÄR panelen).
    // `accentSurface` mäter 5.67 / 4.48 / 4.97:1 mot panel / ränna /
    // bakgrund i ljust läge och 3.45 / 3.99 / 3.79 i mörkt — över 3:1 i
    // varje kombination tummen kan hamna i.
    //
    // Och den LAMINERAS INTE: en 6 px tumme rymmer ingen kontur (hårlinjen
    // äter en tredjedel, och `Draw::scrollThumbDraw` är en DELAD primitiv
    // som bara fyller), och familjens mätta regel är att laminering kräver
    // en kontur. Laminerad föll tummen till 2.91:1 mot panelen i ljust läge.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + kontur `border`, tumme
    //              `accentSurface`
    //   hover      `accentSurfaceHover`
    //   tryck      `accentSurfacePress`
    //   fokus      — (listen är inte fokuserbar)
    //   inaktiv    — (listen inaktiveras inte i dag)
    case UI::Glaze: {
        const gfx::Rectf b = c.bounds();
        Draw::rect(c, b, std::min(b.w, b.h) / 2.0f, col.appWorkspace,
                   Pen{col.border, m.borderWidth});
        Draw::scrollThumbDraw(c, b, metrics(), m.scrollPad, m.scrollThumbMin,
                              pressed()   ? col.accentSurfacePress
                              : hovered() ? col.accentSurfaceHover
                                          : col.accentSurface);
        break;
    }

    // MINIMALISTIC — INGEN RÄNNA ALLS. Det är
    // hela skillnaden mot de två andra familjerna, och den räcker: en
    // rullningslist utan ränna är en tumme som svävar i innehållet, vilket
    // är precis vad "ingen egen yta i vila" betyder för den här widgeten.
    //
    // Tummen förblir FYLLD och blir inte en outline — två skäl, båda mätta:
    // `Draw::scrollThumbDraw` kan bara fylla (en `Pen` där hade ändrat en
    // DELAD primitiv, och Panels overlay ritar samma pixlar), och en kant på
    // en 6 px tjock tumme äter en tredjedel av den. `textDim` bär den —
    // grindad ≥ 4.5:1 mot bakgrunden, alltså med god marginal över kravet
    // 3:1 för en tillståndsmarkering.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ingen ränna, tumme `textDim`
    //   hover      tummen `text` (den mörknar/ljusnar mot full textstyrka)
    //   tryck      `text` — samma som hover; en 6 px tumme har inte plats
    //              för tre nivåer och trycket syns i att den FLYTTAR SIG
    //   fokus      — (listen är inte fokuserbar)
    //   inaktiv    — (listen inaktiveras inte i dag)
    case UI::Minimalistic:
        Draw::scrollThumbDraw(c, c.bounds(), metrics(), m.scrollPad,
                              m.scrollThumbMin,
                              hovered() || pressed() ? col.text : col.textDim);
        break;

    // SLATE — ett SPÅR med en PLATTA på.
    //
    // RÄNNAN ritas HÄR och aldrig i den delade primitiven. Den fristående
    // listen ÄGER sin remsa och har ingenting bakom sig; Panels
    // overlay-remsor ritas EFTER barnen och svävar över innehållet, så en
    // opak ränna där hade täckt det som står under. Det är hela skälet att
    // de två — som annars ritar samma pixlar — skiljer sig på just den här
    // punkten. Rännan ligger inom c.bounds() och behöver ingen overdraw.
    //
    // TUMMEN är `border`/`textDim`, inte en platta i `button`-familjen:
    // `button` ÄR `panel` i varje genererad palett (`c.button = c.panel;`),
    // och en overlay-tumme i den färgen mäter **1.00:1** mot panelens yta i
    // alla arton genererade Slate-teman — exakt osynlig. Att `button` inte
    // kan vara `panel` om en tumme ska kunna vara en platta är en
    // PALETTfråga och löses inte i förbifarten; `border`/`textDim` är det
    // minsta ingreppet. Rännan står kvar — den bär remsans synlighet.
    //
    // Tummen får INGEN kant: remsan är scrollThick 10 minus 2 × scrollPad 2
    // = 6 logiska px tjock, och en hårlinje äter en tredjedel av den.
    // Därför behåller Draw::scrollThumbDraw sin signatur (en fyllnadsfärg),
    // och `panel.cpp`s drawAfterChildren byter färger i SAMMA ändring —
    // kontraktet är "samma pixlar för samma TILLSTÅND".
    //
    // De fyra tillstånden (princip 7):
    //   vila       ränna `appWorkspace` + hårlinje `border`, tumme `border`
    //   hover      tummen `textDim`
    //   tryck      tummen `textDim` (tryck och hover ser lika ut igen —
    //              priset för återgången, och det är skrivet)
    //   fokus      — (listen är inte fokuserbar)
    //   inaktiv    — (listen inaktiveras inte i dag)
    case UI::Slate:
    default: {
        const gfx::Rectf b = c.bounds();
        Draw::rect(c, b, std::min(b.w, b.h) / 2.0f, col.appWorkspace,
                   Pen{col.border, m.borderWidth});
        Draw::scrollThumbDraw(c, b, metrics(), m.scrollPad, m.scrollThumbMin,
                              hovered() || pressed() ? col.textDim
                                                     : col.border);
        break;
    }
    }
}

// Widgetens tillstånd som råa rullningsmått — ritningen och beteendet
// (träff, dragmappning) läser SAMMA värden.
Draw::ScrollMetrics Scrollbar::metrics() const
{
    return Draw::ScrollMetrics{min_, max_, value(), page_,
                               orientation_ == Orientation::Vertical};
}

// Rännan och tummen i widget-lokala LOGISKA px — ritningen och greppet
// räknar i samma rymd, och tummen kommer ur SAMMA Draw-funktion som ritar.
gfx::Rectf Scrollbar::track(const Ctx& c) const
{
    return Draw::bounds(c, resolve(c));
}

gfx::Rectf Scrollbar::thumb(const Ctx& c) const
{
    return Draw::scrollThumb(metrics(), track(c), c.style->scrollPad,
                             c.style->scrollThumbMin);
}

// Muskoordinaten längs listens EGEN axel, logisk.
float Scrollbar::localPos(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r = resolve(c);
    return orientation_ == Orientation::Vertical ? Draw::localY(c, r, my)
                                                : Draw::localX(c, r, mx);
}

// KLAMPAT VID LÄSNING — `Slider::value`s regel, se headern.
float Scrollbar::value() const
{
    return std::clamp(value_, min_, std::max(min_, max_));
}

void Scrollbar::setValue(float v)
{
    if (value_ == v)
        return;
    value_      = v;
    commitBase_ = value();   // appens värde är inte användarens ändring
    invalidate();
}

// En gräns som flyttar det EFFEKTIVA värdet är appens ändring — baslinjen
// följer med (`Slider::setMin`).
void Scrollbar::setMin(float v)
{
    if (min_ == v)
        return;
    const float förut = value();
    min_              = v;
    if (value() != förut)
        commitBase_ = value();
    invalidate();
}

void Scrollbar::setMax(float v)
{
    if (max_ == v)
        return;
    const float förut = value();
    max_              = v;
    if (value() != förut)
        commitBase_ = value();
    invalidate();
}

// Svarar om listen går att röra efteråt (`Widget::fireEvent`) — en
// onChange-handler får riva den.
bool Scrollbar::userSet(float v)
{
    const float nv = std::clamp(v, min_, std::max(min_, max_));
    if (nv == value())   // det EFFEKTIVA — ett råvärde utanför räknas inte
        return true;
    value_ = nv;
    invalidate();
    return fireEvent(onChange);
}

// KLAR — `Slider::commitFromUser`s form.
bool Scrollbar::commitFromUser()
{
    if (value() == commitBase_)
        return false;
    commitBase_ = value();
    if (!onCommit)
        return false;
    fireEvent(onCommit);   // värdet läses ur sender (value())
    return true;
}

// SLÄPPET committar tumdraget och rännklicket — trycket var handlingen.
bool Scrollbar::mouseUp(const Ctx&, float, float)
{
    return commitFromUser();
}

void Scrollbar::mouseDown(const Ctx& c, float mx, float my)
{
    const gfx::Rectf th   = thumb(c);
    const bool       vert = orientation_ == Orientation::Vertical;
    const float      pos  = localPos(c, mx, my);
    const float      top  = vert ? th.y : th.x;
    const gfx::Rectf r    = resolve(c);

    // På tummen = grepp (dragOff = var i tummen greppet togs, så tummen
    // inte "hoppar" till pekaren); utanför = rännklick en SIDA i klickets
    // riktning (WinForms-beteendet). Träffen prövas i BÅDA axlarna mot den
    // RITADE tummen — tummen är indragen (scrollPad) och ett klick i den
    // synliga sidoremsan ska sidhoppa, inte greppa.
    dragging_ = th.contains(Draw::localX(c, r, mx), Draw::localY(c, r, my));
    if (dragging_)
        dragOff_ = pos - top;
    else if (!userSet(value() + (pos < top ? -page_ : page_)))
        return;   // en handler rev eller flyttade listen
    invalidate();
}

void Scrollbar::mouseDrag(const Ctx& c, float mx, float my)
{
    if (!dragging_)
        return;
    const gfx::Rectf tr   = track(c);
    const gfx::Rectf th   = thumb(c);
    const bool       vert = orientation_ == Orientation::Vertical;
    const float      free = vert ? tr.h - th.h : tr.w - th.w;
    if (free <= 0.0f)
        return;
    const float t = std::clamp(
        (localPos(c, mx, my) - dragOff_ - (vert ? tr.y : tr.x)) / free,
        0.0f, 1.0f);
    userSet(min_ + t * (std::max(min_, max_) - min_));
}

bool Scrollbar::mouseWheel(const Ctx&, float, float, float wheelX, float wheelY)
{
    // Hjul upp (positiv rad) = mot början — Panels konvention. En rad =
    // en tiondels sida (mjukt, enhetsneutralt).
    const float w = orientation_ == Orientation::Vertical ? wheelY : wheelX;
    if (w == 0.0f)
        return false;
    if (userSet(value() - w * std::max(page_ / 10.0f, (max_ - min_) / 100.0f)))
        commitFromUser();   // ett hjulsteg är en hel handling
    return true;
}

} // namespace glaze
