#pragma once
#include <string>
#include <vector>

#include "glaze/widget.h"

// INTERN header (`sash.h`-prejudikatet) — DOCKNINGENS LAYOUTSTRÄNG, version 2
// Parsern är REN STD: den ger en neutral
// `LayoutSpec` med NAMN, aldrig paneler. Skrivaren (`DockHost::layout`) och
// tillämpningen (`DockHost::setLayout`) bor i dock_string.cpp — de är
// medlemmar i värden, för de rör panelernas privata tillstånd.
//
// GRAMMATIKEN:
//
//     layout := "v2" ( ";" post )*
//     post   := strip | cell | panel | float | tool | ftool
//     strip  := "s=" kant
//     cell   := "c=" tal                       ; vikt, > 0 och ändlig
//     panel  := "p=" namn "," f "," f "," f "," f "," tal "," tal
//                   ; aktiv, infälld, nålad, stängd (f = 0|1),
//                   ; bredd, höjd (logiska px, >= 0 och ändliga, 0 = AUTO)
//     float  := "f=" namn "," pos "," pos "," tal "," tal "," f "," dock
//                   "," tal "," tal
//                   ; en FLYTANDE panel: fönstrets x, y och
//                   ; innehållsyta b, h (logiska px, `AppWindow::setLocation`s
//                   ; rymd; b, h >= 0, 0 = panelens föredragna mått), stängd,
//                   ; dockningskanten den dockas tillbaka till, och panelens
//                   ; bredd, höjd som i `p=`
//     tool   := "t=" namn "," kant "," int "," int "," f
//                   ; verktygsfältet: bandindex och plats i bandet
//                   ; (heltal >= 0), dold (f = 0|1)
//     ftool  := "ft=" namn "," pos "," pos "," f "," dock "," int
//                   ; ett FLYTANDE verktygsfält: fönstrets
//                   ; x, y (logiska px, som `f=`), dold, och MINNET för var
//                   ; det dockas tillbaka — kanten och bandet (heltal >= 0).
//                   ; Storleken står inte med: den är fältets egen
//     kant   := left | top | right | bottom    ; setProp("dock", …)s ord
//     dock   := kant | fill | none             ; också ett objekt i huvudytan
//                                              ; kan flyta
//     pos    := tal | "-"                      ; "-" = ingen plats (Wayland)
//     namn   := [^;=,]+                        ; `name`, objektets namn
//
// REGLERNA: `s=` öppnar en remsa, `c=` kräver en öppen remsa och `p=` en
// öppen cell; `f=`, `t=` och `ft=` är KONTEXTFRIA och rör varken remsan eller
// cellen. En remsa utan cell, en cell utan panel, en okänd tagg, fel
// fältantal, en flagga som inte är 0/1, ett tal som inte är ett ändligt tal
// (eller en negativ storlek, en vikt <= 0), en plats där bara den ena
// koordinaten är `-`, ett bandindex eller en plats som inte är ett heltal
// >= 0, två verktygsfält på samma plats i samma band, ett okänt kantord och
// ett namn två gånger (paneler och fält delar namnrymd — det är objektens
// namn, också mellan `t=` och `ft=`) avvisar HELA strängen — atomiskt,
// ingenting tillämpas. `ft=` tar dock-ordet (som `f=`) och inte bara kanten:
// skrivaren emitterar bara det parsern godtar, och ett fält i huvudytan
// (`Dock::None`) kan flyta.
// Flera aktiva i en cell: den första gäller; ingen: den första fliken. En
// onålad panel läses som infälld (en öppen overlay är ett drifttillstånd).
// Bandindexen får ha luckor — de packas när de tillämpas.
//
// PANELENS MÅTT STÅR PER PANEL: modellen läser
// varje panels `width`/`height` — remsans tjocklek är HÄRLEDD ur dem —
// så strängen sparar dem per panel, och `s=` bär bara
// kanten. En tjocklek på remsan hade varit en andra sanning som kan säga
// emot panelernas, och en återläsning hade ändrat flikarnas mått och deras
// läge efter en omdockning.
//
// EN FLYTANDE PANEL ÄR NÅLAD OCH UTFÄLLD:
// valet är den uttryckliga normaliseringen, inte två flaggor till i
// `f=`. `setFloating(true)` fäster och fäller ut panelen, och knappnålen
// och infällningen nekas så länge den flyter. Det finns alltså inget
// knappnålsläge att spara, och en panel som dockas tillbaka står dockad och
// utfälld. Panelens bredd och höjd sparas däremot — de är remsans mått när
// den dockas igen.
//
// TALEN skrivs i kortaste form som läses tillbaka EXAKT (`std::to_chars`,
// lokaloberoende) och läses med `std::from_chars` — ett tal utanför `float`
// avvisas av den, och NaN/oändligt av en egen kontroll.
//
// ETT FLYTANDE VERKTYGSFÄLT HAR EN EGEN TAGG och inte en
// utökad `t=`: `t=` bär platsunikhet och packade index som gäller dockade
// fält, och en flytande post hade fått undantag i varje regel — delningen
// följer `p=`/`f=`. En `t=` för ett fält som flyter dockar det; ett flytande
// fält som strängen inte nämner fortsätter att flyta.
//
// VERSIONEN är `v2`: `t=` (verktygsfält), `f=` (flytande paneler) och
// `ft=` (flytande fält) ryms alla i den.

namespace glaze::detail {

struct LayoutPanelSpec {
    std::string name;
    bool        active    = false;
    bool        collapsed = false;
    bool        pinned    = true;
    bool        closed    = false;
    float       width     = 0.0f;
    float       height    = 0.0f;
};

struct LayoutCellSpec {
    float                        weight = 1.0f;
    std::vector<LayoutPanelSpec> panels;
};

struct LayoutStripSpec {
    Dock                        edge = Dock::Left;
    std::vector<LayoutCellSpec> cells;
};

// En flytande panel: fönstrets plats och innehållsyta, och det panelen
// dockas tillbaka med.
struct LayoutFloatSpec {
    std::string name;
    bool        hasPosition = false;   // false = "-" (ingen plats)
    float       x = 0.0f, y = 0.0f;
    float       w = 0.0f, h = 0.0f;    // innehållsytan, 0 = föredragen
    bool        closed = false;
    Dock        dock   = Dock::Left;
    float       width  = 0.0f;         // panelens egna mått, som i `p=`
    float       height = 0.0f;
};

// Ett verktygsfält: kanten, bandindexet och platsen i bandet.
struct LayoutBarSpec {
    std::string name;
    Dock        edge   = Dock::Top;
    int         band   = 0;
    int         pos    = 0;
    bool        hidden = false;
};

// Ett FLYTANDE verktygsfält: fönstrets plats, dold, och
// minnet för var det dockas tillbaka.
struct LayoutFloatBarSpec {
    std::string name;
    bool        hasPosition = false;   // false = "-" (ingen plats)
    float       x = 0.0f, y = 0.0f;
    bool        hidden = false;
    Dock        dock   = Dock::Top;
    int         band   = 0;
};

struct LayoutSpec {
    std::vector<LayoutStripSpec>    strips;
    std::vector<LayoutFloatSpec>    floats;      // i strängens ordning
    std::vector<LayoutBarSpec>      bars;        // i strängens ordning
    std::vector<LayoutFloatBarSpec> floatBars;   // i strängens ordning
};

// Varför en sträng avvisades — kategorin och det felande stycket, loggens två
// delar.
struct LayoutError {
    std::string why;
    std::string what;
};

// ATOMISK: hela strängen tolkas till `out` först när den är giltig rakt
// igenom; vid fel lämnas `out` orörd och `err` beskriver det första felet.
bool parseLayout(const std::string& s, LayoutSpec& out, LayoutError& err);

// Ett namn går att spara om det är icke-tomt och saknar strängens
// avgränsare. Ett medlemsnamn gör alltid det.
bool savableName(const std::string& n);

// Kantens ord i strängen (left/top/right/bottom) — `dock`-vokabulären.
const char* edgeWord(Dock d);

// Talet i kortaste form som läses tillbaka exakt — lokaloberoende.
std::string numberText(float v);

} // namespace glaze::detail
