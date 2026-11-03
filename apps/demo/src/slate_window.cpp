#include "slate_window.h"

#include <glaze_terminal/terminal.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "about_dialog.h"

namespace {

constexpr gfx::Color kAndrad{214, 118, 44, 255};   // statusfärger på rader
constexpr gfx::Color kFel{204, 68, 68, 255};
constexpr gfx::Color kBlaa{44, 112, 196, 255};     // bildens två färger
constexpr gfx::Color kGul{246, 186, 58, 255};

// En rad på sidan: en Bar utan yta och hårlinje, med luft mellan cellerna.
class Rad final : public glaze::Bar {
public:
    Rad() { setPlain(true); }

protected:
    float childGap(const glaze::Ctx&) const override { return 12.0f; }
};

// En cell i en rad: en ramad yta som delar radens bredd efter sin vikt och
// fyller radens höjd (Panels måttregel: 0 × 0, och en ren rad ger ett barn utan
// eget mått hela tvärsidan).
class Cell final : public glaze::Panel {
public:
    explicit Cell(float vikt) : vikt_(vikt) { setBorder(true); }
    float flexGrow() const override { return vikt_; }

private:
    float vikt_;
};

// Staplar widgets i en yta, uppifrån och ned, med syskonankare: varje rad
// fäster under den förra, så höjderna följer stilen.
class Stapel {
public:
    Stapel(glaze::Panel& yta, std::string prefix, std::string over)
        : yta_(yta), prefix_(std::move(prefix)), over_(std::move(over))
    {
    }

    // Under den förra raden, med egen bredd.
    template <class W, class... A>
    W* under(float gap, A&&... a)
    {
        W* w         = skapa<W>(std::forward<A>(a)...);
        w->x         = 8;
        w->anchorTop = {over_, gap};
        over_        = w->name;
        return w;
    }

    // Under den förra raden, sträckt mellan ytans kanter.
    template <class W, class... A>
    W* bred(float gap, A&&... a)
    {
        W* w       = under<W>(gap, std::forward<A>(a)...);
        w->anchors = {true, true, true, false};
        w->right   = 8;
        return w;
    }

    // Som `bred`, och dessutom ned till ytans underkant.
    template <class W, class... A>
    W* fyll(float gap, A&&... a)
    {
        W* w       = under<W>(gap, std::forward<A>(a)...);
        w->anchors = {true, true, true, true};
        w->right   = 8;
        w->bottom  = 8;
        return w;
    }

    // Till höger om den senaste, med samma överkant. Nästa `under` räknar
    // fortfarande från radens första widget.
    template <class W, class... A>
    W* bredvid(A&&... a)
    {
        glaze::Widget* förra = senaste_;
        W*             w     = skapa<W>(std::forward<A>(a)...);
        w->anchorLeft        = {förra->name, 8};
        w->anchorTop         = förra->anchorTop;
        return w;
    }

private:
    template <class W, class... A>
    W* skapa(A&&... a)
    {
        W* w     = yta_.add<W>(std::forward<A>(a)...);
        w->name  = prefix_ + std::to_string(n_++);
        senaste_ = w;
        return w;
    }

    glaze::Panel&  yta_;
    std::string    prefix_;
    std::string    over_;
    glaze::Widget* senaste_ = nullptr;
    int            n_       = 0;
};

// Ramad cell med rubrik och skiljelinje, och stapeln som börjar under linjen.
Stapel cell(glaze::Bar& r, const std::string& namn, const char* rubrik,
            float vikt = 1.0f)
{
    auto* c = r.add<Cell>(vikt);
    c->name = namn;
    auto* t = c->add<glaze::Label>(rubrik);
    t->x    = 8;
    t->y    = 6;
    t->name = namn + "Rub";
    t->setRole(glaze::Label::Role::Heading);
    auto* s      = c->add<glaze::Separator>();
    s->x         = 8;
    s->anchorTop = {t->name, 6};
    s->anchors   = {true, true, true, false};
    s->right     = 8;
    s->name      = namn + "Sep";
    return Stapel(*c, namn + "_", s->name);
}

// Fyra ringar i två färger, ur pixlar.
glaze::Bitmap ringar(int n, gfx::Color a, gfx::Color b)
{
    std::vector<std::uint8_t> px(std::size_t(n) * std::size_t(n) * 4);
    const float mitt  = float(n - 1) / 2.0f;
    const float bredd = float(n) / 8.0f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const float      r = std::hypot(float(x) - mitt, float(y) - mitt);
            const gfx::Color f = int(r / bredd) % 2 ? b : a;
            std::uint8_t*    p =
                &px[(std::size_t(y) * std::size_t(n) + std::size_t(x)) * 4];
            p[0] = f.r;
            p[1] = f.g;
            p[2] = f.b;
            p[3] = r <= float(n) / 2.0f ? f.a : 0;
        }
    }
    return glaze::Bitmap(n, n, px.data());
}

glaze::ImageSource ikon(glaze::StockIcon i)
{
    return glaze::stockIcon(i);
}

const char* const kLoremKort =
    "Slate ritar fyllda ytor med en hårlinje. Texten här bryts mot bredden, så "
    "radhöjden och kontrasten mot ytan går att bedöma.";

const char* const kMarkdown =
    "# Rubrik ett\n\nEn **fet**, en *kursiv* och en `kod` i löpande text, med "
    "[en länk](https://github.com/holmbom/GlazeUI).\n\n## Rubrik två\n\n"
    "- en punkt\n- en till\n  - nästlad\n\n1. ett\n2. två\n\n> Ett citat.\n\n"
    "```\nfor (int i = 0; i < 3; ++i)\n    rita(i);\n```\n\n"
    "| Vänster | Mitten | Höger |\n|:--|:-:|--:|\n| a | **b** | 12 |\n"
    "| c | `d` | 345 |\n";

} // namespace

// ------------------------------------------------------------------ fönstret

SlateWindow::SlateWindow()
{
    setUI(glaze::UI::Slate);
    setTitle("Slate — alla komponenter");
    setContentSize(1560, 900);
    setMinimumSize(1000, 600);

    dock_       = add<glaze::DockHost>();
    dock_->name = "faltYta";
    dock_->dock = glaze::Dock::Fill;
    dock_->onChange = [this] { synkaFalt(); };

    byggMenyer();
    byggTemaFalt();
    byggVerktygsFalt();
    byggFaltMeny();
    byggStatusrad();
    byggSida();

    snabbmeny_.addItem("Klipp ut").icon = ikon(glaze::StockIcon::Cut);
    snabbmeny_.addItem("Kopiera").icon  = ikon(glaze::StockIcon::Copy);
    snabbmeny_.addItem("Klistra in").enabled = false;
    snabbmeny_.addSeparator();
    snabbmeny_.addCheck("Ett kryss", true);
    snabbmeny_.addRadio("En radiopost", true);
    snabbmeny_.addRadio("En annan radiopost");
    snabbmeny_.addSeparator();
    glaze::MenuItem& under = snabbmeny_.addSubmenu("En undermeny");
    glaze::MenuItem  post;
    post.text = "En post i undermenyn";
    under.submenu.push_back(std::move(post));
    sida_->onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
        snabbmeny_.popupAt(*s, e.x, e.y);
    };

    takt_.setInterval(50);
    takt_.onTick = [this] {
        forlopp_ += 0.5f;
        if (forlopp_ > 100.0f)
            forlopp_ = 0.0f;
        rorligt_->setValue(forlopp_);
    };
    onLoad = [this] {
        takt_.start();
        synkaUtseende();
        if (!terminal_->start())
            visa("Terminalen startade inte: " + terminal_->lastError());
    };
}

void SlateWindow::visa(const std::string& text)
{
    statusBar.setPanelText(0, text);
}

// ------------------------------------------------------------------- temat

void SlateWindow::valjTema(int index)
{
    const int antal = glaze::themeCount(glaze::UI::Slate);
    temaIndex_      = (index % antal + antal) % antal;
    setTheme(glaze::themeAt(glaze::UI::Slate, temaIndex_));
    synkaUtseende();
}

void SlateWindow::valjLage(glaze::ThemeMode m)
{
    setThemeMode(m);
    synkaUtseende();
}

bool SlateWindow::morktNu() const
{
    return &theme().resolve(themeMode()) == &theme().dark;
}

void SlateWindow::vaxlaLage()
{
    valjLage(morktNu() ? glaze::ThemeMode::Light : glaze::ThemeMode::Dark);
}

void SlateWindow::synkaUtseende()
{
    temaDd_->setSelectedIndex(temaIndex_);
    for (int i = 0; i < glaze::themeCount(glaze::UI::Slate); ++i)
        temaMeny_->items[static_cast<std::size_t>(i)].checked = i == temaIndex_;

    const glaze::ThemeMode m = themeMode();
    lageSystem_->setChecked(m == glaze::ThemeMode::System);
    lageLjust_->setChecked(m == glaze::ThemeMode::Light);
    lageMorkt_->setChecked(m == glaze::ThemeMode::Dark);
    glyfSystem_->setChecked(m == glaze::ThemeMode::System);
    glyfLjust_->setChecked(m == glaze::ThemeMode::Light);
    glyfMorkt_->setChecked(m == glaze::ThemeMode::Dark);
    menuBar.setItemChecked({"Tema", "Läge", "System"},
                           m == glaze::ThemeMode::System);
    menuBar.setItemChecked({"Tema", "Läge", "Ljust"},
                           m == glaze::ThemeMode::Light);
    menuBar.setItemChecked({"Tema", "Läge", "Mörkt"},
                           m == glaze::ThemeMode::Dark);

    const char* namn = m == glaze::ThemeMode::Light  ? "ljust"
                       : m == glaze::ThemeMode::Dark ? "mörkt"
                                                     : "system";
    visa("Slate · " + glaze::themeAt(glaze::UI::Slate, temaIndex_).name + " · " +
         namn);
}

void SlateWindow::vaxlaStorIkon()
{
    const bool stor = !appIcon.isLarge();
    appIcon.setLarge(stor);
    storIkon_->setChecked(stor);
    menuBar.setItemChecked({"Visa", "Stor ikon"}, stor);
}

void SlateWindow::vaxlaMenyer()
{
    const bool namnlist = !menuBar.inTitleBar();
    menuBar.setInTitleBar(namnlist);
    menyGlyf_->setChecked(namnlist);
    menuBar.setItemChecked({"Visa", "Menyer i namnlisten"}, namnlist);
}

// ------------------------------------------------------------------- menyerna

void SlateWindow::byggMenyer()
{
    menuBar.show();

    menuBar.addMenu("Arkiv");
    menuBar.addItem("Arkiv", "Nytt", "Ctrl+N");
    menuBar.setItemIcon({"Arkiv", "Nytt"}, ikon(glaze::StockIcon::New));
    menuBar.addItem("Arkiv", "Öppna…", "Ctrl+O");
    menuBar.setItemIcon({"Arkiv", "Öppna…"}, ikon(glaze::StockIcon::Open));
    menuBar.setHandler({"Arkiv", "Öppna…"}, [this] {
        filDialog_.setMode(glaze::FileDialogMode::Open);
        filDialog_.run(*this);
    });
    menuBar.addItem("Arkiv", "Spara", "Ctrl+S");
    menuBar.setItemIcon({"Arkiv", "Spara"}, ikon(glaze::StockIcon::Save));
    menuBar.setItemEnabled({"Arkiv", "Spara"}, false);
    menuBar.addSeparator("Arkiv");
    menuBar.addSubmenu("Arkiv", "Senaste");
    menuBar.addItem({"Arkiv", "Senaste"}, "Rapport.pdf");
    menuBar.addItem({"Arkiv", "Senaste"}, "Brev.txt");
    menuBar.addSubmenu({"Arkiv", "Senaste"}, "Äldre");
    menuBar.addItem({"Arkiv", "Senaste", "Äldre"}, "Budget.xlsx");
    menuBar.addSeparator("Arkiv");
    menuBar.addItem("Arkiv", "Avsluta", "Alt+F4");
    menuBar.setHandler({"Arkiv", "Avsluta"}, [this] { close(); });

    menuBar.addMenu("Redigera");
    menuBar.addItem("Redigera", "Ångra");
    menuBar.setItemIcon({"Redigera", "Ångra"}, ikon(glaze::StockIcon::Undo));
    menuBar.addItem("Redigera", "Gör om");
    menuBar.setItemEnabled({"Redigera", "Gör om"}, false);
    menuBar.addSeparator("Redigera");
    menuBar.addItem("Redigera", "Klipp ut");
    menuBar.addItem("Redigera", "Kopiera");
    menuBar.addItem("Redigera", "Klistra in");

    menuBar.addMenu("Visa");
    menuBar.addCheck("Visa", "Rutnät", true);
    menuBar.addCheck("Visa", "Linjaler");
    menuBar.addSeparator("Visa");
    menuBar.addRadio("Visa", "Lista", true);
    menuBar.addRadio("Visa", "Detaljer");
    menuBar.addRadio("Visa", "Ikoner");
    menuBar.addSeparator("Visa");

    // NAMNLISTEN: menyerna och två glyfknappar, som kortas och samlas under ⋯
    // när fönstret görs smalt.
    menuBar.setInTitleBar(true);
    menuBar.addCheck("Visa", "Menyer i namnlisten", true);
    menuBar.setHandler({"Visa", "Menyer i namnlisten"}, [this] { vaxlaMenyer(); });
    menuBar.addCheck("Visa", "Stor ikon");
    menuBar.setHandler({"Visa", "Stor ikon"}, [this] { vaxlaStorIkon(); });
    auto glyf = [](glaze::ToolButton* b, glaze::StockIcon i, const char* tips) {
        b->setGlyphOnly(true);
        b->setImg(ikon(i));
        b->setToolTip(tips);
        return b;
    };
    glyf(topBar.addStart<glaze::ToolButton>(), glaze::StockIcon::Search, "Sök");
    storIkon_ = glyf(topBar.addEnd<glaze::ToolButton>(), glaze::StockIcon::Sidebar,
                     "Stor ikon");
    storIkon_->setCheckable(true);
    storIkon_->onClick = [this] { vaxlaStorIkon(); };
    menyGlyf_ = glyf(topBar.addEnd<glaze::ToolButton>(), glaze::StockIcon::Menu,
                     "Menyer i namnlisten");
    menyGlyf_->setCheckable(true);
    menyGlyf_->setChecked(true);
    menyGlyf_->onClick = [this] { vaxlaMenyer(); };
    glyf(topBar.addEnd<glaze::ToolButton>(), glaze::StockIcon::Help, "Inaktiverad")
        ->disable();
    // LÄGET, intill fönsterknapparna: det valda lyser i accentfärgen. Trycket
    // slår om kryssrutan, och `synkaUtseende` sätter den rätt igen.
    auto lageGlyf = [&](glaze::StockIcon i, const char* tips, glaze::ThemeMode m) {
        auto* b = glyf(topBar.addEnd<glaze::ToolButton>(), i, tips);
        b->setCheckable(true);
        b->onClick = [this, m] { valjLage(m); };
        return b;
    };
    glyfSystem_ = lageGlyf(glaze::StockIcon::ThemeSystem, "Följ systemet",
                           glaze::ThemeMode::System);
    glyfLjust_  = lageGlyf(glaze::StockIcon::ThemeLight, "Ljust",
                           glaze::ThemeMode::Light);
    glyfMorkt_  = lageGlyf(glaze::StockIcon::ThemeDark, "Mörkt",
                           glaze::ThemeMode::Dark);

    // TEMAT: en radiopost per tema i Slate, först i menyn så att indexet i
    // menyn är temats index. Under dem stegen och läget.
    temaMeny_ = &menuBar.addMenu("Tema");
    for (int i = 0; i < glaze::themeCount(glaze::UI::Slate); ++i)
        temaMeny_->addRadio(glaze::themeAt(glaze::UI::Slate, i).name, i == 0)
            .onClick = [this, i] { valjTema(i); };
    menuBar.addSeparator("Tema");
    menuBar.addItem("Tema", "Föregående tema", "F7");
    menuBar.setHandler({"Tema", "Föregående tema"},
                       [this] { valjTema(temaIndex_ - 1); });
    menuBar.addItem("Tema", "Nästa tema", "F8");
    menuBar.setHandler({"Tema", "Nästa tema"},
                       [this] { valjTema(temaIndex_ + 1); });
    menuBar.addItem("Tema", "Växla ljust och mörkt", "F9");
    menuBar.setHandler({"Tema", "Växla ljust och mörkt"}, [this] { vaxlaLage(); });
    menuBar.addSubmenu("Tema", "Läge");
    menuBar.addRadio({"Tema", "Läge"}, "System", true);
    menuBar.addRadio({"Tema", "Läge"}, "Ljust");
    menuBar.addRadio({"Tema", "Läge"}, "Mörkt");
    menuBar.setHandler({"Tema", "Läge", "System"},
                       [this] { valjLage(glaze::ThemeMode::System); });
    menuBar.setHandler({"Tema", "Läge", "Ljust"},
                       [this] { valjLage(glaze::ThemeMode::Light); });
    menuBar.setHandler({"Tema", "Läge", "Mörkt"},
                       [this] { valjLage(glaze::ThemeMode::Dark); });

    menuBar.addMenu("Hjälp");
    menuBar.addItem("Hjälp", "Om");
    menuBar.setHandler({"Hjälp", "Om"}, [this] {
        AboutDialog dlg;
        dlg.run(*this);
    });
}

// ---------------------------------------------------------- verktygsfälten

void SlateWindow::byggTemaFalt()
{
    auto* f   = dock_->add<glaze::ToolBar>();
    temaFalt_ = f;
    f->name   = "temaFalt";
    f->setText("Tema");
    f->setOverflow(false);

    f->add<glaze::Label>("Tema");
    f->addGap();
    auto* forra = f->add<glaze::ToolButton>();
    forra->setImg(ikon(glaze::StockIcon::ChevronLeft));
    forra->setToolTip("Föregående tema (F7)");
    forra->onClick = [this] { valjTema(temaIndex_ - 1); };

    temaDd_        = f->add<glaze::Dropdown>();
    temaDd_->width = 170;
    std::vector<std::string> namn;
    for (int i = 0; i < glaze::themeCount(glaze::UI::Slate); ++i)
        namn.push_back(glaze::themeAt(glaze::UI::Slate, i).name);
    temaDd_->setItems(std::move(namn));
    temaDd_->onChange = [this] { valjTema(temaDd_->selectedIndex()); };

    auto* nasta = f->add<glaze::ToolButton>();
    nasta->setImg(ikon(glaze::StockIcon::ChevronRight));
    nasta->setToolTip("Nästa tema (F8)");
    nasta->onClick = [this] { valjTema(temaIndex_ + 1); };

    f->add<glaze::Separator>();
    f->add<glaze::Label>("Läge");
    f->addGap();
    auto lage = [&](const char* text, glaze::ThemeMode m) {
        auto* b = f->add<glaze::ToolButton>(text);
        b->setCheckable(true);
        b->onClick = [this, m] { valjLage(m); };
        return b;
    };
    lageSystem_ = lage("System", glaze::ThemeMode::System);
    lageLjust_  = lage("Ljust", glaze::ThemeMode::Light);
    lageMorkt_  = lage("Mörkt", glaze::ThemeMode::Dark);
}

void SlateWindow::byggVerktygsFalt()
{
    auto ikonknapp = [](glaze::ToolBar* f, glaze::StockIcon i, const char* tips) {
        auto* b = f->add<glaze::ToolButton>();
        b->setImg(ikon(i));
        b->setToolTip(tips);
        return b;
    };

    // LIGGANDE: ikonknappar i alla lägen och en av varje inbäddad kontroll.
    auto* f  = dock_->add<glaze::ToolBar>();
    verktyg_ = f;
    f->name  = "verktyg";
    f->setText("Verktyg");
    f->setBand(1);
    ikonknapp(f, glaze::StockIcon::New, "Nytt");
    ikonknapp(f, glaze::StockIcon::Open, "Öppna");
    ikonknapp(f, glaze::StockIcon::Save, "Spara (inaktiverad)")->disable();
    f->add<glaze::Separator>();
    auto* angra = ikonknapp(f, glaze::StockIcon::Undo, "Ångra — delad knapp");
    angra->setSplit(true);
    angra->addItem({}, "Skriv text");
    angra->addItem({}, "Flytta");
    auto* ny = f->add<glaze::ToolButton>("Ny");
    ny->addItem({}, "Dokument");
    ny->addItem({}, "Mapp");
    f->add<glaze::Separator>();
    ikonknapp(f, glaze::StockIcon::Grid, "Växlingsbar, ikryssad")
        ->setCheckable(true);
    auto* på = ikonknapp(f, glaze::StockIcon::Sidebar, "Växlingsbar");
    på->setCheckable(true);
    på->setChecked(true);
    f->add<glaze::ToolButton>("Text");
    f->add<glaze::Separator>();
    f->add<glaze::Label>("Etikett");
    f->addGap();
    f->add<glaze::Button>("Knapp");
    f->addGap();
    auto* sok = f->add<glaze::TextInput>();
    sok->width = 120;
    sok->setPlaceholder("Sök");
    f->addGap();
    auto* zoom  = f->add<glaze::Dropdown>();
    zoom->width = 90;
    zoom->setItems({"100 %", "150 %", "200 %"});
    zoom->setSelectedIndex(0);
    f->addGap();
    auto* tal  = f->add<glaze::SpinBox>();
    tal->width = 70;
    tal->setValue(12);
    f->addGap();
    f->add<glaze::Checkbox>("Kryss")->setChecked(true);
    f->addGap();
    f->add<glaze::Radio>("Radio")->setChecked(true);
    f->addGap();
    auto* regl  = f->add<glaze::Slider>();
    regl->width = 90;
    regl->setValue(40);
    f->addGap();
    auto* forl  = f->add<glaze::Progress>();
    forl->width = 80;
    forl->setValue(60);

    // STÅENDE, vid fönstrets vänsterkant.
    auto* s      = dock_->add<glaze::ToolBar>();
    verktygSida_ = s;
    s->name      = "verktygStaende";
    s->setText("Sidofält");
    s->dock = glaze::Dock::Left;
    ikonknapp(s, glaze::StockIcon::Search, "Sök");
    ikonknapp(s, glaze::StockIcon::Folder, "Mapp");
    s->add<glaze::Separator>();
    ikonknapp(s, glaze::StockIcon::Grid, "Rutnät")->setCheckable(true);
    ikonknapp(s, glaze::StockIcon::Trash, "Inaktiverad")->disable();
}

void SlateWindow::byggFaltMeny()
{
    menuBar.addSeparator("Visa");
    menuBar.addSubmenu("Visa", "Verktygsfält");
    const auto visning = [this](const char* text, glaze::ToolBar* f) {
        menuBar.addCheck({"Visa", "Verktygsfält"}, text, true);
        menuBar.setHandler({"Visa", "Verktygsfält", text}, [this, f] {
            if (f->isVisible())
                f->hide();
            else
                f->show();
            synkaFalt();
        });
    };
    visning("Tema", temaFalt_);
    visning("Verktyg", verktyg_);
    visning("Sidofält", verktygSida_);
    menuBar.addSeparator({"Visa", "Verktygsfält"});
    menuBar.addItem({"Visa", "Verktygsfält"}, "Lägg till fält");
    menuBar.setHandler({"Visa", "Verktygsfält", "Lägg till fält"},
                       [this] { laggTillFalt(); });
    menuBar.addItem({"Visa", "Verktygsfält"}, "Ta bort senaste fältet");
    menuBar.setHandler({"Visa", "Verktygsfält", "Ta bort senaste fältet"},
                       [this] { taBortFalt(); });
    synkaFalt();
}

// Ett nytt fält hamnar sist i det yttersta bandet vid överkanten.
void SlateWindow::laggTillFalt()
{
    auto* f = dock_->add<glaze::ToolBar>();
    f->name = "extra" + std::to_string(++extraNr_);
    f->setText("Extra " + std::to_string(extraNr_));
    auto* a = f->add<glaze::ToolButton>();
    a->setImg(ikon(glaze::StockIcon::Star));
    a->setToolTip(f->text());
    auto* b = f->add<glaze::ToolButton>();
    b->setImg(ikon(glaze::StockIcon::Flag));
    b->setToolTip(f->text());
    extraFalt_.push_back(f);
    synkaFalt();
}

// Fältet kan flyta i ett eget fönster, så det är dess förälder som släpper det.
void SlateWindow::taBortFalt()
{
    if (extraFalt_.empty())
        return;
    glaze::ToolBar* f = extraFalt_.back();
    extraFalt_.pop_back();
    f->parent()->releaseOwned(*f);
    synkaFalt();
}

void SlateWindow::synkaFalt()
{
    menuBar.setItemChecked({"Visa", "Verktygsfält", "Tema"}, temaFalt_->isVisible());
    menuBar.setItemChecked({"Visa", "Verktygsfält", "Verktyg"}, verktyg_->isVisible());
    menuBar.setItemChecked({"Visa", "Verktygsfält", "Sidofält"},
                           verktygSida_->isVisible());
    menuBar.setItemEnabled({"Visa", "Verktygsfält", "Ta bort senaste fältet"},
                           !extraFalt_.empty());
}

// ---------------------------------------------------------------- statusraden

void SlateWindow::byggStatusrad()
{
    statusBar.show();
    statusBar.addPanel("Klar", 0, true);

    // En av varje kontroll, med ett fast mellanrum (`addGap`) mellan dem.
    statusBar.add<glaze::Label>("Etikett");
    statusBar.addGap();
    auto* dim = statusBar.add<glaze::Label>("Dämpad");
    dim->setRole(glaze::Label::Role::Dim);
    statusBar.addGap();
    statusBar.add<glaze::Link>("Länk");
    statusBar.addGap();
    statusBar.add<glaze::Button>("Knapp");
    statusBar.addGap();
    auto* tb = statusBar.add<glaze::ToolButton>();
    tb->setImg(ikon(glaze::StockIcon::Refresh));
    tb->setToolTip("Verktygsknapp");
    statusBar.addGap();
    statusBar.add<glaze::ToolButton>("Text");
    statusBar.addGap();
    statusBar.add<glaze::Checkbox>("Kryss")->setChecked(true);
    statusBar.addGap();
    statusBar.add<glaze::Radio>("Radio")->setChecked(true);
    statusBar.addGap();
    auto* dd  = statusBar.add<glaze::Dropdown>();
    dd->width = 90;
    dd->setItems({"Vallista", "Andra"});
    dd->setSelectedIndex(0);
    statusBar.addGap();
    auto* txt  = statusBar.add<glaze::TextInput>("Fält");
    txt->width = 80;
    statusBar.addGap();
    auto* spin  = statusBar.add<glaze::SpinBox>();
    spin->width = 64;
    spin->setValue(3);
    statusBar.addGap();
    auto* sl  = statusBar.add<glaze::Slider>();
    sl->width = 90;
    sl->setValue(60);
    statusBar.addGap();
    auto* pr  = statusBar.add<glaze::Progress>();
    pr->width = 90;
    pr->setValue(45);
    statusBar.addGap();
    auto* ob  = statusBar.add<glaze::Progress>();
    ob->width = 60;
    ob->setIndeterminate(true);
    statusBar.addGap();
    auto* fa  = statusBar.add<glaze::ColorPicker>();
    fa->width = 70;
    fa->setColor("#3366FF");
    statusBar.addGap();

    statusBar.addPanel("Rad 12, kol 4", 100);
    statusBar.addPanel("100 %");
    statusBar.addPanel("UTF-8", 56);
}

// --------------------------------------------------------------------- sidan

glaze::Bar* SlateWindow::rad(float h)
{
    auto* r    = sida_->add<Rad>();
    r->name    = "rad" + std::to_string(radNr_++);
    r->x       = 12;
    r->right   = 12;
    r->anchors = {true, true, true, false};
    r->height  = h;
    if (sistaRad_.empty())
        r->y = 12;
    else
        r->anchorTop = {sistaRad_, 12};
    sistaRad_ = r->name;
    return r;
}

void SlateWindow::byggSida()
{
    sida_       = dock_->add<glaze::Panel>();   // huvudytan
    sida_->name = "sida";
    sida_->showScrollbars(true);

    radKnapparOchVal();
    radInmatning();
    radListor();
    radRemsorOchYtor();
    radDockningOchText();
    radDialoger();

    // Luft under den sista raden när sidan rullar.
    auto* fot      = sida_->add<glaze::Spacer>();
    fot->x         = 0;
    fot->anchorTop = {sistaRad_, 0};
    fot->width     = 1;
    fot->height    = 12;
    fot->name      = "sidfot";
}

void SlateWindow::radKnapparOchVal()
{
    glaze::Bar* r = rad(376);

    {
        Stapel s = cell(*r, "knappar", "Button · ToolButton");
        s.under<glaze::Button>(10, "Knapp");
        s.bredvid<glaze::Button>("Inaktiverad")->disable();

        s.under<glaze::Button>(8, "Med ikon")->setImg(ikon(glaze::StockIcon::Save));
        auto* ikonAv = s.bredvid<glaze::Button>("Med ikon");
        ikonAv->setImg(ikon(glaze::StockIcon::Save));
        ikonAv->disable();

        // Den högsta först: nästa rad räknar från radens första widget.
        auto* stor = s.under<glaze::Button>(8);
        stor->setImg(ringar(64, kBlaa, kGul));
        stor->setIconSize(32);
        auto* bara = s.bredvid<glaze::Button>();
        bara->setImg(ikon(glaze::StockIcon::Settings));
        bara->setToolTip("Bara en ikon");

        // Standardknappen ritas på accentytan; Enter går hit.
        auto* std_ = s.under<glaze::Button>(8, "Standardknapp");
        setAcceptButton(*std_);
        s.bredvid<glaze::Button>("Textfärg")->setColor(kFel);

        s.under<glaze::ToolButton>(12, "Verktyg");
        auto* tAv = s.bredvid<glaze::ToolButton>("Av");
        tAv->disable();
        auto* tVal = s.under<glaze::ToolButton>(4, "Ikryssad");
        tVal->setCheckable(true);
        tVal->setChecked(true);
        auto* tValAv = s.bredvid<glaze::ToolButton>("Ikryssad av");
        tValAv->setCheckable(true);
        tValAv->setChecked(true);
        tValAv->disable();
        auto* tMeny = s.under<glaze::ToolButton>(4, "Meny");
        tMeny->addItem({}, "Dokument");
        tMeny->addItem({}, "Mapp");
        auto* tDel = s.bredvid<glaze::ToolButton>("Delad");
        tDel->setSplit(true);
        tDel->addItem({}, "Ett");
        tDel->addItem({}, "Två");
    }
    {
        Stapel s = cell(*r, "val", "Checkbox · Radio");
        s.under<glaze::Checkbox>(10, "Av");
        s.under<glaze::Checkbox>(6, "På")->setChecked(true);
        auto* tre = s.under<glaze::Checkbox>(6, "Mellanläge, tre lägen");
        tre->setThreeState(true);
        tre->setCheckState(glaze::CheckState::Indeterminate);
        s.under<glaze::Checkbox>(6, "Av, inaktiverad")->disable();
        auto* kAv = s.under<glaze::Checkbox>(6, "På, inaktiverad");
        kAv->setChecked(true);
        kAv->disable();
        auto* mAv = s.under<glaze::Checkbox>(6, "Mellanläge, inaktiverad");
        mAv->setCheckState(glaze::CheckState::Indeterminate);
        mAv->disable();

        auto* a = s.under<glaze::Radio>(14, "Alternativ A");
        a->setGroup("slate");
        a->setChecked(true);
        s.under<glaze::Radio>(6, "Alternativ B")->setGroup("slate");
        auto* c = s.under<glaze::Radio>(6, "Inaktiverad");
        c->setGroup("slate");
        c->disable();
        auto* d = s.under<glaze::Radio>(6, "Vald, inaktiverad");
        d->setGroup("slateAv");
        d->setChecked(true);
        d->disable();
    }
    {
        Stapel s = cell(*r, "regl", "Slider · Progress · Scrollbar");
        auto*  sl = s.bred<glaze::Slider>(12);
        sl->setValue(40);
        auto* slAv = s.bred<glaze::Slider>(10);
        slAv->setValue(70);
        slAv->disable();

        rorligt_ = s.bred<glaze::Progress>(14);
        auto* p  = s.bred<glaze::Progress>(10);
        p->setValue(100);
        auto* pAv = s.bred<glaze::Progress>(10);
        pAv->setValue(60);
        pAv->disable();
        s.bred<glaze::Progress>(10)->setIndeterminate(true);

        auto* h = s.bred<glaze::Scrollbar>(14);
        h->setOrientation(glaze::Scrollbar::Orientation::Horizontal);
        auto* hAv = s.bred<glaze::Scrollbar>(10);
        hAv->setOrientation(glaze::Scrollbar::Orientation::Horizontal);
        hAv->disable();
    }
    {
        Stapel s = cell(*r, "text", "Label · Link · Separator · Picture");
        s.under<glaze::Label>(10, "Text");
        s.under<glaze::Label>(4, "Dämpad")->setRole(glaze::Label::Role::Dim);
        s.under<glaze::Label>(4, "Accent")->setRole(glaze::Label::Role::Accent);
        s.under<glaze::Label>(4, "Rubrik")->setRole(glaze::Label::Role::Heading);
        s.under<glaze::Label>(4, "Inaktiverad")->disable();
        s.bred<glaze::Label>(4, kLoremKort)->setWrap(true);
        auto* kort = s.under<glaze::Label>(4, "En lång text som kortas med ellips");
        kort->width = 140;
        kort->setElide(true);
        auto* l = s.under<glaze::Link>(8, "En länk");
        l->setUrl("https://github.com/holmbom/GlazeUI");
        s.bredvid<glaze::Link>("Inaktiverad")->disable();
        s.bred<glaze::Separator>(8);
        s.bred<glaze::Separator>(6)->disable();
        auto bild = [](glaze::Picture* p, glaze::ImageSource b) {
            p->width  = 32;
            p->height = 32;
            p->setImg(std::move(b));
            return p;
        };
        bild(s.under<glaze::Picture>(8), ringar(64, kBlaa, kGul));
        bild(s.bredvid<glaze::Picture>(), ringar(64, kGul, kBlaa))->disable();
        bild(s.bredvid<glaze::Picture>(),
             glaze::stockIcon(glaze::StockIcon::Folder, 32));
    }
}

void SlateWindow::radInmatning()
{
    glaze::Bar* r = rad(368);

    {
        Stapel s = cell(*r, "falt", "TextInput");
        s.bred<glaze::TextInput>(10, "Ett textfält");
        auto* plats = s.bred<glaze::TextInput>(6);
        plats->setPlaceholder("Platshållare");
        auto* losen = s.bred<glaze::TextInput>(6, "p4ssw0rd");
        losen->setPassword(true);
        s.bred<glaze::TextInput>(6, "Skrivskyddat")->setReadOnly(true);
        s.bred<glaze::TextInput>(6, "Inaktiverat")->disable();
    }
    {
        Stapel s = cell(*r, "tal", "SpinBox · Dropdown");
        s.under<glaze::SpinBox>(10)->setValue(42);
        auto* dec = s.bredvid<glaze::SpinBox>();
        dec->setDecimals(2);
        dec->setStep(0.25);
        dec->setValue(3.25);
        auto* av = s.under<glaze::SpinBox>(6);
        av->setValue(7);
        av->disable();

        auto* d = s.bred<glaze::Dropdown>(12);
        d->setItems({"Vallista", "Andra", "Tredje", "Fjärde", "Femte"});
        d->setSelectedIndex(0);
        auto* tom = s.bred<glaze::Dropdown>(6);
        tom->setItems({"Inget valt ännu", "Andra"});
        auto* dAv = s.bred<glaze::Dropdown>(6);
        dAv->setItems({"Inaktiverad"});
        dAv->setSelectedIndex(0);
        dAv->disable();
    }
    {
        Stapel s = cell(*r, "valj", "FilePicker · ColorPicker · DatePicker");
        auto*  f = s.bred<glaze::FilePicker>(10);
        f->setPath("rapport.pdf");
        auto* fAv = s.bred<glaze::FilePicker>(6);
        fAv->setPath("rapport.pdf");
        fAv->disable();

        s.under<glaze::ColorPicker>(12)->setColor("#3366FF");
        auto* alfa = s.bredvid<glaze::ColorPicker>();
        alfa->setAllowAlpha(true);
        alfa->setColor("#3366FF80");
        auto* cAv = s.under<glaze::ColorPicker>(6);
        cAv->setColor("#FF9800");
        cAv->disable();

        s.under<glaze::DatePicker>(12);
        s.bredvid<glaze::DatePicker>()->setShowTime(true);
        auto* dAv = s.under<glaze::DatePicker>(6);
        dAv->setDate(2026, 1, 15);
        dAv->disable();
    }
    {
        Stapel s = cell(*r, "yta", "TextArea");
        auto*  t = s.bred<glaze::TextArea>(10);
        t->setText(std::string(kLoremKort) + "\n\n" + kLoremKort + "\n\n" + kLoremKort);
        t->height = 96;
        auto* skydd = s.bred<glaze::TextArea>(6);
        skydd->setText("Skrivskyddad");
        skydd->setReadOnly(true);
        skydd->height = 60;
        auto* tom = s.bred<glaze::TextArea>(6);
        tom->setPlaceholder("Platshållare");
        tom->height = 60;
        auto* av = s.bred<glaze::TextArea>(6);
        av->setText("Inaktiverad");
        av->disable();
        av->height = 60;
    }
}

void SlateWindow::radListor()
{
    glaze::Bar* r = rad(440);

    {
        Stapel s = cell(*r, "lista", "ListBox", 0.8f);
        auto*  l = s.bred<glaze::ListBox>(10);
        l->height = 120;
        l->setItems({"En rad", "Vald rad", "Tredje", "Fjärde", "Femte", "Sjätte",
                     "Sjunde"});
        l->setSelectedIndex(1);
        auto* k = s.bred<glaze::ListBox>(8);
        k->height = 106;
        k->setItems({"Med kryss", "Andra", "Tredje", "Fjärde"});
        k->setShowChecks(true);
        k->setChecked(0, true);
        k->setChecked(2, true);
        auto* av = s.bred<glaze::ListBox>(8);
        av->height = 80;
        av->setItems({"Inaktiverad", "Andra", "Tredje"});
        av->setSelectedIndex(0);
        av->disable();
    }
    {
        Stapel s = cell(*r, "listvy", "ListView", 1.2f);
        auto*  v = s.bred<glaze::ListView>(10);
        v->height = 230;
        v->addColumn("Namn", 130);
        v->addColumn("Storlek", 60, glaze::Align::Right);
        v->addColumn("Ändrad", 86);
        struct Fil {
            const char* namn;
            const char* storlek;
            const char* datum;
            glaze::StockIcon ikon;
        };
        const Fil filer[] = {
            {"Rapport.pdf", "12 kB", "2026-10-07", glaze::StockIcon::Document},
            {"Semester.png", "3 MB", "2026-09-30", glaze::StockIcon::File},
            {"Budget.xlsx", "48 kB", "2026-08-21", glaze::StockIcon::File},
            {"Anteckningar.txt", "900 B", "2026-08-19", glaze::StockIcon::Document},
            {"Fotografier", "", "2026-01-02", glaze::StockIcon::Folder},
            {"Musik", "", "2026-01-01", glaze::StockIcon::Folder},
            {"fil2.txt", "2 kB", "2025-12-30", glaze::StockIcon::File},
            {"fil10.txt", "1 kB", "2025-12-29", glaze::StockIcon::File},
            {"fil11.txt", "1 kB", "2025-12-28", glaze::StockIcon::File},
            {"fil12.txt", "1 kB", "2025-12-27", glaze::StockIcon::File},
        };
        for (const Fil& f : filer)
            v->addRow({f.namn, f.storlek, f.datum}, ikon(f.ikon));
        v->setRowColor(2, kFel);
        v->setRowColor(3, kAndrad);
        v->setMultiSelect(true);
        v->setShowChecks(true);
        v->setChecked(1, true);
        v->setSortColumn(0);
        v->setSelectedIndex(0);
        v->setFillColumn(0);

        auto* av = s.fyll<glaze::ListView>(8);
        av->setColumns({{"Namn", 130},
                        {"Storlek", 60, glaze::Align::Right},
                        {"Ändrad", 86}});
        av->setRows({{{"Rapport.pdf", "12 kB", "2026-10-07"},
                      ikon(glaze::StockIcon::Document)},
                     {{"Anteckningar.txt", "900 B", "2026-08-19"}}});
        av->setSelectedIndex(0);
        av->setFillColumn(0);
        av->disable();
    }
    {
        Stapel s = cell(*r, "trad", "TreeView");
        auto*  t = s.bred<glaze::TreeView>(10);
        t->height = 230;
        const glaze::TreePath dok = t->addNode({}, "Dokument");
        t->addNode(dok, "Rapport.pdf", ikon(glaze::StockIcon::Document));
        const glaze::TreePath brev = t->addNode(dok, "Brev.txt");
        t->setNodeColor(brev, kAndrad);
        const glaze::TreePath bild = t->addNode({}, "Bilder");
        t->addNode(bild, "Semester");
        const glaze::TreePath musik = t->addNode({}, "Musik");
        for (int i = 1; i <= 12; ++i)
            t->addNode(musik, (i < 10 ? "0" : "") + std::to_string(i) + ".flac");
        t->setExpanded(dok, true);
        t->setExpanded(musik, true);
        t->setSelectedPath({0, 1});
        t->setMultiSelect(true);
        t->setShowChecks(true);

        auto* av = s.fyll<glaze::TreeView>(8);
        av->setNodes({{"Dokument",
                       {{.text = "Rapport.pdf", .checked = true},
                        {.text = "Brev.txt", .color = kAndrad}},
                       {},
                       true},
                      {"Bilder"}});
        av->setShowChecks(true);
        av->setSelectedPath({0, 1});
        av->disable();
    }
    {
        Stapel s = cell(*r, "data", "DataGrid", 1.2f);
        auto*  d = s.bred<glaze::DataGrid>(10);
        d->height = 230;
        d->addColumn("Värd", 96);
        d->addColumn("Port", 52, glaze::Align::Right);
        d->addColumn("Protokoll", 84);
        d->addColumn("Id", 48, glaze::Align::Right);
        d->setColumnEditor(2, glaze::DataGrid::Editor::Choice);
        d->setColumnChoices(2, {"HTTP", "HTTPS", "FTP", "SSH"});
        d->setColumnEditor(3, glaze::DataGrid::Editor::None);
        const char* const värdar[] = {"alpha", "beta",  "gamma", "delta", "epsilon",
                                      "zeta",  "eta",   "theta", "iota",  "kappa"};
        int id = 1;
        for (const char* v : värdar) {
            d->addRow({v, std::to_string(8000 + id), id % 2 ? "HTTP" : "HTTPS",
                       std::to_string(id)});
            ++id;
        }
        d->setRowColor(2, kFel);
        d->setCurrentCell(0, 0);
        d->setFillColumn(0);

        auto* av = s.fyll<glaze::DataGrid>(8);
        av->setColumns({{"Värd", 96},
                        {"Port", 52, glaze::Align::Right},
                        {"Protokoll", 84}});
        av->setRows({{{"alpha", "8080", "HTTP"}}, {{"beta", "443", "HTTPS"}}});
        av->setCurrentCell(1, 2);
        av->setFillColumn(0);
        av->disable();
    }
    {
        Stapel s = cell(*r, "egensk", "PropertyGrid", 0.8f);
        std::vector<glaze::PropertyGrid::Row> rader;
        rader.push_back({"name", "okBtn", {}, true, ""});
        rader.push_back({"text", "Tryck här", {}, false, ""});
        rader.push_back({"dock", "None",
                         {"None", "Left", "Top", "Right", "Bottom", "Fill"},
                         false, ""});
        rader.push_back({"width", "120", {}, false, ""});
        rader.push_back({"height", "30", {}, false, ""});
        rader.push_back({"visible", "true", {"true", "false"}, false, ""});
        auto* g   = s.bred<glaze::PropertyGrid>(10);
        g->height = 230;
        g->setRows(rader);
        auto* av = s.fyll<glaze::PropertyGrid>(8);
        av->setRows(rader);
        av->disable();
    }
}

void SlateWindow::radRemsorOchYtor()
{
    glaze::Bar* r = rad(430);

    {
        Stapel s = cell(*r, "remsor", "ToolBar · Bar · TopBar · StatusBar", 1.4f);

        auto remsa = [&](bool av) {
            auto* t = s.bred<glaze::ToolBar>(10);
            t->dock = glaze::Dock::None;
            auto släck = [av](glaze::Widget* w) {
                if (av)
                    w->disable();
            };
            for (glaze::StockIcon i : {glaze::StockIcon::New, glaze::StockIcon::Open,
                                       glaze::StockIcon::Save}) {
                auto* b = t->add<glaze::ToolButton>();
                b->setImg(ikon(i));
                släck(b);
            }
            släck(t->add<glaze::Separator>());
            auto* v = t->add<glaze::ToolButton>("Växel");
            v->setCheckable(true);
            v->setChecked(true);
            släck(v);
            auto* m = t->add<glaze::ToolButton>("Meny");
            m->addItem({}, "Ett");
            släck(m);
            auto* d = t->add<glaze::ToolButton>("Delad");
            d->setSplit(true);
            d->addItem({}, "Ett");
            släck(d);
            släck(t);
        };
        remsa(false);
        remsa(true);

        auto bar = [&](bool av) {
            auto* b = s.bred<glaze::Bar>(10);
            auto* k = b->add<glaze::Button>("Knapp i en Bar");
            b->addSpacer();
            auto* l = b->add<glaze::Label>("Etikett");
            std::vector<glaze::Widget*> barn = {k, l};
            for (glaze::CaptionButton::Kind kind :
                 {glaze::CaptionButton::Kind::Minimize,
                  glaze::CaptionButton::Kind::Maximize,
                  glaze::CaptionButton::Kind::Close}) {
                auto* c = b->add<glaze::CaptionButton>();
                c->setKind(kind);
                barn.push_back(c);
            }
            if (av) {
                for (glaze::Widget* w : barn)
                    w->disable();
                b->disable();
            }
        };
        bar(false);
        bar(true);

        auto* top = s.bred<glaze::TopBar>(10);
        auto* tt  = top->add<glaze::Label>("En TopBar");
        tt->setRole(glaze::Label::Role::Titlebar);
        top->addSpacer();

        auto status = [&](bool av) {
            auto* st = s.bred<glaze::StatusBar>(10);
            st->dock = glaze::Dock::None;
            if (av)
                st->disable();
            st->addPanel("En statusrad", 0, true);
            st->addPanel("100 %");
            st->addPanel("UTF-8", 56);
        };
        status(false);
        status(true);
    }
    {
        Stapel s = cell(*r, "flikar", "TabControl");
        auto*  t = s.bred<glaze::TabControl>(10);
        t->height = 170;
        t->setClosable(true);
        for (const char* fil : {"Rapport.pdf", "Brev.txt", "Budget.xlsx"}) {
            glaze::TabPage& p = t->addPage(fil);
            auto*           l = p.add<glaze::Label>(fil);
            l->x              = 8;
            l->y              = 8;
        }
        t->setSelectedIndex(1);
        t->onTabClose = [](glaze::Widget* w, glaze::TabCloseArgs& e) {
            auto* tc = static_cast<glaze::TabControl*>(w);
            if (glaze::TabPage* p = tc->page(e.index))
                tc->unadopt(*p);
        };
        auto* n = s.bred<glaze::TabControl>(8);
        n->height = 90;
        n->add<glaze::TabPage>("Allmänt");
        n->add<glaze::TabPage>("Avancerat");
        n->add<glaze::TabPage>("Övrigt");
        auto* av = s.fyll<glaze::TabControl>(8);
        av->add<glaze::TabPage>("Inaktiverad");
        av->add<glaze::TabPage>("Andra");
        av->disable();
    }
    {
        Stapel s  = cell(*r, "ytor", "Panel · GroupBox");
        auto*  gb = s.bred<glaze::GroupBox>(10, "En gruppram");
        auto*  g1 = gb->add<glaze::Checkbox>("Dubbelsidig");
        g1->name  = "gb1";
        g1->setChecked(true);
        auto* g2      = gb->add<glaze::Checkbox>("Svartvit");
        g2->anchorTop = {"gb1", 6};
        auto* gbAv    = s.bred<glaze::GroupBox>(8, "Inaktiverad gruppram");
        gbAv->disable();
        auto* r1 = gbAv->add<glaze::Radio>("Stående");
        r1->name = "gbR";
        r1->setChecked(true);
        r1->disable();
        auto* r2      = gbAv->add<glaze::Radio>("Liggande");
        r2->anchorTop = {"gbR", 6};
        r2->disable();

        auto* p = s.fyll<glaze::Panel>(10);
        p->setBorder(true);
        p->showScrollbars(true);
        for (int n = 0; n < 6; ++n) {
            auto* b = p->add<glaze::Button>("Knapp i en panel " +
                                            std::to_string(n + 1));
            b->name = "pan" + std::to_string(n);
            b->x    = 8;
            b->width = 420;
            if (n == 0)
                b->y = 8;
            else
                b->anchorTop = {"pan" + std::to_string(n - 1), 6};
        }
    }
    {
        Stapel s = cell(*r, "delare", "Splitter · FloatingPanel");
        auto ruta = [](glaze::Splitter* d, const char* text) {
            auto* p = d->add<glaze::Panel>();
            p->setBorder(true);
            auto* l = p->add<glaze::Label>(text);
            l->x    = 6;
            l->y    = 6;
        };
        auto* st = s.bred<glaze::Splitter>(10);
        st->height = 90;
        st->setPaneSize(120);
        ruta(st, "Första");
        ruta(st, "Andra");
        auto* li = s.bred<glaze::Splitter>(8);
        li->height = 90;
        li->setVertical(false);
        li->setPaneSize(40);
        ruta(li, "Övre");
        ruta(li, "Nedre");

        // Den fria panelen flyttas och storleksändras inne i sin yta.
        auto* y = s.fyll<glaze::Panel>(8);
        y->setBorder(true);
        y->setClip(true);
        auto* f = y->add<glaze::FloatingPanel>("En fri panel");
        f->x      = 12;
        f->y      = 12;
        f->width  = 180;
        f->height = 110;
        f->setClosable(true);
        auto* l = f->add<glaze::Label>("Dra i rubriken eller hörnet.");
        l->x    = 8;
        l->y    = 8;
    }
}

void SlateWindow::radDockningOchText()
{
    glaze::Bar* r = rad(460);

    {
        Stapel s = cell(*r, "dock", "DockHost · DockPanel · Terminal", 1.6f);
        auto*  h = s.fyll<glaze::DockHost>(10);
        h->dock  = glaze::Dock::None;

        auto* band = h->add<glaze::ToolBar>();
        band->name = "dockFalt";
        for (glaze::StockIcon i : {glaze::StockIcon::Cut, glaze::StockIcon::Copy,
                                   glaze::StockIcon::Paste}) {
            band->add<glaze::ToolButton>()->setImg(ikon(i));
        }

        auto* verktyg  = h->add<glaze::DockPanel>("Verktyg");
        verktyg->name  = "dockVerktyg";
        verktyg->width = 160;
        auto* falt     = verktyg->add<glaze::TextInput>();
        falt->dock     = glaze::Dock::Top;
        falt->setPlaceholder("Filtrera");

        auto* egensk  = h->add<glaze::DockPanel>("Egenskaper");
        egensk->name  = "dockEgensk";
        egensk->dock  = glaze::Dock::Right;
        egensk->width = 160;

        auto* utdata   = h->add<glaze::DockPanel>("Utdata");
        utdata->name   = "dockUtdata";
        utdata->dock   = glaze::Dock::Bottom;
        utdata->height = 130;

        auto* term = h->add<glaze::DockPanel>("Terminal");
        term->name = "dockTerm";
        term->dock = glaze::Dock::Bottom;
        auto* t    = term->add<glaze::Terminal>();
        t->dock    = glaze::Dock::Fill;

        auto* dok = h->add<glaze::Panel>();
        dok->setBorder(true);
        auto* dl = dok->add<glaze::Label>("Dokumentet: dra en rubrik mot en kant.");
        dl->x    = 8;
        dl->y    = 8;

        // Efter adoptionen: terminalen blir en flik bredvid utdatan.
        h->dockPanel(*term, *utdata, glaze::Dock::Fill);
        terminal_ = t;
    }
    {
        Stapel s = cell(*r, "rita", "Canvas");
        auto*  c = s.fyll<glaze::Canvas>(10);
        c->onPaint = [this](glaze::Widget*, glaze::PaintArgs& e) {
            const auto& col = theme().resolve(themeMode()).colors;
            const glaze::Stroke rut{.paint = col.border};
            for (int x = 0; x <= 1600; x += 40)
                e.painter.drawLine({double(x), 0}, {double(x), 1000}, rut);
            for (int y = 0; y <= 1000; y += 40)
                e.painter.drawLine({0, double(y)}, {1600, double(y)}, rut);
            e.painter.fillRect({40, 40, 120, 80}, col.accent);
            e.painter.drawRect({200, 40, 120, 80},
                               glaze::Stroke{.paint = col.text, .width = glaze::px(2)});
            e.painter.drawLine({40, 200}, {320, 160},
                               glaze::Stroke{.paint = col.accent, .width = glaze::px(3)});
            e.painter.drawText({40, 220}, "Text på ritytan",
                               {.size   = glaze::px(style().uiFontSize),
                                .color  = col.text,
                                .anchor = glaze::TextAnchor::TopLeft});
        };
    }
    {
        Stapel s = cell(*r, "md", "MarkdownView");
        auto*  m = s.fyll<glaze::MarkdownView>(10, kMarkdown);
        m->setBorder(true);
    }
}

void SlateWindow::radDialoger()
{
    glaze::Bar* r = rad(190);

    {
        Stapel s = cell(*r, "dlgMed", "messageBox");
        auto ruta = [this](const char* text, glaze::MessageBoxType typ,
                           glaze::MessageBoxButtons knappar) {
            return [this, text, typ, knappar] {
                glaze::messageBox(*this, text, "Slate", knappar, typ);
            };
        };
        s.under<glaze::Button>(10, "Information")->onClick =
            ruta("En upplysning.", glaze::MessageBoxType::Information,
                 glaze::MessageBoxButtons::OK);
        s.bredvid<glaze::Button>("Fråga")->onClick =
            ruta("Ta bort alla poster?", glaze::MessageBoxType::Confirmation,
                 glaze::MessageBoxButtons::YesNoCancel);
        s.under<glaze::Button>(8, "Varning")->onClick =
            ruta("Filen är skrivskyddad.", glaze::MessageBoxType::Warning,
                 glaze::MessageBoxButtons::OKCancel);
        s.bredvid<glaze::Button>("Fel")->onClick =
            ruta("Filen kunde inte sparas.", glaze::MessageBoxType::Error,
                 glaze::MessageBoxButtons::RetryCancel);
    }
    {
        Stapel s = cell(*r, "dlgFil", "FileDialog · ColorDialog · inputBox");
        auto fil = [this](glaze::FileDialogMode m) {
            return [this, m] {
                filDialog_.setMode(m);
                filDialog_.run(*this);
            };
        };
        s.under<glaze::Button>(10, "Öppna…")->onClick =
            fil(glaze::FileDialogMode::Open);
        s.bredvid<glaze::Button>("Spara…")->onClick =
            fil(glaze::FileDialogMode::Save);
        s.under<glaze::Button>(8, "Mapp…")->onClick =
            fil(glaze::FileDialogMode::Folder);
        s.bredvid<glaze::Button>("Färg…")->onClick = [this] {
            fargDialog_.run(*this);
        };
        s.under<glaze::Button>(8, "Inmatning…")->onClick = [this] {
            glaze::inputBox(*this, "Lagrets namn:", "Byt namn", "Vägar");
        };
    }
    {
        Stapel s = cell(*r, "dlgFon", "Fönster · snabbmeny · verktygstips");
        s.under<glaze::Button>(10, "Modal dialog…")->onClick = [this] {
            AboutDialog dlg;
            dlg.run(*this);
        };
        s.bredvid<glaze::Button>("Ägt fönster…")->onClick = [this] {
            if (!formular_)
                formular_ = std::make_unique<FormDialog>();
            formular_->show(*this);
        };
        auto* tips = s.under<glaze::Button>(8, "Vila pekaren här");
        tips->setToolTip("Ett verktygstips, med text som kan bli ganska lång "
                         "och då bryts mot sin största bredd.");
        auto* tipsAv = s.bredvid<glaze::Button>("Inaktiverad med tips");
        tipsAv->setToolTip("Tips visas också på en inaktiverad kontroll.");
        tipsAv->disable();
        s.under<glaze::Label>(10, "Högerklicka på sidan för snabbmenyn.")
            ->setRole(glaze::Label::Role::Dim);
    }
    {
        Stapel s = cell(*r, "dra", "Dra och släpp");
        auto*  k = s.under<glaze::Button>(10, "Dra mig");
        k->setImg(ikon(glaze::StockIcon::File));
        k->setAllowDrag(true);
        k->onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText("Rapport.pdf");
            e.img = glaze::stockIcon(glaze::StockIcon::File);
        };
        auto* zon = s.bred<glaze::Panel>(8);
        zon->height = 44;
        zon->setBorder(true);
        zon->setAllowDrop(true);
        auto* txt    = zon->add<glaze::Label>("Släpp här");
        txt->x       = 8;
        txt->anchors = {true, false, false, false};
        zon->onDragEnter = [](glaze::Widget*, glaze::DragArgs& e) {
            if (e.data.has(glaze::DragData::kText))
                e.effect = glaze::DragEffect::Copy;
        };
        zon->onDrop = [txt](glaze::Widget*, glaze::DragArgs& e) {
            txt->setText(e.data.text());
        };
    }
}
