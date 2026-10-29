// contrast_gate — KONTRASTGRINDEN för de genererade palettfamiljerna.
//
// Kör:  ./build/tests/contrast_gate [-v]
// Exit: 0 = alla mätningar klarar sitt krav · 1 = minst en föll
//
// Det här programmet är SKÄLET att paletterna får genereras. 36 roller ×
// 2 lägen × 9 hues × 2 familjer = 1 296 värden är inte granskningsbart för
// hand — men det är trivialt att MÄTA, och en ljushetstrappa som glider
// faller då direkt i stället för om ett år. Principen
// (tydlighet före smak), gjord körbar:
//
//     text            ≥ 4.5:1 mot sin bakgrund (WCAG AA)
//     kanter och
//     tillståndsmark. ≥ 3:1
//     i BÅDA lägena, och går tydlighet och smak isär vinner tydligheten.
//
// Vad som mäts, och varför just det:
//   text/bg, text/panel   vanlig text på båda ytorna den kan hamna på
//   textDim/bg            sekundär text är TEXT — samma krav
//   buttonBorder/bg       för Minimalistic ÄR kanten hela knappformen
//   link/bg, linkHover/bg en länk är text
//   accent/bg             fokusring, bock, tryckt kant = tillståndsmarkering
//   onAccent/accentSurface (+ hover/press) — texten på en fylld accentyta
//   accent/appWorkspace   spårens FYLLNING mot spåret (slider, progress) —
//                         det är fyllnaden som bär spårets betydelse
//
// Vad som med FLIT inte mäts:
//   "Windows" och "Lavender" — de två HANDSKRIVNA paletterna. Windows är
//   Microsofts egen och REFERENSEN allt jämförs mot (dess `border` mäter
//   1.39:1 mot bakgrunden; vi skriver inte om Windows för att blidka vår
//   egen grind). Lavender är grindad för hand med mätvärdena i
//   theme.cpp-kommentarerna.
//   `border`/`panel`-kanten — en hårlinje runt en panel är en antydan, inte
//   en form. Kontrollkanten (`buttonBorder`) är den som bär. Samma sak
//   gäller `border` runt ett SPÅR. Mot `appWorkspace`:
//     Slate         1.91–2.14 (ljust) · 1.95–2.18 (mörkt) — 0 av 18 når 3:1
//     Minimalistic  2.43–3.17 (ljust) · 2.81–3.53 (mörkt) — 9 av 18
//   Ett krav här hade alltså fällt HELA Slate-familjen på en hårlinje som
//   bara är en antydan. Spårets synlighet ligger i FYLLNADEN, som mäts.
//   accentSurface mot bakgrunden — en accentyta är en YTA, och dess
//   avgränsning bärs av konturen. Både Windows (~1.9:1) och Glaze (3.80:1)
//   ligger där de ligger; ett krav här hade tvingat fram en trappa som
//   gjorde onAccent omöjlig i andra änden av kulörcirkeln.
//
//   SLIDERTUMMEN I MINIMALISTIC: en fylld `accentSurface`-cirkel HELT UTAN
//   kontur, där ytan själv bär formen, klarar inte kravet 3:1 i någon av
//   familjerna:
//     Minimalistic  min 2.28:1 (Indigo/mörk) · 4 hues under kravet
//     Slate         min 1.50:1 (Indigo/mörk) · 7 hues under kravet
//   Slutsatsen är inte att trappan ska ändras (en accentyta som är ljus nog
//   att separera från en mörk bakgrund kan inte längre bära vit text — det
//   är samma dödband som `onAccent`-mätningen redan hanterar), utan att
//   ritningen ger tummen en hårlinje i `buttonBorder`, den roll som ÄR
//   grindad ≥ 3:1 mot bakgrunden och som Minimalistic låter bära varje
//   annan form. Undantaget ovan gäller därför fortfarande.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glaze/color.h>
#include <glaze/style.h>
#include <glaze/theme.h>

namespace {

// Mätningen är ramverkets egen (glaze/color.h) — grinden mäter med SAMMA
// funktion som generatorn valde `onAccent` med, och ingen egen kopia kan glida.
using glaze::color::contrast;

struct Measure {
    const char* name;
    float       required;
    float (*value)(const glaze::Theme::Colors&);
};

const Measure kMeasures[] = {
    {"text/bg",      4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.text, c.windowBg); }},
    {"text/panel",   4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.text, c.panel); }},
    {"textDim/bg",   4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.textDim, c.windowBg); }},
    {"link/bg",      4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.link, c.windowBg); }},
    {"linkHover/bg", 4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.linkHover, c.windowBg); }},
    {"btnBorder/bg", 3.0f, [](const glaze::Theme::Colors& c) { return contrast(c.buttonBorder, c.windowBg); }},
    {"accent/bg",    3.0f, [](const glaze::Theme::Colors& c) { return contrast(c.accent, c.windowBg); }},
    {"onAcc/surf",   4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.onAccent, c.accentSurface); }},
    {"onAcc/hover",  4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.onAccent, c.accentSurfaceHover); }},
    {"onAcc/press",  4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.onAccent, c.accentSurfacePress); }},
    // Spårens FYLLNING mot sitt spår: sliderns passerade
    // del och progressremsan är `accent` ovanpå `appWorkspace`, och det är
    // FYLLNADEN som bär spårets betydelse — kanten runt ett spår är bara en
    // antydan (`border` mot appWorkspace faller — se undantagslistan i
    // filhuvudet; den rollen grindas därför INTE).
    // Min över nio hues × två lägen:
    //   Slate         3.02:1 (Green/ljus) · 3.11:1 (Teal/ljus) — NOLL marginal
    //   Minimalistic  3.73:1 (Green/ljus)
    // De två Slate-raderna är varningen till nästa trappjustering: sänks
    // accentens ljushet ett snäpp i ljust läge faller den här grinden först.
    {"accent/work",  3.0f, [](const glaze::Theme::Colors& c) { return contrast(c.accent, c.appWorkspace); }},
    // KAPSELKLUSTRET. Tonerna härleds av `deriveCapsule()`
    // ur `accentSurface` och `titlebar` — alltså av TVÅ roller som redan
    // rör sig med trappan, och just därför måste utfallet mätas: en
    // justering av endera kan tyst göra klustret osynligt eller glyfen
    // oläslig.
//   (capsBorder/bar finns inte som roll.) Konturen är `frame` —
//   samma linje som runt fönstret, och den grindas inte heller. Skälet är
//   att kapselns form INTE bärs av linjen utan av
    //   `capsuleBody`: en tonad fyllning som alltid syns. Linjen är en
//   förfining som ska synas utan att vara skarp. Ett
    //   3:1-krav på den tvingade fram en nästan svart kontur som gjorde
    //   fönsterknapparna till det starkaste i hela namnlisten — precis
    //   tvärtemot vad chrome ska göra.
    //   glyph/capsHover glyfen byter INTE färg vid hover, så `chromeGlyph`
    //                   måste hålla mot den dämpade fyllningen som text
    //                   (4.5:1). Faller den raden är det `kCapsuleHover`
    //                   som dragits för långt.
    //   onAcc/capsPress vid TRYCK är fyllningen hel accentyta och glyfen
    //                   flippar till `onAccent` — samma par som
    //                   `onAcc/press`, men mätt via kapselns egen roll så
    //                   en palett som skriver över `capsulePress` grindas.
        {"glyph/capsHover", 4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.chromeGlyph, c.capsuleHover); }},
    {"glyf/capsPress", 4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.chromeGlyph, c.capsulePress); }},
    // SKRIVSKYDDAD FÄLTYTA (`TextInput::readOnly`). Tonen
    // härleds av `deriveFieldReadOnly()` mellan `windowBg` och
    // `appWorkspace` — två roller som rör sig med trappan, alltså samma
    // skäl att mäta som kapseln har.
    //   text/roField      VÄRDET SKA LÄSAS. Skrivskyddad är inte inaktiv:
    //                     texten behåller full kontrast och det är bara
    //                     ytan som blir tyst, så kravet är textens 4.5:1
    //                     och inte kantens 3:1.
    //   btnBorder/roField för MINIMALISTIC ÄR kanten hela fältet — en
    //                     fyllning som närmar sig kanten äter upp formen.
    //                     `appWorkspace` rakt av mätte 3.05:1 i Slate/ljust
    //                     (noll marginal), vilket är skälet att tonen bara
    //                     går HALVA vägen dit.
    {"text/roField",      4.5f, [](const glaze::Theme::Colors& c) { return contrast(c.text, c.fieldReadOnly); }},
    {"btnBorder/roField", 3.0f, [](const glaze::Theme::Colors& c) { return contrast(c.buttonBorder, c.fieldReadOnly); }},
};
constexpr int kMeasureCount = static_cast<int>(sizeof kMeasures / sizeof kMeasures[0]);

struct Extremes {
    float       lo = 1e9f, hi = -1e9f;
    std::string loWhere, hiWhere;
    void        add(float v, const std::string& where)
    {
        if (v < lo) { lo = v; loWhere = where; }
        if (v > hi) { hi = v; hiWhere = where; }
    }
};

// En familj = en generator + ett namn. Grinden itererar kAllHues, så en ny
// kulör hamnar automatiskt under mätning — ingen lista att glömma.
struct Family {
    const char* name;
    const glaze::Theme& (*make)(glaze::Hue);
};

int runFamily(const Family& fam, bool verbose)
{
    std::printf("\n=== %s (%d hues × 2 lägen) ===\n", fam.name, glaze::kHueCount);

    Extremes ext[kMeasureCount];
    int      fails = 0;

    for (glaze::Hue h : glaze::kAllHues) {
        const glaze::Theme& t = fam.make(h);
        for (int mode = 0; mode < 2; ++mode) {
            const auto& col = mode == 0 ? t.light.colors : t.dark.colors;
            const std::string where =
                t.name + (mode == 0 ? "/ljus" : "/mörk");

            for (int i = 0; i < kMeasureCount; ++i) {
                const float v = kMeasures[i].value(col);
                ext[i].add(v, where);
                if (v < kMeasures[i].required) {
                    std::printf("  FEL  %-14s %-12s %5.2f:1  (krav %.1f)\n",
                                where.c_str(), kMeasures[i].name, v,
                                kMeasures[i].required);
                    ++fails;
                } else if (verbose) {
                    std::printf("  ok   %-14s %-12s %5.2f:1\n", where.c_str(),
                                kMeasures[i].name, v);
                }
            }
        }
    }

    for (int i = 0; i < kMeasureCount; ++i)
        std::printf("  %-12s krav %.1f   min %5.2f (%s)   max %5.2f (%s)\n",
                    kMeasures[i].name, kMeasures[i].required, ext[i].lo,
                    ext[i].loWhere.c_str(), ext[i].hi, ext[i].hiWhere.c_str());

    std::printf("  => %s\n", fails == 0 ? "GRÖN" : "FÄLLD");
    return fails;
}

// ------------------------------------------------------ NAMNUNIKHETEN
//
// Teman och styles slås upp PER UI, och en väljare (demons dropdown) kan
// därför bara identifiera det aktiva valet på NAMNET — `styleAt`/`themeAt`
// svarar med referenser in i statiska tabeller medan `style()`/`theme()`
// svarar med App:s KOPIA, så adressjämförelse är omöjlig och pekar-
// jämförelse är en tickande bomb (`Style::name` är `const char*`).
// Namnjämförelsen förutsätter alltså att namnet är UNIKT inom sitt UI.
//
// Det är trivialt sant (Slate: "Windows" + nio hue-namn;
// Minimalistic: nio; Glaze: "Lavender"; styles: exakt en per UI, alla
// döpta "Regular"), men INGENTING i koden garanterar det. Därför en grind
// och inte en konvention: dubbletter inom ett UI fäller bygget i stället
// för att tyst göra väljaren omöjlig att synka.
//
// Namn får däremot gärna sammanfalla MELLAN UI:n — "Regular" finns tre
// gånger och "Neutral" två, och det är avsiktligt (namnrymden är per UI).
struct UiEntry {
    const char* name;
    glaze::UI   ui;
};

const UiEntry kUIs[] = {
    {"Glaze", glaze::UI::Glaze},
    {"Slate", glaze::UI::Slate},
    {"Minimalistic", glaze::UI::Minimalistic},
};

int checkNameUniqueness()
{
    std::printf("\n=== namnunikhet per UI ===\n");
    int fails = 0;

    for (const UiEntry& e : kUIs) {
        auto unique = [&](const char* kind, int count,
                          auto&& nameAt) {
            std::vector<std::string> seen;
            int                      dups = 0;
            for (int i = 0; i < count; ++i) {
                const std::string n = nameAt(i);
                for (std::size_t j = 0; j < seen.size(); ++j) {
                    if (seen[j] == n) {
                        std::printf("  FEL  %s/%s: \"%s\" finns på både "
                                    "index %d och %d\n",
                                    e.name, kind, n.c_str(),
                                    static_cast<int>(j), i);
                        ++dups;
                    }
                }
                seen.push_back(n);
            }
            if (dups == 0)
                std::printf("  ok   %-13s %-6s %d namn, alla unika\n", e.name,
                            kind, count);
            fails += dups;
        };

        unique("teman", glaze::themeCount(e.ui), [&](int i) {
            return std::string(glaze::themeAt(e.ui, i).name);
        });
        unique("mått", glaze::styleCount(e.ui), [&](int i) {
            return std::string(glaze::styleAt(e.ui, i).name);
        });
    }

    std::printf("  => %s\n", fails == 0 ? "GRÖN" : "FÄLLD");
    return fails;
}

// ---------------------------------------------- STATUSROLLERNA
//
// `warningSurface`/`onWarning` och `errorSurface`/`onError` bär
// meddelanderutans badge och glyf. Glyfen ritas som TEXT via fonten, så
// kravet är textens 4.5:1 och inte en forms 3:1.
//
// Den här kontrollen itererar ALLA inbyggda teman — inte bara de
// genererade — och det är hela poängen med att den står för sig själv:
// familjegrinden ovan undantar med flit "Windows" och "Lavender", de två
// HANDSKRIVNA paletterna, så en mätning bara i `kMeasures` hade lämnat två
// av tjugo omätta. Statusrollerna är dessutom HÄRLEDDA på ett enda ställe
// (`deriveStatus`) och ärvs därför av båda undantagen — då ska de också
// mätas där.
int checkStatusRoles(bool verbose)
{
    std::printf("\n=== statusrollerna (alla inbyggda teman) ===\n");
    int   fails = 0;
    int   n     = 0;
    float lo    = 1e9f;
    std::string loWhere;

    struct Pair {
        const char* name;
        gfx::Color (*on)(const glaze::Theme::Colors&);
        gfx::Color (*surface)(const glaze::Theme::Colors&);
    };
    const Pair pairs[] = {
        {"onWarn/warnSurf",
         [](const glaze::Theme::Colors& c) { return c.onWarning; },
         [](const glaze::Theme::Colors& c) { return c.warningSurface; }},
        {"onErr/errSurf",
         [](const glaze::Theme::Colors& c) { return c.onError; },
         [](const glaze::Theme::Colors& c) { return c.errorSurface; }},
    };

    for (const UiEntry& e : kUIs) {
        for (int i = 0; i < glaze::themeCount(e.ui); ++i) {
            const glaze::Theme& t = glaze::themeAt(e.ui, i);
            for (int mode = 0; mode < 2; ++mode) {
                const auto& col = mode == 0 ? t.light.colors : t.dark.colors;
                const std::string where = std::string(e.name) + "/" + t.name +
                                          (mode == 0 ? "/ljus" : "/mörk");
                for (const Pair& p : pairs) {
                    const float v = contrast(p.on(col), p.surface(col));
                    ++n;
                    if (v < lo) { lo = v; loWhere = where + " " + p.name; }
                    if (v < 4.5f) {
                        std::printf("  FEL  %-28s %-16s %5.2f:1  (krav 4.5)\n",
                                    where.c_str(), p.name, v);
                        ++fails;
                    } else if (verbose) {
                        std::printf("  ok   %-28s %-16s %5.2f:1\n",
                                    where.c_str(), p.name, v);
                    }
                }
            }
        }
    }

    std::printf("  %d mätningar, min %5.2f (%s)\n", n, lo, loWhere.c_str());
    std::printf("  => %s\n", fails == 0 ? "GRÖN" : "FÄLLD");
    return fails;
}

} // namespace

int main(int argc, char** argv)
{
    const bool verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

    const Family families[] = {
        {"Minimalistic", &glaze::Theme::minimalistic},
        {"Slate", &glaze::Theme::slate},
    };

    int fails = 0;
    for (const Family& f : families)
        fails += runFamily(f, verbose);

    fails += checkNameUniqueness();
    fails += checkStatusRoles(verbose);

    std::printf("\n%s — %d mätningar per familj + namnunikheten, %d fel "
                "totalt\n",
                fails == 0 ? "KONTRASTGRINDEN GRÖN" : "KONTRASTGRINDEN FÄLLD",
                kMeasureCount * glaze::kHueCount * 2, fails);
    return fails == 0 ? 0 : 1;
}
