#pragma once
#include <algorithm>

#include "glaze/widgets/column_panel.h"   // ColumnSizing

// KOLUMNLÄGENAS RÄKNING — intern och header-only, delad av ColumnPanel och
// PropertyGrid, så att Manual, AutoMin och Autosize betyder exakt samma bredd i
// en tabell, ett träd och en egenskapslista. Ren aritmetik i LOGISKA px, inget
// tillstånd.

namespace glaze::columnsizing {

// KOLUMNENS BAS: Manual och Autosize är den satta bredden (≤ 0 = förvalet
// `defaultW`) — för Autosize dess golv —, AutoMin den anpassade bredden
// `fitted` (innehållet med luft, redan minst `columnMinW`).
inline float baseWidth(ColumnSizing sizing, float set, float defaultW,
                       float fitted)
{
    if (sizing == ColumnSizing::AutoMin)
        return fitted;
    return set > 0.0f ? set : defaultW;
}

// DET SOM BLIR ÖVER per Autosize-kolumn: innehållsbredden `cw` minus alla
// kolumners baser, delat LIKA mellan `autosizeCount` kolumner — aldrig
// negativt, och 0 utan någon Autosize-kolumn.
inline float autosizeShare(float cw, float sumOfBases, int autosizeCount)
{
    if (autosizeCount <= 0)
        return 0.0f;
    return std::max(0.0f, cw - sumOfBases) / float(autosizeCount);
}

// DEN EFFEKTIVA BREDDEN: basen, och för en Autosize-kolumn basen + andelen.
inline float effectiveWidth(ColumnSizing sizing, float base, float share)
{
    return sizing == ColumnSizing::Autosize ? base + share : base;
}

} // namespace glaze::columnsizing
