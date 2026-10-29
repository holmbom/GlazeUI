// Panel — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// Draw ritar, c.style bär måtten och temat färgerna. Innehållsytan är
// panelens EGEN geometri (innerBounds i logiska px = sanningen; innerRect
// är dess FYSISKA projektion för hit-test/childArea) — ingen dubbel
// sanning om måtten. BETEENDET (scroll, klipp, hjul-routing, overlay-
// listerna) bor också här. En EGEN container ser exakt likadan ut.

#include "glaze/widgets/panel.h"

#include <algorithm>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

bool Panel::setProp(const std::string& key, const std::string& value)
{
    if (key == "border") {
        setBorder(propparse::parseBool(value));
        return true;
    }
    if (key == "clip") {
        setClip(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);
}

// Registrerat typnamn — GlazeBuilders palett/preview skapar paneler via
// registret; barnen byggs med adopt (genererad kod eller C++).
GLAZE_REGISTER_WIDGET("panel", Panel)

gfx::Sizef Panel::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // AXELREGELN: Panel behåller 0×0 och
        // får ALDRIG en auto-storlek ur stilen. En container är en
        // PLACERINGSYTA som appen dimensionerar — ett stilmått hade varit
        // en gissning om något bara appen vet.
        //
        // AUTOSTORLEK UR BARNEN är en annan sak: `Widget::childrenExtent(c)`
        // finns och är OPT-IN. Beslutet ovan gäller ändå — en `Panel` ska fortsätta
        // vara 0×0, för den är en yta man placerar saker i och inte en
        // komposit som VET vad den innehåller. En komposit-författare
        // anropar hjälparen ur sin EGEN `contentSize()`.
        return gfx::Sizef{width, height};
    }
}

// Innehållsytan i LOGISKA px — SANNINGEN: med ram dras barnytan in en
// ramtjocklek så innehållet inte ritar över ramen. Ritningen läser den
// direkt; hit-test/childArea läser den fysiska projektionen nedan.
gfx::Rectf Panel::innerBounds(const Ctx& c) const
{
    const gfx::Rectf b = Draw::bounds(c, resolve(c));
    return border_ ? Draw::inset(b, c.style->panelInset) : b;
}

// Innehållsytan i FYSISKA px. PUBLIK: scroll, klipp, hit-test OCH
// subklasserna (PropertyGrids rader) utgår härifrån.
gfx::Rectf Panel::innerRect(const Ctx& c) const
{
    return Draw::project(c, resolve(c), innerBounds(c));
}

void Panel::draw(const Ctx& c)
{
    switch (c.ui) {
    // MINIMALISTIC — "Ytor separeras med en LINJE, inte med en fyllning"
    //. Panelen har alltså INGEN fyllning
    // alls: när `border` är satt ritas bara hårlinjen, annars ingenting.
    // Det är familjens grundsats tillämpad på en container — bakgrunden är
    // fönstrets hela vägen igenom, och gruppering görs med en linje och med
    // luft.
    //
    // De fyra tillstånden (princip 7): inget av dem finns. En yta har inget
    // läge — remsorna i drawAfterChildren har det.
    case UI::Minimalistic:
        // YTAN FYLLS — familjens övriga teman ger paneler en egen bakgrund,
        // och utan den flyter grupperna ihop med fönstret: en panel vore
        // bara sin kant, och en RAMLÖS panel vore ingenting alls.
        //
        // Det AVVIKER från familjens grundsats "ytor separeras med en
        // LINJE, inte med en fyllning" —
        // ett produktbeslut, inte ett förbiseende. Kvar av grundsatsen är
        // att linjen finns och att stegen är SMÅ: `panel` ligger ett enda
        // ljushetssteg från `windowBg` i alla nio paletter (0.995 mot
        // 0.960 ljust, 0.150 mot 0.105 mörkt), så ytan syns utan att bli
        // en platta.
        Draw::rect(c, c.bounds(), c.style->panelRadius,
                   c.theme->colors.panel);
        if (border_)
            Draw::frame(c, c.bounds(), c.style->panelRadius,
                        Pen{c.theme->colors.border, c.style->borderWidth});
        break;

    // GLAZE — faller igenom till Slate, och det är ett BESLUT.
    // Glazes yt-recept är "fylld yta + bestämd kant, fyrkantiga hörn" —
    // vilket är exakt Slates panel, ritad i Lavender-paletten. De två
    // avvikelser som kunde ha skilt dem föll båda:
    //
    //   RUNDADE HÖRN — radien läses ur
    //   `Style::panelRadius` och `clipRadius()` nedan rundar barnklippet
    //   koncentriskt. Att Glaze och Slate ändå ritar LIKA är alltså inte
    //   längre en spärr utan ett VAL — och värdet (0 i alla tre stilarna i
    //   dag) hör till stilen, inte hit.
    //   LAMINERING — en panel är ingen aktiv yta man kan påverka, och
    //   lamineringen betyder materialitet hos något man interagerar med.
    //   En laminerad panel hade gjort hela fönstret till en knapp.
    //
    // Att grenen står HÄR och inte bara underförstått i `default:` är
    // poängen: identisk ritning i två familjer ska vara ett skrivet beslut.
    case UI::Glaze:

    // SLATE — en YTA: `panel` + hårlinje `border`.
    //
    // RADIEN LÄSES UR `Style::panelRadius`. En panel med rundade hörn kräver
    // RUNDAT BARNKLIPP: annars läcker barnen ut över hörnen. `clipRadius()`
    // nedan rundar barnklippet koncentriskt mot just den här ytan, hela vägen
    // ner i shadern. ORDNINGEN ÄR POÄNGEN — klippet först, fältet sen.
    //
    // Värdet är 0 i alla tre stiltabellerna: förmågan finns, och vilken
    // radie som ser bra ut är stilens fråga.
    //
    // De fyra tillstånden (princip 7): inget av dem finns. En yta har inget
    // läge — remsorna i drawAfterChildren har det, och de är egna plattor.
    case UI::Slate:
    default:
        if (border_)   // utan ram är panelen osynlig — barnen är pixlarna
            Draw::rect(c, c.bounds(), c.style->panelRadius,
                       c.theme->colors.panel,
                       Pen{c.theme->colors.border, c.style->borderWidth});
        break;
    }
}

// Barnens omslutande utsträckning: resolve mot OSCROLLAD innehållsyta —
// samma formel som ritningen använder, bara utan offset.
gfx::Sizef Panel::contentExtent(const Ctx& c) const
{
    const gfx::Rectf inner = innerRect(c);
    Ctx cc  = c;
    cc.area = inner;
    float maxX = 0.0f, maxY = 0.0f;
    for (const auto& ch : children()) {
        if (!ch->isVisible())
            continue;
        const gfx::Rectf r = ch->resolve(cc);
        maxX = std::max(maxX, r.x + r.w - inner.x);
        maxY = std::max(maxY, r.y + r.h - inner.y);
    }
    return gfx::Sizef{maxX, maxY};
}

// Scrollens spelrum i LOGISKA px (0 = axeln svämmar inte över) — EN
// uträkning som scroll, hjul, remsor och klampning delar. Måttet är
// RULLNINGSYTANS (`scrollViewport`), som för en vanlig panel ÄR
// innehållsytan. Den projiceras till fysiska px FÖRST, precis som
// `innerRect` gör, så default-vägen räknar exakt samma flyttal som innan
// kroken fanns — `contentExtent` talar fysiskt.
gfx::Sizef Panel::scrollRange(const Ctx& c) const
{
    const gfx::Rectf vp  = Draw::project(c, resolve(c), scrollViewport(c));
    const gfx::Sizef ext = contentExtent(c);
    return gfx::Sizef{std::max(0.0f, (ext.w - vp.w) / c.scale),
                      std::max(0.0f, (ext.h - vp.h) / c.scale)};
}

gfx::Rectf Panel::scrollViewport(const Ctx& c) const
{
    return innerBounds(c);
}

gfx::Rectf Panel::childArea(const Ctx& c) const
{
    // Innehållsytan förskjuten med klampad scroll — det är HELA
    // scrollmekaniken: barnen resolvar mot en flyttad area.
    const gfx::Sizef range = scrollRange(c);
    gfx::Rectf       a     = innerRect(c);
    a.x -= std::clamp(scrollX_, 0.0f, range.w) * c.scale;
    a.y -= std::clamp(scrollY_, 0.0f, range.h) * c.scale;
    return a;
}

gfx::Rectf Panel::clipRect(const Ctx& c) const
{
    return innerRect(c);
}

// Innehållsytans radie i LOGISKA px — KONCENTRISK mot den yta `draw()`
// faktiskt ritar.
//
// Innehållsytan är panelens rect INSATT med `Style::panelInset` när ramen
// finns (och bara då — se `innerBounds`). Två koncentriska rundade
// rektanglar med avståndet d har radierna r och r − d, så innerformens
// radie är `panelRadius − panelInset`. Insetten är `panelInset` och INTE
// `borderWidth`: den senare är 0 = hårlinje i alla stilar och beskriver
// ramens TJOCKLEK, inte indraget.
float Panel::innerRadius(const Ctx& c) const
{
    const float inset = border_ ? c.style->panelInset : 0.0f;
    return std::max(0.0f, c.style->panelRadius - inset);
}

// Barnklippets radie i FYSISKA px — parhäst till `clipRect`, som också är
// det. Multiplikationen med `c.scale` är av samma slag som `childArea`s
// scrollomräkning: mätvärdet är logiskt, konsumenten fysisk.
float Panel::clipRadius(const Ctx& c) const
{
    return innerRadius(c) * c.scale;
}

void Panel::setScroll(float x, float y)
{
    if (x == scrollX_ && y == scrollY_)
        return;
    scrollX_ = std::max(0.0f, x);
    scrollY_ = std::max(0.0f, y);
    invalidate();   // panelens yta — EN damage-rect, aldrig per barn
}

bool Panel::mouseWheel(const Ctx& c, float, float, float wheelX, float wheelY)
{
    const gfx::Sizef range = scrollRange(c);
    if (range.w <= 0.0f && range.h <= 0.0f)
        return false;   // inget att scrolla — bubbla vidare

    // Hjul upp (positiv rad) = scrolla mot innehållets början.
    const float step = c.style->wheelStep;
    const float nx =
        std::clamp(scrollX_ - wheelScrollPx(c, wheelX, step), 0.0f, range.w);
    const float ny =
        std::clamp(scrollY_ - wheelScrollPx(c, wheelY, step), 0.0f, range.h);
    if (nx == scrollX_ && ny == scrollY_)
        return false;   // redan i kanten — låt en yttre panel ta hjulet

    scrollX_ = nx;
    scrollY_ = ny;
    invalidate();       // EN rect — panelens yta
    return true;
}

// AUTOSKROLLEN under ett drag — kontraktet står
// vid `Widget::dragScroll`. Zonen räknas mot RULLNINGSYTANS kant (under en
// tabells huvud), i logiska px som remsorna, och bara åt ett håll med
// spelrum kvar: en lista vid sitt slut svarar nej, och kedjan går vidare
// till en yttre panel. Scrollen läses KLAMPAD — det lagrade värdet kan ligga
// bortom spelrummet efter att innehållet krympt.
bool Panel::dragScroll(const Ctx& c, float x, float y, bool apply)
{
    const gfx::Sizef range = scrollRange(c);
    if (range.w <= 0.0f && range.h <= 0.0f)
        return false;
    const gfx::Rectf  vp = scrollViewport(c);
    const gfx::Pointf p  = localPoint(c, x, y);
    if (!vp.contains(p.x, p.y))
        return false;
    const float zon  = c.style->dragScrollZone;
    const float steg = c.style->dragScrollStep;
    const float sx   = std::clamp(scrollX_, 0.0f, range.w);
    const float sy   = std::clamp(scrollY_, 0.0f, range.h);
    float       dx   = 0.0f;
    float       dy   = 0.0f;
    if (range.h > 0.0f) {
        if (p.y < vp.y + zon && sy > 0.0f)
            dy = -steg;
        else if (p.y >= vp.y + vp.h - zon && sy < range.h)
            dy = steg;
    }
    if (range.w > 0.0f) {
        if (p.x < vp.x + zon && sx > 0.0f)
            dx = -steg;
        else if (p.x >= vp.x + vp.w - zon && sx < range.w)
            dx = steg;
    }
    if (dx == 0.0f && dy == 0.0f)
        return false;
    if (apply)
        setScroll(std::clamp(sx + dx, 0.0f, range.w),
                  std::clamp(sy + dy, 0.0f, range.h));
    return true;
}

// ------------------------------------------------- overlay-listerna

// Remsorna ligger i RULLNINGSYTANS kanter (LOGISKA px, som all annan
// geometri här — för en vanlig panel är det innehållsytan) och finns bara
// när axeln svämmar över; hörnet lämnas fritt när båda listerna syns.

gfx::Rectf Panel::track(const Ctx& c, bool vertical) const
{
    const gfx::Sizef range = scrollRange(c);
    if ((vertical ? range.h : range.w) <= 0.0f)
        return gfx::Rectf{};
    const gfx::Rectf in     = scrollViewport(c);
    const float      t      = c.style->scrollThick;
    const float      corner = (vertical ? range.w : range.h) > 0.0f ? t : 0.0f;
    return vertical
               ? gfx::Rectf{in.x + in.w - t, in.y, t, in.h - corner}
               : gfx::Rectf{in.x, in.y + in.h - t, in.w - corner, t};
}

// Axelns rullningsmått i LOGISKA px — ritningen OCH greppet läser dem.
// Sidan är det man SER av innehållet, alltså rullningsytans mått.
Draw::ScrollMetrics Panel::scrollMetrics(const Ctx& c, bool vertical) const
{
    const gfx::Sizef range = scrollRange(c);
    const gfx::Rectf in    = scrollViewport(c);
    const float      max   = vertical ? range.h : range.w;
    return Draw::ScrollMetrics{
        0.0f, max, std::clamp(vertical ? scrollY_ : scrollX_, 0.0f, max),
        vertical ? in.h : in.w, vertical};
}

void Panel::drawAfterChildren(const Ctx& c)
{
    if (!scrollbars_)
        return;

    // TUMMEN är samma pixlar som Scrollbar-widgetens — kontraktet är
    // "samma färg för samma TILLSTÅND", och rollerna byts i BÅDA filerna i
    // SAMMA ändring. HÄRLEDNINGEN får däremot skilja sig: overlayn har TVÅ
    // remsor och vet vilken som dras (dragAxis_), Scrollbar har en och läser
    // pressed() rakt av. Panels variant är den precisare, och båda behålls.
    //
    // INGEN RÄNNA här — och det är inte en glömska. Overlayn ritas EFTER
    // barnen och svävar över innehållet; en opak ränna hade täckt det som
    // står under. Den fristående listen äger sin remsa och får därför en.
    // (I Minimalistic sammanfaller de två av en annan orsak: DEN familjen
    // har ingen ränna någonstans.)
    for (const bool vertical : {true, false}) {
        const gfx::Rectf tr = track(c, vertical);
        if (tr.w <= 0.0f || tr.h <= 0.0f)
            continue;
        const int  axis = vertical ? 1 : 2;
        overlayThumb(c, tr, scrollMetrics(c, vertical),
                     dragAxis_ == axis && pressed(),
                     hotAxis_ == axis && hovered());
    }
}

// Overlay-tummen per RITNING. Egen metod och inte tre kopior av loopen
// ovan: remsornas geometri och greppet är gemensamma, det är MATERIALET som
// skiljer familjerna åt.
void Panel::overlayThumb(const Ctx& c, const gfx::Rectf& tr,
                         const Draw::ScrollMetrics& sm, bool down,
                         bool hot) const
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;

    switch (c.ui) {
    // GLAZE — accentytan, exakt som Scrollbar-widgetens tumme (kontraktet).
    // `button` mäter 1.76:1 mot panelens yta i Lavender och kan alltså inte
    // bära en tumme någon ser; `accentSurface` mäter 5.67:1 / 3.45:1.
    // Ingen laminering — en 6 px tumme rymmer ingen kontur, och laminering
    // utan kontur tvättar bort sin egen separation (2.91:1 mätt).
    //   vila `accentSurface` · hover …Hover · tryck …Press (per AXEL)
    case UI::Glaze:
        Draw::scrollThumbDraw(c, tr, sm, m.scrollPad, m.scrollThumbMin,
                              down  ? col.accentSurfacePress
                              : hot ? col.accentSurfaceHover
                                    : col.accentSurface);
        break;

    // MINIMALISTIC — bara tummen, i textfärgerna.
    //   vila `textDim` · hover/tryck `text`
    case UI::Minimalistic:
        Draw::scrollThumbDraw(c, tr, sm, m.scrollPad, m.scrollThumbMin,
                              down || hot ? col.text : col.textDim);
        break;

    // SLATE — `border`/`textDim`. Tummen är inte en platta i `button`-familjen:
    // `button` ÄR `panel` i varje genererad palett (`c.button = c.panel;`) och
    // en overlay-tumme i den färgen mäter exakt **1.00:1** mot panelens yta i
    // alla arton genererade teman — osynlig. Att paletten inte kan bära en
    // platta här är en PALETTfråga, och tills den avgörs är `border`/`textDim`
    // det minsta ingreppet. Hover och tryck ser därför lika ut.
    case UI::Slate:
    default:
        Draw::scrollThumbDraw(c, tr, sm, m.scrollPad, m.scrollThumbMin,
                              down || hot ? col.textDim : col.border);
        break;
    }
}

// Musen i widgetens EGEN logiska rymd — samma koordinater som ritningen,
// så remsorna aldrig får två uppsättningar mått.
gfx::Pointf Panel::localPoint(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r = resolve(c);
    return gfx::Pointf{Draw::localX(c, r, mx), Draw::localY(c, r, my)};
}

bool Panel::inScrollStrip(const Ctx& c, float mx, float my) const
{
    if (!scrollbars_)
        return false;
    const gfx::Pointf p = localPoint(c, mx, my);
    return track(c, true).contains(p.x, p.y) ||
           track(c, false).contains(p.x, p.y);
}

bool Panel::inRowColumn(const Ctx& c, float mx, float my) const
{
    if (inScrollStrip(c, mx, my))
        return false;
    const gfx::Rectf in = innerBounds(c);
    const float      x  = Draw::localX(c, resolve(c), mx);
    return x >= in.x && x <= in.x + in.w;
}

void Panel::mouseDown(const Ctx& c, float mx, float my)
{
    dragAxis_ = 0;
    if (!scrollbars_)
        return;
    const gfx::Pointf p = localPoint(c, mx, my);
    const auto&       m = *c.style;

    for (const bool vertical : {true, false}) {
        const gfx::Rectf tr = track(c, vertical);
        if (!tr.contains(p.x, p.y))
            continue;
        const Draw::ScrollMetrics sm = scrollMetrics(c, vertical);
        const gfx::Rectf th = Draw::scrollThumb(sm, tr, m.scrollPad,
                                                m.scrollThumbMin);
        const float pos   = vertical ? p.y : p.x;
        const float start = vertical ? th.y : th.x;
        if (th.contains(p.x, p.y)) {
            // Greppa tummen — dragOff_ är var i tummen greppet togs.
            dragAxis_ = vertical ? 1 : 2;
            dragOff_  = pos - start;
        } else {
            // Rännklick: en SIDA i klickets riktning.
            const float page = (pos < start ? -1.0f : 1.0f) * sm.page;
            setScroll(scrollX_ + (vertical ? 0.0f : page),
                      scrollY_ + (vertical ? page : 0.0f));
        }
        invalidate();
        return;
    }
}

void Panel::mouseDrag(const Ctx& c, float mx, float my)
{
    if (dragAxis_ == 0)
        return;
    const bool                vertical = dragAxis_ == 1;
    const auto&               m        = *c.style;
    const Draw::ScrollMetrics sm       = scrollMetrics(c, vertical);
    if (sm.max <= 0.0f)
        return;
    const gfx::Rectf  tr = track(c, vertical);
    const gfx::Rectf  th = Draw::scrollThumb(sm, tr, m.scrollPad,
                                             m.scrollThumbMin);
    const float free = (vertical ? tr.h - th.h : tr.w - th.w);
    if (free <= 0.0f)
        return;
    const gfx::Pointf p   = localPoint(c, mx, my);
    const float       pos = (vertical ? p.y : p.x) - dragOff_ -
                      (vertical ? tr.y : tr.x);
    const float v = std::clamp(pos / free, 0.0f, 1.0f) * sm.max;
    setScroll(vertical ? scrollX_ : v, vertical ? v : scrollY_);
}

void Panel::mouseMove(const Ctx& c, float mx, float my)
{
    if (!scrollbars_)
        return;
    // Bara ritkosmetik (het tumme) — invalidatera ENBART vid ändring
    // (hård regel 1: rörelse utan ändring ritar inget).
    const gfx::Pointf p = localPoint(c, mx, my);
    const int axis = track(c, true).contains(p.x, p.y)    ? 1
                     : track(c, false).contains(p.x, p.y) ? 2
                                                          : 0;
    if (axis != hotAxis_) {
        hotAxis_ = axis;
        invalidate();
    }
}

} // namespace glaze
