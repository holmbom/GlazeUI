#include "login_window.h"

LoginWindow::LoginWindow()
{
    setUI(glaze::UI::Slate);
    setThemeMode(glaze::ThemeMode::Light);
    setTitle("Logga in");
    setContentSize(420, 400);
    setMinimumContentSize(360, 400);
    setStartPosition(glaze::StartPosition::CenterScreen);

    const gfx::Color bla{150, 205, 255};
    const gfx::Color vit{255, 255, 255};
    glaze::WindowGradient g;
    g.topLeft     = glaze::tint(bla);
    g.topRight    = glaze::tint(vit);
    g.bottomRight = glaze::tint(vit);
    g.bottomLeft  = glaze::tint(vit);
    g.topLeftDark = glaze::tint(bla, 0.25f);   // de andra hörnen: bakgrunden
    setWindowGradient(g);

    // LÄGET: solen i mörkt, halvcirkeln i ljust — knappen visar vart den leder.
    auto* lage    = add<glaze::ToolButton>();
    lage->name    = "lage";
    lage->anchors = {false, true, true, false};
    lage->right   = 12;
    lage->y       = 12;
    lage->setImg(glaze::stockIcon(glaze::StockIcon::Contrast));
    lage->setToolTip("Växla mellan ljust och mörkt");
    lage->onClick = [this, lage] {
        const bool mork = themeMode() != glaze::ThemeMode::Dark;
        setThemeMode(mork ? glaze::ThemeMode::Dark : glaze::ThemeMode::Light);
        lage->setImg(glaze::stockIcon(mork ? glaze::StockIcon::Brightness
                                           : glaze::StockIcon::Contrast));
    };

    // Knapparna först: fältens ankare pekar bara bakåt, och de står nederst.
    auto* avbryt    = add<glaze::Button>("Avbryt");
    avbryt->name    = "avbryt";
    avbryt->anchors = {false, false, true, true};
    avbryt->right   = 32;
    avbryt->bottom  = 32;
    avbryt->width   = 88;
    avbryt->onClick = [this] { close(); };
    setCancelButton(*avbryt);

    auto* logga     = add<glaze::Button>("Logga in");
    logga->name     = "logga";
    logga->anchors  = {false, false, false, true};
    logga->anchorRight = {"avbryt", 8};
    logga->bottom   = 32;
    logga->width    = 88;
    logga->onClick  = [this] { close(); };
    setAcceptButton(*logga);

    auto* rubrik    = add<glaze::Label>("Välkommen");
    rubrik->name    = "rubrik";
    rubrik->x       = 32;
    rubrik->y       = 36;
    rubrik->setRole(glaze::Label::Role::Heading);

    auto* under      = add<glaze::Label>("Logga in för att fortsätta.");
    under->name      = "under";
    under->x         = 32;
    under->anchorTop = {"rubrik", 4};
    under->setRole(glaze::Label::Role::Dim);

    auto fält = [this](const char* text, const std::string& namn,
                       const std::string& över, float gap) {
        auto* l      = add<glaze::Label>(text);
        l->name      = namn + "Lbl";
        l->x         = 32;
        l->anchorTop = {över, gap};
        auto* f      = add<glaze::TextInput>();
        f->name      = namn;
        f->x         = 32;
        f->right     = 32;
        f->anchors   = {true, true, true, false};
        f->anchorTop = {l->name, 4};
        return f;
    };
    auto* namn = fält("Användarnamn", "namn", "under", 24);
    namn->setPlaceholder("namn@exempel.se");
    fält("Lösenord", "losen", "namn", 12)->setPassword(true);

    auto* kom      = add<glaze::Checkbox>("Kom ihåg mig");
    kom->name      = "kom";
    kom->x         = 32;
    kom->anchorTop = {"losen", 16};

    auto* glomt      = add<glaze::Link>("Glömt lösenordet?");
    glomt->name      = "glomt";
    glomt->x         = 32;
    glomt->anchorTop = {"kom", 12};
}
