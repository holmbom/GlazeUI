#include "icon_window.h"

#include <string>
#include <utility>

// APPENS GLYFER — konturen och färgen i standardikonernas språk, i en ruta på
// 24 × 24 med rad 0 överst och ett lager per `|`. Varje lager läggs på det
// under med sitt kontrastkrav, så en detalj (plustecknet på skivan, texten på
// skylten) syns mot det den ligger på i båda lägena.

// En egen DEL: förbudsskylten, som `@skylt` i konturen och `#skylt` i färgen.
GLAZE_REGISTER_ICON_PART("skylt",
    "M 8.3 3 H 15.7 L 21 8.3 V 15.7 L 15.7 21 H 8.3 L 3 15.7 V 8.3 Z",
    "all deep red gradient red F M 8.3 3 H 15.7 L 21 8.3 V 15.7 L 15.7 21 H 8.3 "
    "L 3 15.7 V 8.3 Z")

// Den gula skylten: texten är UTSTANSAD ur skylten och visar plattan under,
// som är mörk i båda lägena (en fyllning, `over`, och inte konturens ton,
// som speglas i mörkt läge) och lite högre än texten.
GLAZE_REGISTER_ICON("Demo",
    "B 3 8 18 8 0 | R 1.5 5.5 21 13 2 K 3 9.5 18 5 DEMO",
    "over ink yellow B 3 8 18 8 0 | all deep yellow gradient yellow B 1.5 5.5 21 13 2")

// Ramverkets delar: sidan (`@fil`, `#fil`) med en grön skiva och ett plus.
GLAZE_REGISTER_ICON("NyRitning",
    "@fil | C 17 17 5 | M 17 14.5 V 19.5 M 14.5 17 H 19.5",
    "#fil | all deep green gradient green D 17 17 5 | all paper")

// Sidan med textrader och en röd etikett med utstansad text.
GLAZE_REGISTER_ICON("Rapport",
    "@fil | @ftext B 4 12 16 8 1.5 K 4.5 13.5 15 5 RAP",
    "#fil | all deep red over dark red B 4 12 16 8 1.5")

// Två bågar med pilspetsar — bara linjer, som får linjens krav.
GLAZE_REGISTER_ICON("Synka",
    "A 12 12 8 200 340 M 19.5 4.5 V 9.3 H 14.7 "
    "A 12 12 8 20 160 M 4.5 19.5 V 14.7 H 9.3",
    "all green")

// Linjalen: ytan i en gradient, kanten och skalstrecken i en djup ton.
GLAZE_REGISTER_ICON("Mat",
    "R 2 8 20 8 1 M 6 8 V 11 M 10 8 V 12 M 14 8 V 11 M 18 8 V 12",
    "all deep amber gradient amber B 2 8 20 8 1")

// Tre lager i tre toner — vart och ett läggs på det under.
GLAZE_REGISTER_ICON("Lager",
    "M 12 13 L 21 17 L 12 21 L 3 17 Z | M 12 8 L 21 12 L 12 16 L 3 12 Z | "
    "M 12 3 L 21 7 L 12 11 L 3 7 Z",
    "all deep blue gradient dark blue F M 12 13 L 21 17 L 12 21 L 3 17 Z | "
    "all deep blue gradient blue F M 12 8 L 21 12 L 12 16 L 3 12 Z | "
    "all deep blue gradient light blue F M 12 3 L 21 7 L 12 11 L 3 7 Z")

// Klockan och en röd prick.
GLAZE_REGISTER_ICON("Larm",
    "M 6 17 V 11 Q 6 5 12 5 Q 18 5 18 11 V 17 H 20 V 19 H 4 V 17 Z M 10 21.5 H 14 | "
    "C 18 5.5 3",
    "all deep amber gradient amber F M 6 17 V 11 Q 6 5 12 5 Q 18 5 18 11 V 17 H 20 "
    "V 19 H 4 V 17 Z | all deep red gradient red D 18 5.5 3")

// Den gröna skivan med en fylld triangel.
GLAZE_REGISTER_ICON("Kor",
    "C 12 12 10 | F M 10 7.5 L 16.5 12 L 10 16.5 Z",
    "all deep green gradient green D 12 12 10 | all paper")

// Appens egen del och en vit stav.
GLAZE_REGISTER_ICON("Stoppa",
    "@skylt | B 7 10.5 10 3 0",
    "#skylt | all paper")

namespace {

glaze::ImageSource glyf(const char* namn, int storlek)
{
    return glaze::drawnIcon(namn, storlek, glaze::IconStyle::Auto);
}

// Glyfernas namn och knapparnas text.
constexpr std::pair<const char*, const char*> kGlyfer[] = {
    {"NyRitning", "Ny ritning"}, {"Rapport", "Rapport"}, {"Synka", "Synka"},
    {"Mat", "Mät"},              {"Lager", "Lager"},     {"Larm", "Larm"},
    {"Kor", "Kör"},              {"Stoppa", "Stoppa"},
};

} // namespace

IconWindow::IconWindow()
{
    setUI(glaze::UI::Slate);
    setTitle("Egna glyfer");
    setContentSize(620, 360);
    setMinimumContentSize(520, 360);
    setStartPosition(glaze::StartPosition::CenterScreen);
    appIcon.setIcon(glaze::drawnIcon("Demo", 48, glaze::IconStyle::Color));

    // LÄGET, en glyf i namnlisten: solen i mörkt, månen i ljust.
    auto* lage = topBar.addEnd<glaze::ToolButton>();
    lage->name = "lage";
    lage->setGlyphOnly(true);
    lage->setImg(glaze::stockIcon(themeMode() == glaze::ThemeMode::Dark
                                      ? glaze::StockIcon::ThemeLight
                                      : glaze::StockIcon::ThemeDark));
    lage->setToolTip("Växla mellan ljust och mörkt");
    lage->onClick = [this, lage] {
        const bool mork = themeMode() != glaze::ThemeMode::Dark;
        setThemeMode(mork ? glaze::ThemeMode::Dark : glaze::ThemeMode::Light);
        lage->setImg(glaze::stockIcon(mork ? glaze::StockIcon::ThemeLight
                                           : glaze::StockIcon::ThemeDark));
    };

    // VERKTYGSFÄLTET — bara glyferna.
    auto* falt = add<glaze::ToolBar>();
    falt->name = "falt";
    falt->dock = glaze::Dock::Top;
    for (const auto& [namn, text] : kGlyfer) {
        auto* b = falt->add<glaze::ToolButton>();
        b->name = std::string("falt") + namn;
        b->setGlyphOnly(true);
        b->setImg(glyf(namn, 16));
        b->setToolTip(text);
        knappar_.push_back(b);
    }

    auto rubrik = [this](const char* text, const std::string& namn,
                         const std::string& over, float gap) {
        auto* l = add<glaze::Label>(text);
        l->name = namn;
        l->x    = 24;
        if (over.empty())
            l->y = 20;
        else
            l->anchorTop = {over, gap};
        return l;
    };

    // KNAPPAR MED TEXT — ikonen i 16 px till vänster.
    rubrik("Knappar med text, 16 px", "rubrik16", "", 0);
    std::string forra;
    for (const int i : {0, 2, 6, 7}) {
        const auto& [namn, text] = kGlyfer[i];
        auto*       b            = add<glaze::Button>(text);
        b->name = std::string("knapp") + namn;
        b->setImg(glyf(namn, 16));
        b->anchorTop = {"rubrik16", 8};
        if (forra.empty())
            b->x = 24;
        else
            b->anchorLeft = {forra, 8};
        forra = b->name;
        knappar_.push_back(b);
    }

    // STORA KNAPPAR — ikonen i 32 px ovanför texten.
    rubrik("Stora knappar, 32 px", "rubrik32", "knappNyRitning", 24);
    forra.clear();
    for (const int i : {1, 3, 4, 5}) {
        const auto& [namn, text] = kGlyfer[i];
        auto*       b            = add<glaze::Button>(text);
        b->name = std::string("stor") + namn;
        b->setImg(glyf(namn, 32));
        b->setIconSize(32);
        b->setImagePos(glaze::ImagePos::Above);
        b->anchorTop = {"rubrik32", 8};
        if (forra.empty())
            b->x = 24;
        else
            b->anchorLeft = {forra, 8};
        forra = b->name;
        knappar_.push_back(b);
    }

    // SKYLTEN i 48 px, bredvid de stora knapparna.
    auto* skylt = add<glaze::Picture>();
    skylt->name = "skylt";
    skylt->setImg(glaze::drawnIcon("Demo", 48, glaze::IconStyle::Color));
    skylt->anchorTop  = {"rubrik32", 8};
    skylt->anchorLeft = {"storLarm", 24};

    auto* av      = add<glaze::Checkbox>("Inaktivera knapparna");
    av->name      = "av";
    av->x         = 24;
    av->anchorTop = {"storRapport", 24};
    av->onChange  = [this, av] {
        for (glaze::Widget* w : knappar_)
            av->isChecked() ? w->disable() : w->enable();
    };

    auto* om = add<glaze::Label>(
        "Auto: färg när knappen är aktiv, mono när den är inaktiv.");
    om->name      = "om";
    om->x         = 24;
    om->anchorTop = {"av", 12};
    om->setRole(glaze::Label::Role::Dim);
}
