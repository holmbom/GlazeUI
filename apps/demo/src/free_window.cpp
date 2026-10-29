#include "free_window.h"

FreeWindow::FreeWindow()
    : glaze::AppWindow(glaze::tr(demo::str::FrittFonster), 260, 200)
{
    setTransparent(true);   // fri geometri — före show()

    // Widgets fungerar som vanligt ovanpå den fria formen.
    auto* stang    = add<glaze::Button>(glaze::tr(demo::str::Stang));
    stang->width   = 90;
    stang->height  = 28;
    stang->anchors = {false, false, false, false};   // inga ankare = centrerad
    stang->onClick = [this](glaze::Widget*, glaze::ClickArgs&) { close(); };
}

void FreeWindow::frame(const glaze::Ctx& c)
{
    // Rund platta (cirkel = rundad rekt med radie = halva sidan) — allt
    // utanför plattan förblir genomskinligt: skrivbordet syns, och klick
    // där går igenom till fönstret under (D3D11).
    const float d = 160.0f * c.scale;
    const gfx::Rectf platta{c.area.x + (c.area.w - d) / 2.0f,
                            c.area.y + (c.area.h - d) / 2.0f, d, d};
    c.r->fillRoundedRect(platta, d / 2.0f, c.theme->colors.accent);

    // Texten sticker ut utanför formen — det är hela poängen.
    c.r->drawText(*c.font, c.area.x + 4.0f * c.scale,
                  platta.y - c.font->height() - 2.0f * c.scale,
                  trs(demo::str::FriGeometri), c.theme->colors.text);
}
