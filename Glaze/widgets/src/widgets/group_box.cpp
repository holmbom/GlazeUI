// GroupBox — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: en yta, en ram med en lucka och en rubrik i luckan. Måtten kommer
// ur stilen (`group*`), färgerna ur temat — inga tal och inga färger i den
// här filen. Geometrin bor i tre privata metoder som ritningen OCH
// containerkrokarna läser, så det man ser och det barnen placeras mot
// aldrig kan glida isär.

#include "glaze/widgets/group_box.h"

#include <algorithm>

#include "glaze/draw.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("groupbox", GroupBox)

bool GroupBox::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ---------------------------------------------------------- geometrin

// Ramen börjar mitt i rubrikbandet — linjen går GENOM rubriken, det är
// vad "rubrik i kanten" betyder. Läget beror inte på om det finns en
// rubrik: att tömma texten ska aldrig flytta innehållet.
gfx::Rectf GroupBox::frameRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float band = Draw::lineHeight(c);
    return gfx::Rectf{b.x, b.y + band / 2.0f, b.w,
                      std::max(0.0f, b.h - band / 2.0f)};
}

// LUCKAN: rubriken plus `groupTitleGap` på båda sidor, `groupTitleIndent`
// in från vänsterkanten, över hela rubrikbandet. Tom rubrik = noll bredd =
// ingen lucka. Kapad mot ramens högra del, så en rubrik som är längre än
// ramen aldrig bryter hörnbågen på andra sidan.
gfx::Rectf GroupBox::titleRect(const Ctx& c, const gfx::Rectf& b) const
{
    const auto& m = *c.style;
    const float x = b.x + m.groupTitleIndent;
    if (text().empty())
        return gfx::Rectf{x, b.y, 0.0f, Draw::lineHeight(c)};
    const float bredd = Draw::measure(c, text()).w + 2.0f * m.groupTitleGap;
    const float maxB  = std::max(0.0f, b.x + b.w - m.groupTitleIndent - x);
    return gfx::Rectf{x, b.y, std::min(bredd, maxB), Draw::lineHeight(c)};
}

// LUFTEN — EN gång för barnens yta, autostorleken och höjden för en bredd:
// `groupPad` på sidorna och i botten, och HELA rubrikbandet plus `groupPad`
// upptill — innehåll får aldrig krypa upp i rubriken.
std::optional<Widget::Insets> GroupBox::childInsets(const Ctx& c) const
{
    const float pad = c.style->groupPad;
    return Insets{pad, Draw::lineHeight(c) + pad, pad, pad};
}

// Barnens yta: ytan minus luften.
gfx::Rectf GroupBox::innerRect(const Ctx& c, const gfx::Rectf& b) const
{
    const Insets in = *GroupBox::childInsets(c);
    return gfx::Rectf{b.x + in.left, b.y + in.top,
                      std::max(0.0f, b.w - in.left - in.right),
                      std::max(0.0f, b.h - in.top - in.bottom)};
}

// ---------------------------------------------------- containerkrokarna
// Samma innerYTA som ritningen räknar i, projicerad till FYSISKA px —
// `Draw::bounds(c, resolve(c))` är `c.bounds()` utanför draw().

gfx::Rectf GroupBox::childArea(const Ctx& c) const
{
    const gfx::Rectf r = resolve(c);
    return Draw::project(c, r, innerRect(c, Draw::bounds(c, r)));
}

gfx::Rectf GroupBox::clipRect(const Ctx& c) const
{
    return childArea(c);
}

// Klippets radie, KONCENTRISK mot ramen: två koncentriska rundade
// rektanglar med avståndet d har radierna r och r − d (Panel::innerRadius
// är prejudikatet). Med stilarnas nuvarande tal är `groupPad` ≥ `groupRadius` i alla
// tre stilarna, så klippet blir rakt — men formeln står kvar, för det är
// den och inte talen som är kontraktet.
float GroupBox::clipRadius(const Ctx& c) const
{
    return std::max(0.0f, c.style->groupRadius - c.style->groupPad) * c.scale;
}

gfx::Sizef GroupBox::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN med AUTOSTORLEK UR BARNEN — basens, ur luften
        // (`childInsets`): en satt axel vinner, en AUTO-axel blir barnens
        // utsträckning plus luften, och med en satt `width` följer höjden
        // innerbredden på `heightForWidth`s väg. En auto-bredd är dessutom
        // minst så bred som rubriken med sin lucka och indraget på båda
        // sidor. Ett tomt auto-mått blir därmed aldrig noll: rubrikbandet och
        // luften finns alltid.
        gfx::Sizef s = Widget::contentSize(c);
        if (width <= 0.0f && !text().empty()) {
            const auto& m = *c.style;
            s.w = std::max(s.w, Draw::measure(c, text()).w +
                                    2.0f * (m.groupTitleIndent + m.groupTitleGap));
        }
        return s;
    }
    }
}

void GroupBox::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf f   = frameRect(c, b);
    const gfx::Rectf t   = titleRect(c, b);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;

    switch (c.ui) {
    // GLAZE och MINIMALISTIC ritar som SLATE, och det är ett BESLUT
    // gruppramen är en YTA, och ytrecepten i alla tre
    // familjerna är redan samma sak för en ramad Panel: fyllningen `panel`
    // och hårlinjen `border` (Glazes "bestämd kant" gäller kontroller —
    // Panel faller igenom till Slate av samma skäl; Minimalistic fyller
    // sina paneler, och dess kant ÄR redan
    // en linje). En gruppram som ritades annorlunda än en ramad panel i
    // samma familj hade sett ut som en tredje sorts yta. Det som skiljer
    // familjerna är MÅTTEN (radie, luft, indrag — `group*` i stilen) och
    // FÄRGERNA, och båda axlarna är redan bytta.
    case UI::Glaze:
    case UI::Minimalistic:

    // SLATE — en YTA med rubriken I KANTEN.
    //
    // De fyra tillstånden (princip 7): bara ett finns. En gruppram har
    // varken hover, tryck eller fokus — den bär saker som har det. INAKTIV
    // dimmar rubriken (`textColor`); ytan och ramen står kvar, som på en
    // inaktiv Panel, och barnen släcks av appen (enabled kaskadar inte).
    case UI::Slate:
    default:
        Draw::rect(c, f, m.groupRadius, col.panel);
        Draw::frameGap(c, f, m.groupRadius, Pen{col.border, m.borderWidth},
                       t.x, t.w);
        Draw::text(c, Draw::inset(t, m.groupTitleGap), text(),
                   textColor(c));
        break;
    }
}

} // namespace glaze
