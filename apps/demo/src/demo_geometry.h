#pragma once
#include <cmath>
#include <numbers>
#include <vector>

#include <glaze/glaze.h>

// En VÅGIG RING kring (cx, cy): `n` punkter med radien r · (1 + tand ·
// cos 7a), lagda i världen genom `till` (kartans eller scenens koordinater
// från mitten). Ritytans flikar bygger sina klippytor med den.
namespace demo {

template <class Till>
std::vector<glaze::Point> vagRing(double cx, double cy, double r, double tand,
                                  int n, Till till)
{
    std::vector<glaze::Point> p;
    p.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double a = i * 2.0 * std::numbers::pi / n;
        const double q = r * (1.0 + tand * std::cos(7.0 * a));
        p.push_back(till(cx + q * std::cos(a), cy + q * std::sin(a)));
    }
    return p;
}

} // namespace demo
