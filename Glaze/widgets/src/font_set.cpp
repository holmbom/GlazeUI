// FontSet — se font_set.h. En plats per (storlek, familj, stil), laddad LAT
// och omladdad när storleken × skalan ändrats — och de fria storlekarna
// bredvid, under ett tak.

#include "font_set.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <gfx/platform.h>

#include "glaze/draw.h"

namespace glaze {

namespace {

// TAKET för de fria storlekarna i ett fönster. Ett fält eller en lista har EN
// storlek, och ett fönster med fler än ett fåtal olika är ovanligt — taket
// finns för en app som byter storlek i drift (en zoom), där varje ny storlek
// annars hade blivit en atlas till för fönstrets livstid. Ett tal i den
// interna filen och inte i `Style`: det är ingen utseendefråga.
constexpr std::size_t kFriaMax = 8;

} // namespace

gfx::Font* FontSet::get(gfx::Renderer& r, const Style& s, float scale,
                        TextSize size, gfx::FontFamily family,
                        gfx::FontStyle style)
{
    const float storlek = size == TextSize::Heading      ? s.headingFontSize
                          : size == TextSize::Subheading ? s.subheadingFontSize
                                                         : s.uiFontSize;
    const float px = storlek * scale;
    Slot&       p  = slots_[static_cast<std::size_t>(size)]
                     [family == gfx::FontFamily::Mono ? 1 : 0]
                     [static_cast<std::size_t>(style) & 3u];
    if (p.px != px) {
        // En ny storlek — stilbyte, skalbyte eller en annan skärm. De byter
        // aldrig MITT i en bildruta (stilen och skalan är fönstrets under
        // hela ritningen), så den gamla fonten rivs i den första frågan EFTER
        // bytet, när ingen batch längre pekar på dess atlas — samma ögonblick
        // som `reloadFont` river rollfonten.
        p.font.reset();
        p.tried = false;
        p.px    = px;
    }
    if (!p.tried) {
        p.tried = true;
        std::unique_ptr<gfx::Font> f = r.loadFont(family, px, style);
        if (f && f->valid())
            p.font = std::move(f);
    }
    return p.font.get();
}

// En fri plats per (FYSISK px, familj, stil): samma nyckel ur två widgets är
// samma font. En font som inte går att ladda står kvar som en tom post —
// prövas inte igen i varje bildruta — tills `trim` släpper den.
gfx::Font* FontSet::sized(gfx::Renderer& r, float px, gfx::FontFamily family,
                          gfx::FontStyle style)
{
    for (Fri& f : fria_)
        if (f.px == px && f.family == family && f.style == style) {
            f.frågad = ram_;
            return f.font.get();
        }
    Fri ny;
    ny.px     = px;
    ny.family = family;
    ny.style  = style;
    ny.frågad = ram_;
    std::unique_ptr<gfx::Font> f = r.loadFont(family, px, style);
    if (f && f->valid())
        ny.font = std::move(f);
    fria_.push_back(std::move(ny));
    return fria_.back().font.get();
}

// Vid framens början pekar ingen batch på någon atlas, så varje fri storlek
// går att släppa här — de som frågats längst sedan, tills taket hålls.
void FontSet::trim()
{
    while (fria_.size() > kFriaMax)
        fria_.erase(std::min_element(
            fria_.begin(), fria_.end(),
            [](const Fri& a, const Fri& b) { return a.frågad < b.frågad; }));
    ++ram_;
}

std::optional<Length> textSizeValue(const Length& size, const char* typ,
                                    const std::string& namn)
{
    if (size.unit == Unit::World || !std::isfinite(size.value) ||
        size.value < 0.0f) {
        gfx::log("%s(%s): setTextSize(%g %s) — ett mått ≥ 0 i px eller mm "
                 "(0 = stilens); ignoreras",
                 typ, namn.c_str(), double(size.value),
                 size.unit == Unit::World ? "världsenheter"
                 : size.unit == Unit::Mm  ? "mm"
                                          : "px");
        return std::nullopt;
    }
    if (size.value == 0.0f)
        return Length{};
    return size;
}

// Fältens textstorlek är `Draw::withFont` i UI/Regular — EN regel för
// storleken, enheten och reserven.
Ctx sizedText(const Ctx& c, const Length& size)
{
    return Draw::withFont(c, gfx::FontFamily::UI, gfx::FontStyle::Regular, size);
}

} // namespace glaze
