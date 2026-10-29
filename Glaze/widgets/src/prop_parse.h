#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "glaze/assets.h"   // AssetHandle/img — parseImg nedan
#include "glaze/draw.h"     // Align/VAlign — parseAlign/parseVAlign nedan
#include "glaze/widget.h"

// Delade parse-hjälpare för setProp-egenskapsvärden (nyckel/värde-text) —
// används av widgetarnas setProp-implementationer (basen + subklasserna).
// Internt i libglaze; egna widgets parsar sina egna värden själva.

namespace glaze::propparse {

inline std::vector<std::string> splitComma(const std::string& s)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t p = s.find(',', start);
        out.push_back(s.substr(start, p - start));
        if (p == std::string::npos)
            break;
        start = p + 1;
    }
    return out;
}

// "left"/"center"/"right" → Align. Namnen är enumets egna, gemena (den
// form setProp-vägen levererar: `parseEnumTail` gemenar sista segmentet).
// Delad av `Label` och `TextInput` — två widgets som parsar
// samma vokabulär är två chanser att glida isär.
inline bool parseAlign(const std::string& v, Align& out)
{
    if (v == "left")   { out = Align::Left;   return true; }
    if (v == "center") { out = Align::Center; return true; }
    if (v == "right")  { out = Align::Right;  return true; }
    return false;
}

// "top"/"center"/"bottom" → VAlign (tabellens `headerVAlign`).
// Samma gemena form som `parseAlign`, och här bredvid av samma skäl.
inline bool parseVAlign(const std::string& v, VAlign& out)
{
    if (v == "top")    { out = VAlign::Top;    return true; }
    if (v == "center") { out = VAlign::Center; return true; }
    if (v == "bottom") { out = VAlign::Bottom; return true; }
    return false;
}

inline bool parseBool(const std::string& v)
{
    return v == "yes" || v == "true" || v == "1";
}

// "lista/nyckel" → bildhandtaget, samma par som `glaze::img`. Delad av de
// bildbärande widgetarna så att ingen har en egen kopia — samma skäl som
// `parseAlign`.
//
// false = inget snedstreck, alltså inte ens formen (setProp svarar då
// false och raden blir opak). En MISS i assets är däremot inget formfel:
// `img` loggar den första gången och ger ett OGILTIGT handtag, vilket är
// widgetens vanliga "ingen bild".
inline bool parseImg(const std::string& v, AssetHandle& out)
{
    const size_t slash = v.find('/');
    if (slash == std::string::npos)
        return false;
    out = glaze::img(v.substr(0, slash), v.substr(slash + 1));
    return true;
}

inline bool parseSize(const std::string& v, float& w, float& h)
{
    return std::sscanf(v.c_str(), "%fx%f", &w, &h) == 2;
}

inline bool parseDock(const std::string& v, Dock& d)
{
    if      (v == "none")   d = Dock::None;
    else if (v == "left")   d = Dock::Left;
    else if (v == "top")    d = Dock::Top;
    else if (v == "right")  d = Dock::Right;
    else if (v == "bottom") d = Dock::Bottom;
    else if (v == "fill")   d = Dock::Fill;
    else return false;
    return true;
}

// Kommalista av kanter ("left,top", "left,right,top"); "none" = inga
// ankare (centrerad + offset). Ogiltig kant = false, a lämnas orörd.
inline bool parseAnchors(const std::string& v, Anchors& a)
{
    Anchors r{false, false, false, false};
    if (v != "none") {
        for (const auto& part : splitComma(v)) {
            if      (part == "left")   r.left   = true;
            else if (part == "top")    r.top    = true;
            else if (part == "right")  r.right  = true;
            else if (part == "bottom") r.bottom = true;
            else return false;
        }
    }
    a = r;
    return true;
}

// SYSKONANKARET — setProp-textformen:
// "<namn>" (gap 0) eller "<namn>,<gap>"; "none" och tom sträng NOLLSTÄLLER.
// STRIKT som `parseImgCall`: ett ogiltigt tal eller ett tomt namn är false
// och lämnar `a` orörd — aldrig en gissning.
//
// OBS att `.glazeui`-formen är en annan: där står raden som en
// C++-STRUCTLITERAL (`knapp8.anchorLeft = {"knapp7", 8};`) och avkodas av
// `bld::parseSiblingAnchorText` innan den sätts DIREKT på fältet, precis
// som `anchors`. Textformen här är för handskriven `setProp`-användning
// och för egna widgets som vidarebefordrar till basen.
inline bool parseSiblingAnchor(const std::string& v, SiblingAnchor& a)
{
    if (v.empty() || v == "none") {
        a = SiblingAnchor{};
        return true;
    }
    const size_t comma = v.find(',');
    const std::string namn = v.substr(0, comma);
    if (namn.empty())
        return false;
    float gap = 0.0f;
    if (comma != std::string::npos) {
        const std::string tal = v.substr(comma + 1);
        if (tal.empty())
            return false;
        char* end = nullptr;
        gap = std::strtof(tal.c_str(), &end);
        if (!end || *end != '\0')
            return false;
    }
    a.to  = namn;
    a.gap = gap;
    return true;
}

// ETT MÅTT ur en C++-LITERAL — den form `.glazeui` bär
// (`split.setPaneSize(220);`, `220.5f`, `.5`, `1e3`). STRIKT: skräp efter
// talet (`12abc`), komma (aldrig ett decimaltecken i C++), hex och icke
// ändliga tal avvisas, och tolkningen sker i "C"-lokalen så att appens
// `setlocale` aldrig ändrar vad ett tal betyder. Samma regler som
// talfältets `tolkaLiteral` (spin_box.cpp) — den lokala kopian där kan
// flytta hit; den här är delarens och dockningspanelens.
inline bool parseDouble(const std::string& in, double& out)
{
    std::string s = in;
    const auto  a = s.find_first_not_of(" \t");
    const auto  b = s.find_last_not_of(" \t");
    s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    if (!s.empty() && (s.back() == 'f' || s.back() == 'F' ||
                       s.back() == 'l' || s.back() == 'L'))
        s.pop_back();
    if (s.empty() || s.find_first_of("xX',") != std::string::npos)
        return false;
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    is >> v;
    if (is.fail() || !std::isfinite(v))
        return false;
    is >> std::ws;
    if (!is.eof())
        return false;
    out = v;
    return true;
}

// ETT HELTAL ur en C++-literal — `parseDouble`s regler, sedan ett HELT tal
// inom `int` (timerns och systemfältets `interval`, tabellens radantal).
// Skräp är false och aldrig 0 — `atoi`s "oparsbart ⇒ 0" hade gjort det till
// ett värde ingen skrev. `false` lämnar `out` orörd.
inline bool parseInt(const std::string& in, int& out)
{
    double v = 0.0;
    if (!parseDouble(in, v) || v != std::floor(v) ||
        v > static_cast<double>(std::numeric_limits<int>::max()) ||
        v < static_cast<double>(std::numeric_limits<int>::min()))
        return false;
    out = static_cast<int>(v);
    return true;
}

// Samma tal som float — det de flesta egenskaperna är. (Ritytans världs-
// koordinater tar `parseDouble`: en SWEREF-koordinat får inte avrundas.)
inline bool parseNumber(const std::string& in, float& out)
{
    double v = 0.0;
    if (!parseDouble(in, v))
        return false;
    out = static_cast<float>(v);
    return true;
}

// "x, y" — två tal ur ett setter-anrops argumentlista (`setCenter(674000,
// 6580000)` når `setProp("center", "674000, 6580000")` genom builderns
// setter-väg). Exakt två, båda strikta.
inline bool parseDoublePair(const std::string& v, double& a, double& b)
{
    const std::vector<std::string> d = splitComma(v);
    return d.size() == 2 && parseDouble(d[0], a) && parseDouble(d[1], b);
}

// FÄRGEN SOM TEXT — `#RRGGBB`, formen färgväljarens hexfält visar,
// `ColorPicker::setColor("#3366FF")` tar och `Label`s `color`-nyckel alltid
// tagit. Läsningen och skrivningen står HÄR tillsammans: två widgets som
// parsar samma vokabulär är två chanser att glida isär (`parseAlign`s skäl).
//
// STRIKT: valfritt `#`, sedan EXAKT sex hexsiffror (versaler eller gemener)
// och ingenting annat — inga blanksteg (den som läser användarens text
// trimmar själv), ingen kortform (`#36F` hade gjort en halvskriven färg
// giltig mitt i skrivningen) och ingen alfa (färgen är opak). Värdet blir
// opakt. `false` lämnar `ut` orörd.
//
// ALFAN är opt-in per anropare: med `alfa` tas
// också `#RRGGBBAA` — åtta siffror, alfan sist (CSS:s ordning) — och sex
// siffror är fortfarande opakt. Bara färgväljaren frågar så; `Label`s
// `color` och ritytans färger förblir sex siffror.
inline bool parseHexColor(std::string_view v, gfx::Color& ut,
                          bool alfa = false)
{
    if (!v.empty() && v.front() == '#')
        v.remove_prefix(1);
    if (v.size() != 6 && !(alfa && v.size() == 8))
        return false;
    auto siffra = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    const std::size_t kanaler = v.size() / 2;   // 3, eller 4 med alfan
    std::uint8_t kanal[4] = {0, 0, 0, 255};
    for (std::size_t i = 0; i < kanaler; ++i) {
        const int hög = siffra(v[2 * i]);
        const int låg = siffra(v[2 * i + 1]);
        if (hög < 0 || låg < 0)
            return false;
        kanal[i] = static_cast<std::uint8_t>(hög * 16 + låg);
    }
    ut   = gfx::Color{};
    ut.r = kanal[0];
    ut.g = kanal[1];
    ut.b = kanal[2];
    ut.a = kanal[3];
    return true;
}

// Den KANONISKA textformen: `#` och sex VERSALA hexsiffror. Alfan skrivs
// inte — formen är opak, som `parseHexColor` — utom med `alfa`, då den står
// sist som två siffror till (`#RRGGBBAA`).
inline std::string hexColorText(gfx::Color c, bool alfa = false)
{
    char buf[12];
    if (alfa)
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", unsigned(c.r),
                      unsigned(c.g), unsigned(c.b), unsigned(c.a));
    else
        std::snprintf(buf, sizeof buf, "#%02X%02X%02X", unsigned(c.r),
                      unsigned(c.g), unsigned(c.b));
    return buf;
}

} // namespace glaze::propparse
