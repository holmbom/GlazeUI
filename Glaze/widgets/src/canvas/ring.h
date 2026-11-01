#pragma once
#include <cstddef>
#include <span>

// INTERN — PUNKTEN I EN RING, en regel för träffprovet (hit_index.cpp) och
// klippets prov på processorn (clip.cpp).

namespace glaze::detail {

// Ligger (x, y) i ringen `r`, med jämn–udda-regeln? `P` har `x` och `y`;
// ringens slutpunkt behöver inte upprepa den första. En ring med färre än
// tre punkter rymmer ingenting. Över flera ringar (en yta med hål) är svaret
// deras udda antal — `in ^= inRing(…)` per ring.
template <class P>
bool inRing(std::span<const P> r, double x, double y)
{
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const P& a = r[i];
        const P& b = r[j];
        if ((a.y > y) != (b.y > y) &&
            x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

} // namespace glaze::detail
