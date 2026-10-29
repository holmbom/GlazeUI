// design_theme — designtaggens avkodning (se .h). Kompileras av BÅDE
// GlazeBuilder och glaze-render: den beror på `glaze` och hör därför inte
// hemma i rena builder_core (samma precedens som palette.cpp).

#include "design_theme.h"

#include <cstdlib>
#include <vector>

namespace bld {
namespace {

// "Slate" → UI. Namnen är enumets egna — ett okänt namn är hellre en
// loggad miss än en gissning.
bool uiByName(const std::string& v, glaze::UI& out)
{
    if (v == "Glaze")        { out = glaze::UI::Glaze;        return true; }
    if (v == "Slate")        { out = glaze::UI::Slate;        return true; }
    if (v == "Minimalistic") { out = glaze::UI::Minimalistic; return true; }
    return false;
}

const char* uiName(glaze::UI u)
{
    switch (u) {
    case glaze::UI::Glaze:        return "Glaze";
    case glaze::UI::Minimalistic: return "Minimalistic";
    case glaze::UI::Slate:
    default:                      return "Slate";
    }
}

// "slate" → UI::Slate — FAMILJEN i temavärdet `familj(Namn)`. Skild från
// `ui=`: temats namnrymd hör till sin egen familj, och taggen får därför
// (medvetet) bära ett Slate-tema på Minimalistics form. Namnrymden hålls
// isär i UPPSLAGET, inte av en vakt.
bool familyByName(const std::string& v, glaze::UI& out)
{
    if (v == "glaze")        { out = glaze::UI::Glaze;        return true; }
    if (v == "slate")        { out = glaze::UI::Slate;        return true; }
    if (v == "minimalistic") { out = glaze::UI::Minimalistic; return true; }
    return false;
}

// "slate(Teal)" → Theme. Uppslaget går via themeAt(ui, i) — EN sanning,
// samma lista som defaultTheme och demons dropdown läser.
const glaze::Theme* themeByValue(const std::string& v)
{
    const size_t par = v.find('(');
    if (par == std::string::npos || v.empty() || v.back() != ')')
        return nullptr;
    glaze::UI fam = glaze::UI::Slate;
    if (!familyByName(v.substr(0, par), fam))
        return nullptr;
    const std::string namn = v.substr(par + 1, v.size() - par - 2);
    for (int i = 0; i < glaze::themeCount(fam); ++i) {
        const glaze::Theme& t = glaze::themeAt(fam, i);
        if (t.name == namn)
            return &t;
    }
    return nullptr;
}

// Hela värdet måste vara ett heltal — "0x" hade annars tyst blivit 0.
bool intByValue(const std::string& v, int& out)
{
    if (v.empty())
        return false;
    char*           slut = nullptr;
    const long long n    = std::strtoll(v.c_str(), &slut, 10);
    if (slut != v.c_str() + v.size())
        return false;
    out = static_cast<int>(n);
    return true;
}

} // namespace

DesignTag decodeDesignLine(const std::string& designLine)
{
    DesignTag tag;
    if (designLine.empty())
        return tag;

    std::vector<DesignPair> par;
    if (!parseDesignLine(designLine, par)) {
        gfx::log("designtagg: trasig rad '%s' — ingen axel sätts (raden "
                 "bevaras)", designLine.c_str());
        return tag;
    }

    for (const DesignPair& p : par) {
        if (p.key == "ui") {
            if (uiByName(p.value, tag.ui))
                tag.hasUI = true;
            else
                gfx::log("designtagg: okänd ui '%s' — ignoreras",
                         p.value.c_str());
        } else if (p.key == "theme") {
            if (const glaze::Theme* t = themeByValue(p.value)) {
                tag.theme    = t;
                tag.hasTheme = true;
            } else {
                gfx::log("designtagg: okänt theme '%s' (formen är "
                         "familj(Namn)) — ignoreras", p.value.c_str());
            }
        } else if (p.key == "style") {
            int i = 0;
            if (!intByValue(p.value, i)) {
                gfx::log("designtagg: style '%s' är inget heltal — ignoreras",
                         p.value.c_str());
            } else {
                tag.styleIndex = i;
                tag.hasStyle   = true;
            }
        } else if (p.key == "lang") {
            // SPRÅKET är ritbordets fjärde axel . Ingen
            // NY SATSFORM behövdes: driftvalet är appkod (`setLanguage` i
            // `onLoad`), precis som `setUI` — en genererad
            // `setLanguage("fi");` i `initializeComponent` vore
            // per-fönster-språk bakvägen, vilket är förkastat.
            // Taggen beskriver RITBORDET.
            //
            // Värdet valideras INTE här: den enda som kan svara om en
            // tagg finns är språkbutiken, och den frågas i
            // `applyDesignTag` — där en okänd tagg loggas och axeln
            // lämnas osatt.
            tag.lang    = p.value;
            tag.hasLang = true;
        } else {
            // Okänd nyckel: ignoreras vid avkodning men SKRIVS TILLBAKA
            // (regel 4 — en nyare builder får inte se sin metadata raderad
            // av en äldre).
            gfx::log("designtagg: okänd nyckel '%s' — ignoreras (bevaras)",
                     p.key.c_str());
        }
    }
    return tag;
}

void applyDesignTag(glaze::AppWindow& w, const DesignTag& tag)
{
    // UI FÖRST — setUI återställer theme och style till familjens defaults.
    if (tag.hasUI)
        w.setUI(tag.ui);
    if (tag.hasTheme)
        w.setTheme(*tag.theme);
    if (tag.hasStyle) {
        // styleAt klampar tyst till 0. En tyst klampning döljer gammal eller
        // skadad metadata, så vi loggar INNAN vi låter den ske.
        const int n = glaze::styleCount(w.ui());
        if (tag.styleIndex < 0 || tag.styleIndex >= n)
            gfx::log("designtagg: style=%d utanför 0..%d för %s — klampas",
                     tag.styleIndex, n - 1, uiName(w.ui()));
        w.setStyle(glaze::styleAt(w.ui(), tag.styleIndex));
    }
    // SPRÅKET SIST i kedjan: det är APP-GLOBALT som de
    // tre andra axlarna, och `setLanguage` loggar själv vid okänd tagg
    // och lämnar då språket oförändrat — axeln blir osatt, precis som en
    // odekodbar `ui=`.
    if (tag.hasLang)
        glaze::setLanguage(tag.lang);
}

std::string describeDesign(const glaze::AppWindow& w,
                           const std::string& designLine)
{
    // Temats FAMILJ skrivs inte ut: taggen får bära ett tema ur en annan
    // familj än `ui`, och en påhittad familjeetikett hade ljugit om vilket
    // uppslag som faktiskt gjordes. Namnet + UI:t är vad som gäller.
    const std::string vad = std::string(uiName(w.ui())) + " / " +
                            w.theme().name + " / " + w.style().name;
    return designLine.empty() ? "utan designtagg — verktygets " + vad
                              : "designtema " + vad;
}

} // namespace bld
