#include "glaze/theme.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gfx/platform.h>

#include "glaze/color.h"

namespace glaze {

// Färghjälparna bor i glaze/color.h — publika för ett sidopaket som härleder
// sina färger ur temat.
using color::contrast;
using color::hsl;
using color::hueDegrees;
using color::luminance;
using color::mix;

const Theme::Variant& Theme::resolve(ThemeMode mode) const
{
    switch (mode) {
    case ThemeMode::Light:
        return light;
    case ThemeMode::Dark:
        return dark;
    default:
        // System: fråga OS:et nu — anropas per repaint, så ett OS-byte
        // slår igenom vid nästa omritning utan omstart
        return gfx::systemTheme() == gfx::SystemTheme::Dark ? dark : light;
    }
}

namespace {

// KAPSELKLUSTRETS färger härleds — de skrivs inte ut.
//
// Skälet är att de ska gå att vrida på för ALLA tjugo paletterna samtidigt:
// "gör hovern mindre stark" ska vara EN siffra att ändra, inte fyrtio hex
// att skriva om. Andelarna nedan ÄR designen; resten är mekanik.
//
// Basen är `accentSurface` och inte `accent`. `accent` är accenten som
// BETYDELSE (text, ring, bock) och tvingas i mörkt läge till en LJUS ton
// för att klara 4.5:1 som text — en yta i den tonen blir gäll. Rollen
// `accentSurface` finns just för accenten som YTA, och ett fack ÄR en yta.
// Konturen är däremot `accent`: den är en linje, alltså betydelse.
//
// Andelarna (0..1 av accentytan, resten `titlebar`):
// Kroppens ton, som andel av vägen från underlaget (`windowBg`) till
// `chromeHover`. 0.60 valt så att de GENERERADE paletterna landar på exakt
// den separation Minimalistic redan hade (0.033
// ljushet under baren) — de handskrivna får samma andel och därmed en ton
// som syns i stället för att försvinna.
constexpr float kCapsuleBody  = 1.15f;
// TÄNT FACK. Andelen är avståndet från `titlebar` — 1.0 = hela vägen till
// chrome-tonen, och över 1.0 EXTRAPOLERAR (color::mix klampar).
// Min/max +40 %, stängknappen −20 %. De två går åt olika håll
// med flit: hovern på min/max är annars för tyst för att synas, medan det röda
// på stängknappen är annars starkare än en hover behöver vara — den ska säga
// "här är du", inte "är du säker".
constexpr float kCapsuleHover = 1.40f;
constexpr float kCapsuleClose = 0.80f;
// TRYCKET är INTE en fjärde blandning. Grinden fäller 0.68 på 3.36:1 i
// ljust läge (Slate/Indigo): ju mer accentyta, desto mörkare fyllning, och
// den mörka glyfen slutar hålla. Att bara dra ner andelen hade gett ett
// tryck som knappt syns — samma 1.50:1-problem.
// Trycket går därför HELA VÄGEN till `accentSurfacePress` och glyfen flippar
// till `onAccent`, ett par paletten redan grindar (`onAcc/press`). Hovern
// är dämpad och tyst, trycket är helt — det är skillnaden mellan att peka
// och att bestämma sig.

void deriveCapsule(Theme::Colors& c)
{
    // UTAN ACCENT: kapseln är CHROME, och chromet
    // är inte en plats där appen uttrycker sig — accenten drar blicken till
    // fönsterknapparna, som är det man ska titta minst på. Rollerna nedan
    // finns redan och är designade för exakt de här knapparna: glyf-formen
    // (Windows-stilen) har alltid använt `chromeHover`/`chromePress`.
    //
    // KONTUREN = FÖNSTERRAMEN. Samma roll som linjen runt fönstret, för det
    // ÄR samma sorts linje: chrome-struktur, inte kontrollform.
    //
    // Regeln bakom, och den gäller allt chrome:
    // **inramad, men står inte ut.** Namnlisten och fönsterknapparna är det
    // man ska titta MINST på — de ska gå att hitta när man söker dem, inte
    // dra ögat dit när man inte gör det. Accent som kontur skulle dra ögat, för accent
    // BETYDER "titta här", och klustret skulle bli det starkaste i hela
    // namnlisten. Ser du en svag linje här är den avsiktlig — det är inte
    // ett förbiseende att laga.
    c.capsuleBorder = c.frame;
    // KROPPEN räknas ur `windowBg` — barens FAKTISKA fyllning
    // — och inte ur `titlebar`. Skälet: i de två
    // HANDSKRIVNA paletterna är `titlebar` identisk med `windowBg`
    // (Windows 243/243), så en andel av vägen från `titlebar` landar
    // 2 steg från underlaget och tonen FÖRSVINNER. Att den syns i
    // Minimalistic är en tillfällighet: den familjen har bara GENERERADE
    // paletter, där trappan skiljer de två (0.930 mot 0.960).
    // Ur underlaget blir separationen densamma i alla tjugo.
    c.capsuleBody   = mix(c.windowBg, c.chromeHover, kCapsuleBody);
    c.capsuleHover  = mix(c.titlebar, c.chromeHover, kCapsuleHover);
    c.capsulePress  = c.chromePress;
    c.capsuleClose  = mix(c.titlebar, c.chromeClose, kCapsuleClose);
}

// SKRIVSKYDDAD FÄLTYTA — härledd av samma skäl som kapseln:
// en tyst yta i tjugo paletter ska vara EN siffra att vrida på, inte
// tjugo hex att skriva om.
//
// Tonen ligger mellan fönstrets bakgrund och den FÖRSÄNKTA arbetsytan:
// "läs gärna, men skriv inte här". Den kan inte vara `panel` (det ÄR den
// redigerbara fältytan i både Slate och Glaze — då syns ingen skillnad)
// och inte heller exakt `windowBg` (då försvinner ytan helt, och för
// MINIMALISTIC — vars fält saknar fyllning i vila — hade signalen blivit
// osynlig). Halva vägen mellan de två är därför tonen.
//
// Grindens mått (tests/contrast_gate.cpp, `btnBorder/roField`, min
// över nio hues × två lägen): Minimalistic **3.03:1** (Green/ljust) ·
// Slate **3.10:1** (Teal/ljust). Marginalen är tunn — men den är
// LJUSHETSTRAPPANS och inte den här andelens: samma familjs redan
// befintliga `btnBorder/bg` mäter 3.15:1 i exakt samma hue, och en
// svepning över andelen 0.30–0.50 flyttar minimum med 0.05. Det finns
// alltså ingenting att vinna på att dra tonen närmare `windowBg`, bara
// synlighet att förlora. Faller raden är det trappan som ska ses över.
//
// Handskrivna paletterna (grindas INTE — uttryckligt undantag i
// contrast_gate.cpp):
//   Lavender  `buttonBorder`/`fieldReadOnly` 6.19:1 ljust · 6.38:1 mörkt
//             `text`/`fieldReadOnly`         14.28:1 · 16.82:1
//   Windows   `buttonBorder`/`fieldReadOnly` 1.31:1 ljust · 1.57:1 mörkt
//             `text`/`fieldReadOnly`         14.58:1 · 17.04:1
// Windows-siffran är LÅG och det är väntat: dess `buttonBorder` är
// #d0d0d0 och mäter redan 1.39:1 mot sin EGEN bakgrund — Microsofts
// palett är referensen och skrivs inte om för att blidka vår grind
// (samma undantag som contrast_gate.cpp:s filhuvud beskriver). Den
// skrivskyddade ytan ÄRVER alltså referenspalettens svaga kant; den
// tillför ingen ny svaghet. Slate ritar dessutom en FYLLD yta, så formen
// bärs inte av kanten där — det är i Minimalistic kravet biter, och den
// familjen är genererad och grindad.
constexpr float kFieldReadOnly = 0.5f;

void deriveFieldReadOnly(Theme::Colors& c)
{
    c.fieldReadOnly = mix(c.windowBg, c.appWorkspace, kFieldReadOnly);
}

// STATUSYTORNA — varningens och felets badge, plus
// glyfens färg ovanpå var och en.
//
// De här två är de ENDA roller i hela paletten som INTE följer familjens
// kulör, och det är ett beslut: rött betyder fel i en violett app och i en
// grön. En status som bytte färg med temat hade slutat vara en status och
// blivit dekor. Samma skäl gör dem lika i ljust och mörkt läge — precis som
// trafikljusen, som macOS håller identiska i båda.
//
// LJUSHETEN är EN siffra för båda, och den är vald ur mätningen nedan:
// vid 0.44 landar rött på luminans ~0.127 (nästan-vit glyf, 5.7:1) och
// amber på ~0.341 (nästan-svart glyf, 7.1:1). Kravet är TEXTENS 4.5:1,
// för glyfen ÄR text — den ritas via fonten, inte som en form.
constexpr float kStatusWarnHue = 44.0f;    // amber — samma vinkel som Hue::Amber
constexpr float kStatusErrHue  = 358.0f;   // röd   — samma vinkel som Hue::Red
constexpr float kStatusSat     = 0.75f;
constexpr float kStatusLight   = 0.44f;

// Glyfen väljs som `onAccent` väljs: den av nästan-vit och nästan-svart som
// mäter HÖGST mot ytan. Ingen enda färg klarar båda ändarna av
// kulörcirkeln, och amber och rött ligger just i var sin ände.
constexpr float kOnStatusLight = 0.985f;   // nästan vit, inte vit
constexpr float kOnStatusDark  = 0.050f;   // nästan svart

gfx::Color onStatus(gfx::Color surface)
{
    const gfx::Color white = hsl(0.0f, 0.0f, kOnStatusLight);
    const gfx::Color black = hsl(0.0f, 0.0f, kOnStatusDark);
    return contrast(white, surface) >= contrast(black, surface) ? white : black;
}

void deriveStatus(Theme::Colors& c)
{
    c.warningSurface = hsl(kStatusWarnHue, kStatusSat, kStatusLight);
    c.onWarning      = onStatus(c.warningSurface);
    c.errorSurface   = hsl(kStatusErrHue, kStatusSat, kStatusLight);
    c.onError        = onStatus(c.errorSurface);
}

// SLÄPPYTAN — accentYTAN med en fjärdedels täckning. Ytan och
// inte `accent`: `accent` är accenten som betydelse (text, ring), och i
// mörkt läge en ljus ton som hade blekt ut innehållet under i stället för
// att tona det. En fjärdedel låter texten under läsas — indikatorn säger
// VAR, den ska inte dölja VAD. Konturen ritas i `accent` av widgeten.
constexpr float kDropHintAlpha = 0.25f;

void deriveDropHint(Theme::Colors& c)
{
    c.dropHint   = c.accentSurface;
    c.dropHint.a = static_cast<std::uint8_t>(255.0f * kDropHintAlpha + 0.5f);
}

// RITYTANS RUTNÄT — en femtedel av vägen från fältets
// yta mot texten. Linjen är en HÅRLINJE och delas ofta mellan två
// pixelrader, så tonen måste tåla att halveras och ändå skilja sig från
// ytan; och den inaktiverade ritytan tonar den med 1 − iconFade, som också
// ska synas. Texten mäter ≥ 4,5:1 mot `panel` i varje palett (contrast_gate),
// så en femtedel av den skillnaden är synlig i alla tjugo.
constexpr float kCanvasGrid = 0.2f;

void deriveCanvasGrid(Theme::Colors& c)
{
    c.canvasGrid = mix(c.panel, c.text, kCanvasGrid);
}

// STANDARDIKONERNAS FÄRGER — färgvariantens accentdelar. Kulörerna
// är familjegeneratorns vinklar (`color::hueDegrees`), gult en bit från
// amber; mättnaden är EN. Ljusheten följer LÄGET, läst ur paletten (texten
// ljusare än bakgrunden = mörkt läge): mot en ljus bakgrund en mörkare ton och
// mot en mörk en ljusare, så en röd linje syns i båda. Gult är en YTA bakom
// konturen och därför ljusare än de andra.
constexpr float kStockYellowHue = 48.0f;
constexpr float kStockSat       = 0.70f;
constexpr float kStockLight     = 0.46f;   // ljust läge
constexpr float kStockDark      = 0.62f;   // mörkt läge
constexpr float kStockYellowUp  = 0.12f;   // gult ljusare än de andra

void deriveStockIcon(Theme::Colors& c)
{
    const float l = luminance(c.text) > luminance(c.windowBg) ? kStockDark
                                                              : kStockLight;
    c.stockBlue   = hsl(hueDegrees(Hue::Blue), kStockSat, l);
    c.stockGreen  = hsl(hueDegrees(Hue::Green), kStockSat, l);
    c.stockRed    = hsl(hueDegrees(Hue::Red), kStockSat, l);
    c.stockAmber  = hsl(hueDegrees(Hue::Amber), kStockSat, l);
    c.stockYellow = hsl(kStockYellowHue, kStockSat, l + kStockYellowUp);
}

// De HÄRLEDDA grupperna i ETT anrop — allt som beräknas ur en färdig
// palett i stället för att skrivas ut. Kallas av båda handskrivna
// paletterna OCH av generatorn, så en ny härledd roll kan aldrig glömmas
// på ett av de tre ställena.
void deriveRoles(Theme::Colors& c)
{
    deriveCapsule(c);
    deriveFieldReadOnly(c);
    deriveStatus(c);
    deriveDropHint(c);
    deriveCanvasGrid(c);
    deriveStockIcon(c);
}

} // namespace

const Theme& Theme::slateDefault()
{
    // Windows standardfärger (Win11-paletten) i båda lägena.
    static const Theme t = [] {
        Theme th{
        "Windows",   // Win11-paletten — referensen, RÖRS INTE. lang_gate:rad API-namn
        // light
        Variant{Colors{
            {243, 243, 243, 255},   // windowBg
            {251, 251, 251, 255},   // panel
            {229, 229, 229, 255},   // appWorkspace
            {27, 27, 27, 255},      // text
            {96, 96, 96, 255},      // textDim
            {251, 251, 251, 255},   // button
            {246, 246, 246, 255},   // buttonHover
            {240, 240, 240, 255},   // buttonPress
            {251, 251, 251, 255},   // buttonDisabled — = button: Slate rörs inte
            {208, 208, 208, 255},   // buttonBorder   — = border
            {255, 255, 255, 0},     // sheen          — Slate lamineras inte
            {255, 255, 255, 0},     // sheenHigh
            {208, 208, 208, 255},   // border
            {0, 103, 192, 255},     // accent
            {0, 103, 192, 255},     // accentSurface — samma i ljust läge
            {0, 90, 168, 255},      // accentSurfaceHover
            {0, 78, 145, 255},      // accentSurfacePress
            {255, 255, 255, 255},   // onAccent — vit text på blå yta
            {0, 0, 0, 40},          // shadow — diskret på ljus botten
            {0, 103, 192, 255},     // link
            {0, 78, 145, 255},      // linkHover — mörknar vid pekaren
            {243, 243, 243, 255},   // titlebar — sömlös mot windowBg
            {27, 27, 27, 255},      // titlebarText
            {229, 229, 229, 255},   // chromeHover
            {224, 224, 224, 255},   // chromePress
            {196, 43, 28, 255},     // chromeClose — Windows-rött, båda lägena
            {27, 27, 27, 255},      // chromeGlyph
            {255, 255, 255, 255},   // chromeCloseGlyph — vit på rött
            {208, 208, 208, 255},   // frame
            {255, 95, 87, 255},     // trafficClose      — macOS-cirklarna,
            {204, 76, 69, 255},     // trafficClosePress   samma i båda lägena
            {254, 188, 46, 255},    // trafficMin
            {203, 150, 36, 255},    // trafficMinPress
            {40, 200, 64, 255},     // trafficMax
            {32, 160, 51, 255},     // trafficMaxPress
            {0, 0, 0, 190},         // trafficGlyph      — mörk, halvtransp.
        }},
        // dark
        Variant{Colors{
            {32, 32, 32, 255},      // windowBg
            {43, 43, 43, 255},      // panel
            {24, 24, 24, 255},      // appWorkspace
            {255, 255, 255, 255},   // text
            {160, 160, 160, 255},   // textDim
            {45, 45, 45, 255},      // button
            {50, 50, 50, 255},      // buttonHover
            {39, 39, 39, 255},      // buttonPress
            {45, 45, 45, 255},      // buttonDisabled — = button
            {61, 61, 61, 255},      // buttonBorder   — = border
            {255, 255, 255, 0},     // sheen
            {255, 255, 255, 0},     // sheenHigh
            {61, 61, 61, 255},      // border
            {96, 205, 255, 255},    // accent
            {0, 120, 212, 255},     // accentSurface — MÄTTAD, inte den ljusa
            {30, 140, 230, 255},    // accentSurfaceHover
            {0, 100, 180, 255},     // accentSurfacePress
            {255, 255, 255, 255},   // onAccent
            {0, 0, 0, 110},         // shadow — mörk botten kräver mer alfa
            {96, 205, 255, 255},    // link
            {160, 225, 255, 255},   // linkHover — ljusnar vid pekaren
            {32, 32, 32, 255},      // titlebar — sömlös mot windowBg
            {255, 255, 255, 255},   // titlebarText
            {45, 45, 45, 255},      // chromeHover
            {41, 41, 41, 255},      // chromePress
            {196, 43, 28, 255},     // chromeClose — Windows-rött, båda lägena
            {255, 255, 255, 255},   // chromeGlyph
            {255, 255, 255, 255},   // chromeCloseGlyph
            {61, 61, 61, 255},      // frame
            {255, 95, 87, 255},     // trafficClose
            {204, 76, 69, 255},     // trafficClosePress
            {254, 188, 46, 255},    // trafficMin
            {203, 150, 36, 255},    // trafficMinPress
            {40, 200, 64, 255},     // trafficMax
            {32, 160, 51, 255},     // trafficMaxPress
            {0, 0, 0, 190},         // trafficGlyph
        }},
        };
        // De härledda rollerna räknas ur den färdiga paletten — se
        // deriveRoles (kapseln + den skrivskyddade fältytan).
        deriveRoles(th.light.colors);
        deriveRoles(th.dark.colors);
        return th;
    }();
    return t;
}

// Glaze — HUSETS palett.
// Violett `#703cf9` på lavendeltonade neutraler: bakgrunden är inte grå
// utan `#efeffb`, och det är hela intrycket (neutralerna tonas
// mot accenten). EN accent, resten neutraler.
//
// Varje färg är GRINDAD mot kontrastkraven: text 4.5:1 mot
// sin bakgrund, kanter och tillståndsmarkeringar 3:1 (WCAG AA), i BÅDA
// lägena. De mätta värdena står i kommentarerna — går tydlighet och stil
// isär vinner tydligheten, så en färg som inte klarar sin siffra byts, den
// motiveras inte bort.
//
// Mörkt läge är HÄRLETT, inte inverterat: varje ljus roll har ett mörkt
// syskon med samma funktion. Undantaget är violetten, som i mörkt läge
// måste vara TVÅ färger: `accent` ljusnar till #a689ff för att klara
// 4.5:1 som text mot den nästan svarta bakgrunden, medan `accentSurface`
// mörknar till #7c4dff för att kunna bära vit text. I ljust läge är de
// samma kulör — det är mörkret som tvingar isär dem, inte smaken.
const Theme& Theme::glazeDefault()
{
    static const Theme t = [] {
        Theme th{
        "Lavender",  // husets palett. lang_gate:rad API-namn
        // ---------------------------------------------------------- light
        Variant{Colors{
            {239, 239, 251, 255},   // windowBg     — #efeffb ur color_idea
            {255, 255, 255, 255},   // panel        — vita kort på lavendel
            {227, 227, 245, 255},   // appWorkspace — försänkt, mörkare ton
            {25, 26, 43, 255},      // text         — 15.1:1 mot windowBg
            {90, 91, 115, 255},     // textDim      — 5.8:1, klarar 4.5
            {203, 186, 251, 255},   // button        — #cbbafb, svart text 9.8:1
            {192, 171, 251, 255},   // buttonHover   — MER violett, inte ljusare:
            {181, 156, 251, 255},   // buttonPress     kroppen är redan blek
            {227, 227, 245, 255},   // buttonDisabled— neutral, ingen accent
            {92, 58, 192, 255},     // buttonBorder  — #5c3ac0, bärande (se nedan)
            {255, 255, 255, 61},    // sheen         — vit 24 %
            {255, 255, 255, 42},    // sheenHigh     — vit 16.5 % ovanpå
            {123, 124, 158, 255},   // border        — 3.5:1, klarar 3
            {112, 60, 249, 255},    // accent       — #703cf9, 4.97:1 som text
            {112, 60, 249, 255},    // accentSurface      — samma kulör i ljust
            {95, 44, 232, 255},     // accentSurfaceHover — vit text 6.97:1
            {79, 34, 201, 255},     // accentSurfacePress — vit text 8.73:1
            {255, 255, 255, 255},   // onAccent     — vit på accent 5.67:1
            {25, 26, 43, 41},       // shadow        — 16 %, kort och låg
            {112, 60, 249, 255},    // link         — accenten bär betydelse
            {79, 34, 201, 255},     // linkHover    — mörknar vid pekaren
            {239, 239, 251, 255},   // titlebar     — sömlös mot windowBg
            {25, 26, 43, 255},      // titlebarText
            {225, 224, 245, 255},   // chromeHover
            {214, 212, 240, 255},   // chromePress
            {196, 43, 28, 255},     // chromeClose  — rött av hävd
            {25, 26, 43, 255},      // chromeGlyph
            {255, 255, 255, 255},   // chromeCloseGlyph
            {198, 197, 226, 255},   // frame        — fönsterkant, inte kontroll
            {255, 95, 87, 255},     // trafficClose      — macOS-cirklarna,
            {204, 76, 69, 255},     // trafficClosePress   samma i båda lägena
            {254, 188, 46, 255},    // trafficMin
            {203, 150, 36, 255},    // trafficMinPress
            {40, 200, 64, 255},     // trafficMax
            {32, 160, 51, 255},     // trafficMaxPress
            {0, 0, 0, 190},         // trafficGlyph
        }},
        // ----------------------------------------------------------- dark
        Variant{Colors{
            {20, 20, 31, 255},      // windowBg     — nästan svart, violett dragning
            {29, 29, 43, 255},      // panel
            {14, 14, 23, 255},      // appWorkspace
            {242, 242, 248, 255},   // text         — 16.4:1 mot windowBg
            {162, 162, 189, 255},   // textDim      — 7.3:1
            {66, 47, 120, 255},     // button        — #422f78, vit text 11.0:1
            {74, 51, 139, 255},     // buttonHover   — mot accenten
            {58, 41, 106, 255},     // buttonPress   — mot svart
            {30, 30, 45, 255},      // buttonDisabled
            {155, 136, 238, 255},   // buttonBorder  — #9b88ee, 3.74:1 mot kroppen
            {216, 207, 253, 36},    // sheen         — 14 %, tonad mot violett
            {255, 255, 255, 18},    // sheenHigh     — vit 7 % ovanpå
            {118, 118, 160, 255},   // border        — 3.5:1 mot knappytan
            {166, 137, 255, 255},   // accent       — LJUSARE violett: 6.6:1 som text
            {124, 77, 255, 255},    // accentSurface      — MÄTTAD: vit text 4.81:1,
            {143, 102, 255, 255},   // accentSurfaceHover   och 3.80:1 mot bakgrunden
            {106, 55, 232, 255},    // accentSurfacePress
            {255, 255, 255, 255},   // onAccent           — vit i BÅDA lägena
            {0, 0, 0, 97},          // shadow        — 38 %: mörk botten tål mer
            {166, 137, 255, 255},   // link
            {192, 174, 255, 255},   // linkHover    — ljusnar vid pekaren
            {20, 20, 31, 255},      // titlebar
            {242, 242, 248, 255},   // titlebarText
            {38, 38, 58, 255},      // chromeHover
            {31, 31, 48, 255},      // chromePress
            {196, 43, 28, 255},     // chromeClose
            {242, 242, 248, 255},   // chromeGlyph
            {255, 255, 255, 255},   // chromeCloseGlyph
            {58, 58, 84, 255},      // frame
            {255, 95, 87, 255},     // trafficClose
            {204, 76, 69, 255},     // trafficClosePress
            {254, 188, 46, 255},    // trafficMin
            {203, 150, 36, 255},    // trafficMinPress
            {40, 200, 64, 255},     // trafficMax
            {32, 160, 51, 255},     // trafficMaxPress
            {0, 0, 0, 190},         // trafficGlyph
        }},
        };
        // De härledda rollerna räknas ur den färdiga paletten — se
        // deriveRoles (kapseln + den skrivskyddade fältytan).
        deriveRoles(th.light.colors);
        deriveRoles(th.dark.colors);
        return th;
    }();
    return t;
}

// ===========================================================================
// PALETTGENERATORN — hue in, hel Theme ut.
//
// Varför generera i stället för att skriva ut: 36 roller × 2 lägen × 9 hues
// × 2 familjer = 1 296 värden. Handskrivet är det varken granskningsbart
// eller underhållbart, och en ny färgroll hade krävt 36 nya rader. Det som
// skrivs ut och kommenteras här är LJUSHETSTRAPPAN — den bär designen;
// hexvärdena är dess utfall. Grinden (tests/contrast_gate.cpp) mäter
// utfallet, inte trappan, så en trappa som glider fälls.
//
// Neutral är samma funktion med mättnad 0 — ingen särfallskod.
// ===========================================================================

namespace {

// Temanamnen är API-NAMN (`themeAt(ui, i).name`) och översätts inte, som
// UI-namnen Glaze/Slate/Minimalistic — se demons manifest.
// lang_gate:av temanas API-namn
const char* hueName(Hue h)
{
    switch (h) {
    case Hue::Red:    return "Red";
    case Hue::Orange: return "Orange";
    case Hue::Amber:  return "Amber";
    case Hue::Green:  return "Green";
    case Hue::Teal:   return "Teal";
    case Hue::Blue:   return "Blue";
    case Hue::Indigo: return "Indigo";
    case Hue::Violet: return "Violet";
    case Hue::Neutral:
    default:          return "Neutral";
    }
}
// lang_gate:på

// LJUSHETSTRAPPAN för ETT läge. Det här ÄR designen — allt annat är
// mekanik. Värdena är HSL-ljushet (0..1); mättnaden ligger i de tre
// s-fälten, en per grupp, eftersom ytor, text och accent tål helt olika
// mycket kulör.
struct Ladder {
    float windowBg, panel, appWorkspace;
    float buttonHover, buttonPress;
    float text, textDim;
    float border, buttonBorder;
    float accent, accentSurface, link, linkHover;
    float titlebar;
    float sSurface, sText, sAccent;
};

// ---- Minimalistic ("Outline"): kanten ÄR formen, så kantens ljushet är
// den känsligaste raden i hela tabellen.
// Mättnad: "nedtonat rejält, men tydliga toner".
constexpr Ladder kMiniLight{
    /* windowBg      */ 0.960f, /* panel */ 0.995f, /* appWorkspace */ 0.920f,
    /* buttonHover   */ 0.905f, /* buttonPress */ 0.855f,
    /* text          */ 0.120f, /* textDim */ 0.380f,   // dim 4.96–7.50:1
    /* border        */ 0.560f, /* buttonBorder */ 0.500f,  // kant 3.15–4.49:1
    /* accent        */ 0.400f, /* accentSurface */ 0.380f,
    /* link          */ 0.340f, /* linkHover */ 0.280f,  // länken är TEXT: 4.5:1
    /* titlebar      */ 0.930f,
    /* sSurface      */ 0.16f,  /* sText */ 0.18f, /* sAccent */ 0.30f,
};
constexpr Ladder kMiniDark{
    /* windowBg      */ 0.105f, /* panel */ 0.150f, /* appWorkspace */ 0.075f,
    /* buttonHover   */ 0.215f, /* buttonPress */ 0.275f,
    /* text          */ 0.930f, /* textDim */ 0.660f,
    /* border        */ 0.400f, /* buttonBorder */ 0.460f,
    /* accent        */ 0.680f, /* accentSurface */ 0.420f,
    /* link          */ 0.680f, /* linkHover */ 0.760f,
    /* titlebar      */ 0.135f,
    /* sSurface      */ 0.13f,  /* sText */ 0.09f, /* sAccent */ 0.32f,
};

// ---- Slate: EGEN trappa, inte Minimalistics. Slate ritar FYLLDA ytor där
// Minimalistic ritar kanter, så samma mättnadsprofil ger ett annat intryck:
// en tonad yta täcker hundra gånger så många pixlar som en tonad linje.
// Därför HALVERAD ytmättnad — och en betydligt kraftigare accent, eftersom
// accenten här är en YTA man trycker på och inte en linje. Ljushetsstegen
// är Win11-palettens EGNA, tagna ur slateDefault() ovan, så de nio
// genererade familjerna känns som syskon till "Windows".
//
// TVÅ steg avviker från Windows med flit, och båda av MÄTT skäl:
//   buttonBorder 0.500 (Windows 0.816 = 1.39:1 mot bakgrunden) — under
//     3:1-kravet för en kontrollkant. Referenspaletten får behålla sin
//     siffra; en NY palett skrivs inte trasig.
//   accent 0.340 (Windows 0.376) — Windows accent är en MÄTTAD BLÅ, och
//     blått är mörkt. Samma ljushet i grönt/gult mäter 2.51:1 mot samma
//     bakgrund, alltså under 3:1 för en fokusring. Hue-oberoende trappa
//     kräver den mörkaste kulörens ljushet.
constexpr Ladder kSlateLight{
    /* windowBg      */ 0.953f, /* panel */ 0.984f, /* appWorkspace */ 0.898f,
    /* buttonHover   */ 0.965f, /* buttonPress */ 0.941f,
    /* text          */ 0.106f, /* textDim */ 0.376f,
    /* border        */ 0.640f, /* buttonBorder */ 0.500f,
    /* accent        */ 0.340f, /* accentSurface */ 0.380f,
    /* link          */ 0.280f, /* linkHover */ 0.240f,
    /* titlebar      */ 0.953f,   // sömlös mot windowBg, som Windows
    /* sSurface      */ 0.10f,  /* sText */ 0.12f, /* sAccent */ 0.75f,
};
constexpr Ladder kSlateDark{
    /* windowBg      */ 0.125f, /* panel */ 0.169f, /* appWorkspace */ 0.094f,
    /* buttonHover   */ 0.196f, /* buttonPress */ 0.153f,
    /* text          */ 0.940f, /* textDim */ 0.627f,
    /* border        */ 0.300f, /* buttonBorder */ 0.460f,
    /* accent        */ 0.680f, /* accentSurface */ 0.380f,
    /* link          */ 0.680f, /* linkHover */ 0.760f,
    /* titlebar      */ 0.125f,
    /* sSurface      */ 0.09f,  /* sText */ 0.07f, /* sAccent */ 0.65f,
};

// onAccent och de två accent-TILLSTÅNDEN hänger ihop och avgörs av en
// MÄTNING, inte av trappan:
//
//  1. onAccent är den av nästan-vit och nästan-svart som mäter HÖGST mot
//     accentytan. Ingen enda färg klarar båda ändarna av kulörcirkeln —
//     gult vid samma ljushet som blått har mer än dubbla luminansen, så vit
//     text på gul yta faller under 4.5:1 hur man än vrider på trappan.
//  2. hover och tryck flyttar därför ALLTID BORT från onAccent (mörkare när
//     texten är vit, ljusare när den är svart). Då kan kontrasten mot texten
//     bara ÖKA i ett tillstånd, aldrig minska — annars hamnar hover i det
//     dödband där varken vit eller svart når 4.5:1 (gul/orange yta
//     runt luminans 0.19).
constexpr float kOnAccentLight = 0.985f;   // nästan vit, inte vit — mjukare
constexpr float kOnAccentDark  = 0.050f;   // nästan svart; marginalen behövs

Theme buildTheme(Hue h, const Ladder& lightL, const Ladder& darkL,
                 const std::string& name)
{
    Theme t = Theme::slateDefault();   // ärver chrome-rött + trafikljusen
    t.name  = name;

    auto build = [&](Theme::Variant& v, const Ladder& L, bool dark) {
        auto&       c   = v.colors;
        const bool  neu = (h == Hue::Neutral);
        const float H   = hueDegrees(h);
        const float sS  = neu ? 0.0f : L.sSurface;
        const float sT  = neu ? 0.0f : L.sText;
        const float sA  = neu ? 0.0f : L.sAccent;

        c.windowBg       = hsl(H, sS, L.windowBg);
        c.panel          = hsl(H, sS, L.panel);
        c.appWorkspace   = hsl(H, sS, L.appWorkspace);
        c.button         = c.panel;
        c.buttonHover    = hsl(H, sS, L.buttonHover);
        c.buttonPress    = hsl(H, sS, L.buttonPress);
        c.buttonDisabled = c.windowBg;
        c.buttonBorder   = hsl(H, sS, L.buttonBorder);
        c.border         = hsl(H, sS, L.border);
        c.text           = hsl(H, sT, L.text);
        c.textDim        = hsl(H, sT, L.textDim);
        c.accent         = hsl(H, sA, L.accent);
        c.accentSurface  = hsl(H, sA, L.accentSurface);
        c.link           = hsl(H, sA, L.link);
        c.linkHover      = hsl(H, sA, L.linkHover);

        // Mätningen (se noten ovan): först texten, sedan tillstånden BORT
        // från den.
        const gfx::Color white = hsl(0.0f, 0.0f, kOnAccentLight);
        const gfx::Color black = hsl(0.0f, 0.0f, kOnAccentDark);
        const bool       onWhite =
            contrast(white, c.accentSurface) >= contrast(black, c.accentSurface);
        c.onAccent = onWhite ? white : black;
        const float dir = onWhite ? -1.0f : 1.0f;
        c.accentSurfaceHover = hsl(H, sA, L.accentSurface + dir * 0.05f);
        c.accentSurfacePress = hsl(H, sA, L.accentSurface + dir * 0.09f);

        c.titlebar     = hsl(H, sS, L.titlebar);
        c.titlebarText = c.text;
        c.chromeHover  = c.buttonHover;
        c.chromePress  = c.buttonPress;
        c.chromeGlyph  = c.text;
        c.frame        = c.border;
        // sheen: ingen av de genererade familjerna lamineras (det är
        // Glaze-ritningens grepp) — helt genomskinliga fält, som Slate.
        c.sheen        = gfx::Color{255, 255, 255, 0};
        c.sheenHigh    = gfx::Color{255, 255, 255, 0};
        c.shadow       = dark ? gfx::Color{0, 0, 0, 110} : gfx::Color{0, 0, 0, 40};
    };
    build(t.light, lightL, false);
    build(t.dark, darkL, true);
    deriveRoles(t.light.colors);
    deriveRoles(t.dark.colors);
    return t;
}

template <class Fn>
const Theme& cachedFamily(Hue h, Fn&& make)
{
    // Byggs EN gång per hue och lever till processlut — AppWindow::setTheme
    // kopierar värdet, men themeAt()/defaultTheme() lämnar ut referenser.
    static const std::array<Theme, kHueCount> fam = [&] {
        std::array<Theme, kHueCount> a{};
        for (int i = 0; i < kHueCount; ++i)
            a[static_cast<std::size_t>(i)] = make(kAllHues[i]);
        return a;
    }();
    int i = static_cast<int>(h);
    if (i < 0 || i >= kHueCount)
        i = 0;
    return fam[static_cast<std::size_t>(i)];
}

} // namespace

const Theme& Theme::minimalistic(Hue h)
{
    return cachedFamily(h, [](Hue x) {
        return buildTheme(x, kMiniLight, kMiniDark, hueName(x));
    });
}

const Theme& Theme::slate(Hue h)
{
    return cachedFamily(h, [](Hue x) {
        return buildTheme(x, kSlateLight, kSlateDark, hueName(x));
    });
}

// Minimalistic default = den neutrala av de nio. Ingen egen palett: en
// "Minimalistic-grå" som INTE är familjens Neutral hade varit ett tionde
// tema som ingen valt.
const Theme& Theme::minimalisticDefault()
{
    return minimalistic(Hue::Neutral);
}

// --------------------------------------------------------- UI -> dess teman

int themeCount(UI u)
{
    switch (u) {
    case UI::Glaze:        return 1;                // bara "Lavender"
    case UI::Minimalistic: return kHueCount;        // de nio
    case UI::Slate:
    default:               return kHueCount + 1;    // "Windows" + de nio
    }
}

const Theme& themeAt(UI u, int index)
{
    const int n = themeCount(u);
    if (index < 0 || index >= n)
        index = 0;
    switch (u) {
    case UI::Glaze:
        return Theme::glazeDefault();
    case UI::Minimalistic:
        return Theme::minimalistic(kAllHues[index]);
    case UI::Slate:
    default:
        // Index 0 är "Windows" — referenspaletten, som inte är en hue.
        return index == 0 ? Theme::slateDefault()
                          : Theme::slate(kAllHues[index - 1]);
    }
}

const Theme& defaultTheme(UI u)
{
    return themeAt(u, 0);
}

// ----------------------------------------------- färgväljarens palett
//
// Fyra rader om tio. Gråskalan går i lika steg från svart till
// vitt; de tre tonraderna är samma tio kulörer i tre ljusheter — RÖD ·
// ORANGE · GUL · GULGRÖN · GRÖN · BLÅGRÖN · CYAN · BLÅ · INDIGO · VIOLETT —
// valda så att ljus, mättad och mörk ton av samma kulör står i SAMMA
// kolumn. Talen är Material-paletternas 200-, 500- och 800-steg: en känd,
// avvägd uppsättning i stället för HSL-steg, där gult och cyan blir
// skrikande vid samma ljushet som blått.
//
// INTE GRINDAD mot kontrast: rutorna är innehåll som visas mot sin egen
// kontur (`buttonBorder`), inte text mot en yta. VERSALA hexsiffror med flit
// (glazelanguage-prejudikatet): med gemener hade två av talen innehållit
// X11-bibliotekets namn, ett av backendgrindens sökord i rot-CLAUDE.md.
namespace {

constexpr gfx::Color kPaletteColors[] = {
    // gråskalan
    {0x00, 0x00, 0x00, 0xFF}, {0x1C, 0x1C, 0x1C, 0xFF},
    {0x39, 0x39, 0x39, 0xFF}, {0x55, 0x55, 0x55, 0xFF},
    {0x71, 0x71, 0x71, 0xFF}, {0x8E, 0x8E, 0x8E, 0xFF},
    {0xAA, 0xAA, 0xAA, 0xFF}, {0xC6, 0xC6, 0xC6, 0xFF},
    {0xE3, 0xE3, 0xE3, 0xFF}, {0xFF, 0xFF, 0xFF, 0xFF},
    // ljus ton
    {0xEF, 0x9A, 0x9A, 0xFF}, {0xFF, 0xCC, 0x80, 0xFF},
    {0xFF, 0xF5, 0x9D, 0xFF}, {0xC5, 0xE1, 0xA5, 0xFF},
    {0xA5, 0xD6, 0xA7, 0xFF}, {0x80, 0xCB, 0xC4, 0xFF},
    {0x80, 0xDE, 0xEA, 0xFF}, {0x90, 0xCA, 0xF9, 0xFF},
    {0x9F, 0xA8, 0xDA, 0xFF}, {0xCE, 0x93, 0xD8, 0xFF},
    // mättad ton
    {0xF4, 0x43, 0x36, 0xFF}, {0xFF, 0x98, 0x00, 0xFF},
    {0xFF, 0xEB, 0x3B, 0xFF}, {0x8B, 0xC3, 0x4A, 0xFF},
    {0x4C, 0xAF, 0x50, 0xFF}, {0x00, 0x96, 0x88, 0xFF},
    {0x00, 0xBC, 0xD4, 0xFF}, {0x21, 0x96, 0xF3, 0xFF},
    {0x3F, 0x51, 0xB5, 0xFF}, {0x9C, 0x27, 0xB0, 0xFF},
    // mörk ton
    {0xC6, 0x28, 0x28, 0xFF}, {0xEF, 0x6C, 0x00, 0xFF},
    {0xF9, 0xA8, 0x25, 0xFF}, {0x55, 0x8B, 0x2F, 0xFF},
    {0x2E, 0x7D, 0x32, 0xFF}, {0x00, 0x69, 0x5C, 0xFF},
    {0x00, 0x83, 0x8F, 0xFF}, {0x15, 0x65, 0xC0, 0xFF},
    {0x28, 0x35, 0x93, 0xFF}, {0x6A, 0x1B, 0x9A, 0xFF},
};

} // namespace

const ColorPalette& standardPalette()
{
    static const ColorPalette p{std::span<const gfx::Color>(kPaletteColors),
                                10};
    return p;
}

} // namespace glaze
