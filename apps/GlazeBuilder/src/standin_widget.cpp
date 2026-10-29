#include "standin_widget.h"

namespace {

// Minsta synliga yta när storleken inte satts (logiska px) — en osatt
// egen widget ska ändå gå att se och markera på canvasen.
constexpr float kMinW = 100.0f;
constexpr float kMinH = 40.0f;

} // namespace

// LOGISKA px (mätkontraktet) — konstanterna ovan är redan
// logiska, så svaret går rakt igenom. `c` är oanvänd och det är rätt:
// stand-inen har inget att fråga kontexten om.
gfx::Sizef StandInWidget::contentSize(const glaze::Ctx&) const
{
    const float w = width  > 0.0f ? width  : kMinW;
    const float h = height > 0.0f ? height : kMinH;
    return gfx::Sizef{w, h};
}

void StandInWidget::draw(const glaze::Ctx& c)
{
    const gfx::Rectf r   = resolve(c);
    const auto&      col = c.theme->colors;

    // Ruta med ram + diagonaler ("plats-hållare") och klassnamnet i
    // mitten — allt ur temats färger, texten via c.font (regel 2/3).
    c.r->fillRect(r, col.panel);
    c.r->strokeRect(r, col.border);
    c.r->line(r.x, r.y, r.x + r.w, r.y + r.h, col.border);
    c.r->line(r.x + r.w, r.y, r.x, r.y + r.h, col.border);

    const gfx::Sizef m = c.font->measure(typeName_);
    c.r->drawText(*c.font, r.x + (r.w - m.w) / 2.0f,
                  r.y + (r.h - m.h) / 2.0f, typeName_, col.textDim);
}

namespace bld {

std::unique_ptr<glaze::Widget> makeNodeWidget(const std::string& cppType,
                                              bool* standIn)
{
    std::unique_ptr<glaze::Widget> w = glaze::makeWidgetOfClass(cppType);
    if (standIn)
        *standIn = !w;
    if (!w)
        w = std::make_unique<StandInWidget>(cppType);
    return w;
}

} // namespace bld
