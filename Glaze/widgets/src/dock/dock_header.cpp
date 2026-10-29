// dock_header — se dock_header.h. Ritningen följer widget-filernas regler fullt
// ut (hård regel 3): inga tal och inga färger här, måtten ur `c.style`
// (`dock*`, `cornerRadius`), färgerna ur `c.theme->colors`, ritandet ur `Draw`.

#include "dock_header.h"

#include "glaze/draw.h"

namespace glaze::detail {

namespace {

bool exists(const HeaderButton& k)
{
    return k.rect.w > 0.0f && k.rect.h > 0.0f;
}

// Knappen står TÄND eller NEDTRYCKT — den får en platta.
bool lit(const HeaderButton& k)
{
    return exists(k) && (k.hot || k.down);
}

// Tecknet: en `dockGlyph`-stor ruta, centrerad i knappens kvadrat.
void drawGlyph(const Ctx& c, const HeaderButton& k, const Pen& pen)
{
    const float      g = c.style->dockGlyph;
    const gfx::Rectf r{k.rect.x + (k.rect.w - g) / 2.0f,
                       k.rect.y + (k.rect.h - g) / 2.0f, g, g};
    switch (k.glyph) {
    case HeaderButton::Glyph::Menu:
        Draw::chevron(c, r, Dir::Down, pen);
        break;
    case HeaderButton::Glyph::Pinned:
        Draw::icon(c, r, Icon::Pinned, pen);
        break;
    case HeaderButton::Glyph::Unpinned:
        Draw::icon(c, r, Icon::Unpinned, pen);
        break;
    case HeaderButton::Glyph::Close:
        Draw::cross(c, r, pen);
        break;
    }
}

} // namespace

gfx::Color panelGlyphColor(const Ctx& c, bool enabled)
{
    return enabled ? c.theme->colors.textDim : c.theme->colors.border;
}

// De fyra tillstånden — knapparna följer VERKTYGSKNAPPENS recept
// i varje familj (tool_button.cpp): en rubrikknapp ÄR en platt knapp i en
// remsa, och två sorters platta knappar i samma fönster hade varit en för
// mycket.
//   vila       yta `panel`, rubrikrad + hårlinje, knapptecken `textDim`,
//              ingen platta
//   hover      plattan `buttonHover` (familjens form) och tecknet i `text`
//   tryck      plattan `buttonPress` (också ▾ medan menyn är öppen)
//   inaktiv    rubrik och tecken dimmas (`textDim`/`border`), inga plattor
void drawPanelHeader(const Ctx& c, const PanelHeader& p,
                     const std::string& text)
{
    const gfx::Rectf& b   = p.bounds;
    const gfx::Rectf& h   = p.header;
    const auto&       m   = *c.style;
    const auto&       col = c.theme->colors;

    // Den TÄNDA knappens platta, i knappens kvadrat minus luften.
    auto platta = [&](const HeaderButton& k) {
        return Draw::inset(k.rect, m.dockButtonInset);
    };
    auto fyll = [&](const HeaderButton& k) {
        return k.down ? col.buttonPress : col.buttonHover;
    };
    auto linjeUnder = [&](gfx::Color färg) {
        if (p.line)
            Draw::line(c, b.x, h.y + h.h, b.x + b.w, h.y + h.h,
                       Pen{färg, m.borderWidth});
    };

    switch (c.ui) {
    // GLAZE — en YTA med BESTÄMD KANT: panelen ramas
    // in i fönsterkantens roll `frame` (den står mot ett skikt eller en
    // fönsterkant, inte mot en kontroll — fritt stående eller dockad) och
    // rubrikraden är `windowBg` mot `panel`. Den tända plattan LAMINERAS och
    // får kontrollkanten, som en tänd verktygsknapp.
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::rect(c, h, 0.0f, col.windowBg);
        linjeUnder(col.frame);
        for (const HeaderButton& k : p.buttons) {
            if (!lit(k))
                continue;
            Draw::rect(c, platta(k), m.cornerRadius, fyll(k));
            if (k.hot)
                Draw::laminate(c, platta(k), m.cornerRadius, col.sheen,
                               m.sheenLower, col.sheenHigh, m.sheenUpper);
            Draw::frame(c, platta(k), m.cornerRadius,
                        Pen{col.buttonBorder, m.borderWidth});
        }
        Draw::frame(c, b, 0.0f, Pen{col.frame, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline": ytan är
    // `panel` (familjen fyller sina paneler), rubrikraden
    // får INGEN egen fyllning — bara hårlinjen under — och den tända plattan
    // är fyllningen innanför en kant: `buttonBorder` vid hover, `accent` vid
    // tryck (verktygsknappens recept). En fritt stående panel får dessutom
    // kanten `border` runt om.
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.panel);
        linjeUnder(col.border);
        for (const HeaderButton& k : p.buttons)
            if (lit(k))
                Draw::rect(c, platta(k), m.cornerRadius, fyll(k),
                           Pen{k.down ? col.accent : col.buttonBorder,
                               m.borderWidth});
        if (p.freestanding)
            Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;

    // SLATE — en YTA: `panel` med rubrikraden i
    // `windowBg` och en hårlinje i kontrollkanten under den; den tända
    // plattan är verktygsknappens, utan kontur. En fritt stående panel står
    // på en annan yta och får fältets kant runt om.
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::rect(c, h, 0.0f, col.windowBg);
        linjeUnder(col.border);
        for (const HeaderButton& k : p.buttons)
            if (lit(k))
                Draw::rect(c, platta(k), m.cornerRadius, fyll(k));
        if (p.freestanding)
            Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;
    }

    Draw::clip(c, p.title);
    Draw::text(c, p.title, text, p.titleColor);
    Draw::endClip(c);

    // Tecknen i vilofärgen, och ett TÄNT knapptecken ritas om i full
    // textfärg ovanpå — klippt till knappens ruta.
    const Pen vila{panelGlyphColor(c, p.enabled), m.borderWidth};
    for (const HeaderButton& k : p.buttons)
        if (exists(k))
            drawGlyph(c, k, vila);
    for (const HeaderButton& k : p.buttons) {
        if (!lit(k))
            continue;
        Draw::clip(c, k.rect);
        drawGlyph(c, k, Pen{col.text, m.borderWidth});
        Draw::endClip(c);
    }
}

} // namespace glaze::detail
