// Teckenbredden — se text_width.h. Tre binärsökningar i den genererade
// tabellen; ASCII svarar utan att söka.

#include "glaze_terminal/text_width.h"

#include <algorithm>
#include <iterator>

#include "text_width_table.h"

namespace glaze::term {
namespace {

template <std::size_t N>
bool inTable(const detail::WidthRange (&t)[N], char32_t cp)
{
    const auto* it = std::upper_bound(
        std::begin(t), std::end(t), cp,
        [](char32_t v, const detail::WidthRange& r) { return v < r.first; });
    return it != std::begin(t) && cp <= std::prev(it)->last;
}

} // namespace

int cellWidth(char32_t cp)
{
    // Under de kombinerande tecknen (U+0300): en cell. Det enda Cf där, det
    // mjuka bindestrecket U+00AD, syns som i xterm och glibc:s wcwidth.
    if (cp < 0x300)
        return 1;
    if (inTable(detail::kZero, cp))
        return 0;
    return inTable(detail::kWide, cp) ? 2 : 1;
}

bool isPictographic(char32_t cp)
{
    return cp >= 0xA9 && inTable(detail::kPictographic, cp);
}

} // namespace glaze::term
