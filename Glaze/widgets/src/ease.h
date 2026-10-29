#pragma once
#include <algorithm>
#include <cmath>

// INTERN header (prejudikatet är `sash.h` och `prop_parse.h` i samma mapp) —
// GLIDNINGARNAS EN KURVA: kubisk in–ut. Ritytans kameraglidning
// (`canvas/camera_math.cpp`) och dockningens in- och utglidning
// (`widgets/dock_host.cpp`) går längs samma kurva, så två saker
// som glider i samma fönster gör det likadant.

namespace glaze::detail {

// `t` är förloppets andel (klampad till 0–1), svaret vägens andel.
inline double easeInOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

} // namespace glaze::detail
