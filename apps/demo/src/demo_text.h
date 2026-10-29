#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include <glaze/glaze.h>

#include "demo_strings.h"

// DEMONS TEXTHJÄLPARE.
//
// `glaze::tr(id).str()` är ett KOMPILERINGSFEL på en TEMPORÄR — den
// `&&`-raderade överlagringen i `TrText` fångar precis den dinglande
// referensen — så en `std::string` ur ett id måste gå via ett NAMNGIVET
// objekt. Widgetarna behöver aldrig det (de lagrar `TrText` och löser upp
// själva); det här är för de tre ställen där appen bygger ihop en sträng
// SJÄLV: listornas items, sammansatta statusrader och prefix + värde.
//
// Fri funktion i GLOBAL namnrymd med flit: demon är en APP och inte ett
// bibliotek, och `demo::trs(demo::str::X)` hade varit brus på varje rad.
inline std::string trs(std::uint32_t id)
{
    const glaze::TrText t = glaze::tr(id);
    return t.str();
}

// Ett tal med SPRÅKETS decimaltecken (`glaze::str::DecimalSeparator`) — det
// talfälten visar: "7,43" på svenska, "7.43" på engelska. Delas av ritytans
// flikar (gallery_canvas.cpp, gallery_3d.cpp).
inline std::string tal(double v, int decimaler)
{
    if (std::abs(v) < 0.5 * std::pow(10.0, -decimaler))
        v = 0.0;   // inget "-0,00"
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimaler, v);
    std::string t = buf;
    if (const auto p = t.find('.'); p != std::string::npos)
        t.replace(p, 1, trs(glaze::str::DecimalSeparator));
    return t;
}
