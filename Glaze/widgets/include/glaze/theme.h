#pragma once
#include <span>
#include <string>

#include <gfx/color.h>

#include "glaze/style.h"

// Tema-systemet: PALETTEN. En Theme är en namngiven färgfamilj med en
// light- och en dark-variant. Aktiv variant resolvas vid VARJE repaint
// (AppWindow::makeCtx), så System-läget följer OS:ets ljus/mörkt-byte
// utan omstart och utan polling: OS-byte → EventType::SystemThemeChanged
// → invalidate() → nästa repaint resolvar om (hård regel 1 bevaras).
//
// Theme är en renodlad palettbärare — data användaren får sätta
// (setColorsLight/setColorsDark). VILKEN RITNING som körs ägs av `UI` och
// MÅTTEN av `Style` (båda i glaze/style.h) — tre skilda axlar. En UI kan
// ha FLERA teman: Slate har tio, Minimalistic nio, Glaze ett.
//
// **Namnrymden är per UI, inte global**: "Neutral" för Minimalistic och "Neutral"
// för Slate är två OLIKA paletter, eftersom Slate ritar FYLLDA ytor där
// Minimalistic ritar KANTER och samma mättnadsprofil därför ger ett annat
// intryck. Därför `Theme::slate(Hue)` / `Theme::minimalistic(Hue)`, aldrig
// en platt `Theme::neutral()`.

namespace glaze {

// Temavalet: System följer OS:et (resolvas per repaint), Light/Dark låser.
enum class ThemeMode { System, Light, Dark };

// Kulören en genererad palettfamilj byggs ur. **ENUM och inte en fri
// float**: kontrasten varierar kraftigt med hue — gult och cyan
// är mycket ljusare än blått vid samma HSL-ljushet — så en fri vinkel kan
// hamna under 3:1 utan att någon märker det. Ett enum går att GRINDA, och
// det gör kontrasttestet (tests/contrast_gate.cpp) för varje hue × läge ×
// familj. Neutral är samma generator med mättnad 0 — ingen särfallskod.
enum class Hue { Neutral, Red, Orange, Amber, Green, Teal, Blue, Indigo, Violet };

// Alla hues i ordning. Väljare (demons tema-dropdown) och kontrastgrinden
// itererar HÄR, så ingen lista kan glömmas bort vid en ny kulör.
inline constexpr Hue kAllHues[] = {Hue::Neutral, Hue::Red,   Hue::Orange,
                                   Hue::Amber,   Hue::Green, Hue::Teal,
                                   Hue::Blue,    Hue::Indigo, Hue::Violet};
inline constexpr int kHueCount = 9;

class Theme {
public:
    // Färgrollerna widgets och appar ritar med (via Ctx::theme). Nya
    // roller läggs till här i takt med widget-behoven — aldrig hårdkodade
    // färger ute i widget-/appkod.
    struct Colors {
        gfx::Color windowBg;       // fönstrets bakgrund
        gfx::Color panel;          // sekundär yta (sidopaneler, verktygsfält)
        gfx::Color appWorkspace;   // försänkt arbetsyta (bakom dokument/canvas)
        gfx::Color text;           // vanlig text
        gfx::Color textDim;        // sekundär/inaktiverad text
        gfx::Color button;         // knappfyllnad (vila)
        gfx::Color buttonHover;
        gfx::Color buttonPress;
        gfx::Color buttonDisabled; // inaktiv knapp — EGEN roll, för i en
                                   // palett där knappen är accenttonad kan
                                   // "inaktiv" inte vara samma som vila
        gfx::Color buttonBorder;   // knappens kontur; skild från `border`
                                   // eftersom en liten kontroll tål en
                                   // starkare linje än en stor yta gör

        // LAMINERINGEN — de två ljusa fälten som läggs över en yta för att
        // ge den materialitet (övre X % respektive övre Y %, andelarna bor
        // i Style). Alfan sitter i FÄRGEN och är det som skiljer
        // lägena åt: ett vitt lager mot en mörk yta ger mycket större
        // upplevd effekt per procentenhet än mot en ljus. Helt genomskinlig
        // = ingen laminering (så ser Slate ut).
        gfx::Color sheen;          // det nedre, större fältet
        gfx::Color sheenHigh;      // det övre, mindre — läggs OVANPÅ

        gfx::Color border;         // konturer runt kontroller
        gfx::Color accent;         // markering/fokus/bock

        // Accenten som YTA — EGNA roller, inte tint-funktioner (samma skäl
        // som trafikljusen: en palett ska kunna välja fritt vad "tryckt"
        // betyder). Att de är skilda från `accent` ovan är ett MÄTT krav
        // och inte en bekvämlighet: `accent` är accenten som BETYDELSE
        // (text, länk, ring, bock) och måste klara 4.5:1 mot bakgrunden,
        // vilket i mörkt läge tvingar fram en LJUS violett. En ljus yta
        // bär i sin tur ingen vit text. Kulörerna sammanfaller därför i
        // ljust läge och GÅR ISÄR i mörkt — en axel, två jobb.
        gfx::Color accentSurface;
        gfx::Color accentSurfaceHover;
        gfx::Color accentSurfacePress;
        gfx::Color onAccent;       // text/glyf ovanpå accentytan

        gfx::Color shadow;         // skuggfärg (alfan bär styrkan)

        // Länken (Link). EGNA roller — en länk är inte alltid `accent`:
        // en app kan vilja ha vanlig textfärg i vila och färg först vid
        // hover. Theme äger alla färger, även den valfriheten.
        gfx::Color link;
        gfx::Color linkHover;

        // Chromet — fönstrets titelrad + min/max/close. Ritas av
        // gfx-backenden men färgsätts härifrån: AppWindow trycker ner
        // rollerna som gfx::ChromeColors vid varje repaint.
        gfx::Color titlebar;       // titelradens bakgrund
        gfx::Color titlebarText;
        gfx::Color chromeHover;    // min/max-knapparnas hover
        gfx::Color chromePress;
        gfx::Color chromeClose;    // stäng-knappens hover (röd av hävd)
        gfx::Color chromeGlyph;    // knappglyferna
        gfx::Color chromeCloseGlyph; // glyfen ovanpå röd stäng-hover
        gfx::Color frame;          // tunn ram runt omaximerat fönster

        // Trafikljusen (CaptionButton::Shape::Circle — Mac-namnlisten).
        // EGNA roller, inte hårdkodade tal i widgeten: en app som vill ha
        // andra cirkelfärger byter dem som vilken annan roll som helst.
        // Nedtryckt läge är en EGEN roll i stället för en tint-funktion —
        // en palett ska kunna välja fritt vad "tryckt" betyder.
        // Default-familjen håller dem lika i light och dark (macOS gör så).
        gfx::Color trafficClose;
        gfx::Color trafficClosePress;
        gfx::Color trafficMin;
        gfx::Color trafficMinPress;
        gfx::Color trafficMax;
        gfx::Color trafficMaxPress;
        gfx::Color trafficGlyph;   // hover-glyfen inne i cirkeln

        // Kapselklustret (CaptionButton::Shape::Capsule).
        // Härleds av `deriveCapsule()` i theme.cpp för VARJE palett,
        // handskriven som genererad: `accentSurface` blandad in i
        // `titlebar` med tre andelar. Det är därför fälten står SIST och
        // saknas i de positionella literalerna — de fylls efteråt, på ETT
        // ställe, och det stället är den enda knappen att vrida på när
        // klustret ska bli svagare eller starkare. En app får förstås
        // skriva över dem som vilken annan roll som helst.
        gfx::Color capsuleBorder;  // kapselns kontur (= frame, fönsterramens ton)
        gfx::Color capsuleBody;    // kapselns svagt accenttonade kropp
        gfx::Color capsuleHover;   // TÄNT fack — dämpat, inte full accentyta
        gfx::Color capsulePress;   // tryckt fack
        // STÄNGKNAPPENS hover — dämpad `chromeClose` (−20 %).
        // Egen roll och inte `chromeClose` rakt av, eftersom
        // glyf-formen (Windows-stilen) fortfarande vill ha det fulla röda:
        // i kapseln ska hovern säga "här är du", inte "är du säker".
        gfx::Color capsuleClose;

        // SKRIVSKYDDAD FÄLTYTA (TextInput::readOnly). En EGEN
        // roll och inte disabled-tillståndet: inaktiv betyder "inte nu",
        // skrivskyddad betyder "aldrig, men läs gärna". Texten behåller
        // därför full kontrast; det är YTAN som blir tyst.
        //
        // Varför en egen roll: fältytan ÄR redan `panel` i både
        // Slate och Glaze, så det finns ingen lugnare BEFINTLIG yta att
        // byta till. Härleds som kapselrollerna (`deriveFieldReadOnly` i
        // theme.cpp) och står därför SIST, utanför de positionella
        // literalerna — ett fält insatt MITT i structen hade tyst förskjutit
        // varenda färg i de två handskrivna paletterna, och det hade
        // KOMPILERAT.
        //
        // Grindad i tests/contrast_gate.cpp: `text` ≥ 4.5:1 mot den (värdet
        // ska läsas) och `buttonBorder` ≥ 3:1 (Minimalistic ritar ingen
        // annan form än kanten — en fyllning som närmar sig kanten äter
        // upp fältet).
        gfx::Color fieldReadOnly;

        // STATUSROLLERNA — meddelanderutans badge och
        // dess glyf. FYRA roller och inte två: en yta utan sin textfärg är
        // halva svaret, och det halva svaret hade tvingat ikonen att gissa
        // (`text` mot en mättad amber-yta mäter under 4.5:1 i ljust läge).
        //
        // Information och Confirmation återanvänder `accentSurface`/
        // `onAccent` — de säger "här är något", vilket accenten redan
        // betyder. Bara VARNING och FEL har en betydelse färgen bär.
        //
        // HÄRLEDDA i `deriveStatus()` och därför SIST, utanför de
        // positionella literalerna — samma fälla som kapseln och
        // `fieldReadOnly` undviker: ett fält insatt MITT i structen
        // KOMPILERAR och förskjuter tyst varenda färg i de handskrivna
        // paletterna.
        //
        // De är INTE hue-beroende och skiftar INTE med ljust/mörkt läge,
        // och det är ett beslut: rött betyder fel i en violett app och i en
        // grön, i ljust läge och i mörkt. Samma skäl som gör trafikljusen
        // identiska i båda lägena. `onWarning`/`onError` väljs däremot av
        // en MÄTNING (nästan-vit eller nästan-svart, den som mäter högst) —
        // gult och rött ligger i olika ändar av luminansskalan och kan
        // omöjligt bära samma textfärg.
        //
        // Grindade i tests/contrast_gate.cpp: paren ≥ 4.5:1 i ALLA inbyggda
        // teman, de handskrivna inräknade — glyfen är meningsbärande text.
        // En app får skriva över dem som vilken annan roll som helst.
        gfx::Color warningSurface;
        gfx::Color onWarning;
        gfx::Color errorSurface;
        gfx::Color onError;

        // SLÄPPYTAN — den genomskinliga fyllningen som
        // visar VAR en dragen panel hamnar (dockningsytans indikator, ritad
        // OVANPÅ innehållet). Accentytan med låg alfa: innehållet under ska
        // fortfarande synas, för användaren siktar mot det. Konturen runt den
        // är `accent` (betydelse — samma som drag-och-släppets målram).
        //
        // HÄRLEDD i `deriveDropHint()` och därför SIST, utanför de
        // positionella literalerna — samma fälla som de tre grupperna ovan.
        // Alfan är paletten, inte widgeten: en app som vill ha en starkare
        // indikator skriver över rollen.
        gfx::Color dropHint;

        // RITYTANS RUTNÄT — linjerna en `Canvas` ritar
        // med `setGridSize`. En hjälp och inte innehåll: en svag ton av
        // TEXTEN mot fältets yta (`panel`), så den syns i varje palett och
        // läge (texten mäter ≥ 4,5:1 mot ytan, och en femtedel av den vägen
        // syns som en linje utan att bli en). HÄRLEDD i `deriveCanvasGrid()`
        // och därför SIST, utanför de positionella literalerna.
        gfx::Color canvasGrid;

        // STANDARDIKONERNAS FÄRGER — accentdelarna i
        // `StockIconStyle::Color` (glaze/stock_icon.h); konturen är `text`
        // som i mono. Kulören BETYDER något (statusrollernas skäl: en mapp är
        // gul, ett fel rött) och följer inte familjens hue — men LJUSHETEN
        // följer läget, ur paletten. HÄRLEDDA i `deriveStockIcon()` och
        // därför SIST, utanför de positionella literalerna.
        gfx::Color stockBlue;      // information, spara, navigering, media
        gfx::Color stockGreen;     // klar, lägg till, spela
        gfx::Color stockRed;       // fel, ta bort, stopp
        gfx::Color stockAmber;     // varning, lås, stjärna
        gfx::Color stockYellow;    // mappar — en YTA, aldrig en linje
    };

    // Allt en widget behöver vid ritning för ETT läge. Egen nivå (inte
    // bara = Colors) så framtida parametrar får sin plats här utan att
    // Ctx eller widgets behöver röras.
    struct Variant {
        Colors colors;
    };

    std::string name;
    Variant     light;
    Variant     dark;

    // Aktiv variant för ett läge. System frågar gfx::systemTheme() vid
    // varje anrop — det är det som gör resolvern per repaint levande.
    const Variant& resolve(ThemeMode mode) const;

    // ------------------------------------------------ de inbyggda familjerna
    //
    //   slateDefault        "Windows": Win11-gråskalan.
    //                       RÖRS INTE — referensen allt annat
    //                       jämförs mot, och därför den enda som INTE är
    //                       grindad (Microsofts kantkontrast är 1.39:1).
    //   glazeDefault        "Lavender": husets designade palett, violett
    //                       på lavendeltonade neutraler. Glaze har med flit
    //                       BARA ett tema.
    //   minimalisticDefault = minimalistic(Hue::Neutral).
    static const Theme& slateDefault();
    static const Theme& glazeDefault();
    static const Theme& minimalisticDefault();

    // De GENERERADE familjerna: en palett per hue, byggd ur en LJUSHETS-
    // TRAPPA (theme.cpp). Trappan bär designen; hexvärdena är dess utfall —
    // 36 roller × 2 lägen × 9 hues × 2 familjer är varken granskningsbart
    // eller underhållbart handskrivet, och en ny färgroll hade krävt 36 nya
    // rader. Grindade av tests/contrast_gate.cpp.
    static const Theme& slate(Hue h);
    static const Theme& minimalistic(Hue h);
};

// ------------------------------------------------------- UI -> dess teman
//
// Vilka paletter en ritning har, och i vilken ordning en väljare ska visa
// dem. Index 0 är ALLTID familjens default (det setUI återställer till).
// EN sanning — demons dropdown och defaultTheme läser samma lista.
//
//   Slate         10: "Windows" + de nio hues
//   Minimalistic   9: de nio hues, Neutral först
//   Glaze          1: "Lavender"
int          themeCount(UI u);
const Theme& themeAt(UI u, int index);   // klampas till giltigt index
const Theme& defaultTheme(UI u);         // = themeAt(u, 0)

// ------------------------------------------------ färgväljarens palett
//
// DE FÖRVALDA FÄRGERNA i färgväljarens popup. Färger,
// och därför här och inte i widget-filen (regel 3) — men INTE en temaroll:
// rollerna säger vad en YTA är ("knappens kropp i mörkt läge"), paletten är
// INNEHÅLL användaren väljer ur, och den ska vara densamma i varje tema och
// läge. En röd ruta som blev blekrosa i mörkt läge hade gett appen en annan
// färg än den användaren klickade på.
//
// SKRIVEN I RADER: en gråskala och tre toner (ljus · mättad · mörk) av tio
// kulörer, så rutnätets bredd hör till DATAN och följer med den — widgeten
// har inget eget tal för den.
struct ColorPalette {
    std::span<const gfx::Color> colors;
    int                         columns = 1;
};
const ColorPalette& standardPalette();

} // namespace glaze
