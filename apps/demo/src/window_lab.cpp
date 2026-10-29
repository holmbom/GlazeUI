#include "window_lab.h"

#include <string>

namespace {

// Läget som ord — demons strängar, så att de följer språket.
std::uint32_t lageText(gfx::WindowState s)
{
    switch (s) {
    case gfx::WindowState::Minimized: return demo::str::LabMinimerat;
    case gfx::WindowState::Maximized: return demo::str::LabMaximerat;
    case gfx::WindowState::Normal:    break;
    }
    return demo::str::LabNormal;
}

// "x 120 · y 80 · 800 × 600 · Normal" — talen och tecknen står likadant på
// varje språk, läget och "ingen position" följer det.
std::string geometriText(const glaze::WindowGeometry& g)
{
    std::string t;
    if (g.hasPosition)
        t = "x " + std::to_string(g.x) + " · y " + std::to_string(g.y);
    else
        t = trs(demo::str::LabIngenPosition);
    t += " · " + std::to_string(g.width) + " × " + std::to_string(g.height) +
         " · " + trs(lageText(g.state));
    return t;
}

// FÖNSTRET FÖLJER INNEHÅLLET — bredden är
// begärans och användarens, höjden innehållets: en Top-dockad radbruten
// text, en kryssruta som visar och döljer tre Top-dockade rader med satt
// höjd (DOCKADE rader; formuläret `FormDialog` visar samma sak med rader
// uppifrån och mot nederkanten) och en Bottom-dockad stängknapp.
class FoljDialog : public glaze::AppWindow {
public:
    FoljDialog() : glaze::AppWindow(glaze::tr(demo::str::LabFolj))
    {
        setContentSize(340, 200);   // bredden — höjden räknas ur innehållet
        setSizeToContent(glaze::SizeToContent::Height);
        setStartPosition(glaze::StartPosition::CenterOwner);

        auto* text = add<glaze::Label>(glaze::tr(demo::str::LabFoljText));
        text->name = "text";
        text->dock = glaze::Dock::Top;
        text->setWrap(true);

        auto* fler = add<glaze::Checkbox>(glaze::tr(demo::str::LabFlerRader));
        fler->name = "fler";
        fler->dock = glaze::Dock::Top;
        for (std::size_t i = 0; i < 3; ++i) {
            auto* rad   = add<glaze::Slider>();
            rad->name   = "rad" + std::to_string(i + 1);
            rad->dock   = glaze::Dock::Top;
            rad->height = 32;
            rad->hide();
            rader_[i] = rad;
        }
        fler->onChange = [this, fler] {
            for (glaze::Slider* rad : rader_)
                if (fler->isChecked())
                    rad->show();
                else
                    rad->hide();
        };

        auto* stang    = add<glaze::Button>(glaze::tr(demo::str::Stang));
        stang->name    = "stang";
        stang->dock    = glaze::Dock::Bottom;
        stang->onClick = [this] { close(); };
    }

private:
    glaze::Slider* rader_[3] = {};
};

} // namespace

WindowLab::WindowLab() : glaze::AppWindow(glaze::tr(demo::str::LabTitel))
{
    setContentSize(460, 450);

    // ---- geometrin och den sparade, överst och STRÄCKTA mellan kanterna
    geometri_          = add<glaze::Label>();   // texten sätts av onShow
    geometri_->name    = "geometri";
    geometri_->x       = 16;
    geometri_->y       = 16;
    geometri_->right   = 16;
    geometri_->anchors = {true, true, true, false};

    sparad_            = add<glaze::Label>(glaze::tr(demo::str::LabIngetSparat));
    sparad_->name      = "sparad";
    sparad_->x         = 16;
    sparad_->right     = 16;
    sparad_->anchors   = {true, true, true, false};
    sparad_->anchorTop = {"geometri", 6};
    sparad_->setRole(glaze::Label::Role::Dim);

    // ---- läget — tre knappar i en rad, syskonankrade
    auto* maxBtn      = add<glaze::Button>(glaze::tr(demo::str::LabMaximera));
    maxBtn->name      = "maximera";
    maxBtn->x         = 16;
    maxBtn->anchorTop = {"sparad", 16};
    maxBtn->onClick   = [this] {
        setWindowState(gfx::WindowState::Maximized);
        visaGeometri();
    };

    auto* minBtn       = add<glaze::Button>(glaze::tr(demo::str::LabMinimera));
    minBtn->name       = "minimera";
    minBtn->anchorTop  = {"sparad", 16};
    minBtn->anchorLeft = {"maximera", 8};
    minBtn->onClick    = [this] {
        setWindowState(gfx::WindowState::Minimized);
        visaGeometri();
    };

    auto* normBtn       = add<glaze::Button>(glaze::tr(demo::str::LabNormal));
    normBtn->name       = "normal";
    normBtn->anchorTop  = {"sparad", 16};
    normBtn->anchorLeft = {"minimera", 8};
    normBtn->onClick    = [this] {
        setWindowState(gfx::WindowState::Normal);
        visaGeometri();
    };

    // ---- spara och återställ
    auto* sparaBtn      = add<glaze::Button>(glaze::tr(demo::str::LabSpara));
    sparaBtn->name      = "spara";
    sparaBtn->x         = 16;
    sparaBtn->anchorTop = {"maximera", 8};
    sparaBtn->onClick   = [this] { sparaGeometri(); };

    auto* oppnaBtn       = add<glaze::Button>(glaze::tr(demo::str::LabOppnaSparad));
    oppnaBtn->name       = "oppna";
    oppnaBtn->anchorTop  = {"maximera", 8};
    oppnaBtn->anchorLeft = {"spara", 8};
    oppnaBtn->onClick    = [this] { oppnaSparad(); };

    // ---- pekarna: fyra rutor med var sin pekare. Rubriken står OVANFÖR
    // rutan — en etikett inuti hade varit det pekaren träffar, med sin egen
    // pil.
    auto* pekRubrik      = add<glaze::Label>(glaze::tr(demo::str::LabPekarna));
    pekRubrik->name      = "pekarna";
    pekRubrik->x         = 16;
    pekRubrik->anchorTop = {"spara", 16};

    struct Yta {
        const char*       namn;
        const char*       rubrik;
        std::uint32_t     text;
        gfx::PointerShape pekare;
    };
    static constexpr Yta kYtor[] = {
        {"ytaHand", "rubrikHand", demo::str::LabHand, gfx::PointerShape::Hand},
        {"ytaStangd", "rubrikStangd", demo::str::LabStangdHand,
         gfx::PointerShape::ClosedHand},
        {"ytaDold", "rubrikDold", demo::str::LabDold, gfx::PointerShape::Hidden},
        {"ytaVanta", "rubrikVanta", demo::str::LabVanta, gfx::PointerShape::Wait},
    };
    float x = 16.0f;
    for (const Yta& y : kYtor) {
        auto* r      = add<glaze::Label>(glaze::tr(y.text));
        r->name      = y.rubrik;
        r->x         = x;
        r->anchorTop = {"pekarna", 8};

        auto* p      = add<glaze::Panel>();
        p->name      = y.namn;
        p->x         = x;
        p->width     = 100;
        p->height    = 64;
        p->anchorTop = {y.rubrik, 4};
        p->setBorder(true);
        p->setCursor(y.pekare);
        x += 108.0f;
    }

    // ---- väntepekaren över hela fönstret
    vanta_ = add<glaze::Checkbox>(glaze::tr(demo::str::LabVantaHela));
    vanta_->name      = "vanta";
    vanta_->x         = 16;
    vanta_->anchorTop = {"ytaHand", 16};
    vanta_->onChange  = [this] { setUseWaitCursor(vanta_->isChecked()); };

    // ---- storleken och fönstret: innehållsytan ur
    // `root().onResize`, det aktiva fönstret i timerns tick och dialogen som
    // följer sitt innehåll
    innehall_            = add<glaze::Label>();   // texten sätts av onResize
    innehall_->name      = "innehall";
    innehall_->x         = 16;
    innehall_->anchorTop = {"vanta", 16};
    root().onResize      = [this](glaze::Widget* s, glaze::ResizeArgs&) {
        const gfx::Sizef y = s->actualSize();
        innehall_->setText(trs(demo::str::LabInnehall) +
                           std::to_string(static_cast<int>(y.w + 0.5f)) +
                           " × " +
                           std::to_string(static_cast<int>(y.h + 0.5f)));
    };

    aktivt_            = add<glaze::Label>();   // texten sätts av timern
    aktivt_->name      = "aktivt";
    aktivt_->x         = 16;
    aktivt_->anchorTop = {"innehall", 6};

    auto* foljBtn      = add<glaze::Button>(glaze::tr(demo::str::LabFolj));
    foljBtn->name      = "folj";
    foljBtn->x         = 16;
    foljBtn->anchorTop = {"aktivt", 12};
    foljBtn->onClick   = [this] {
        if (!folj_)
            folj_ = std::make_unique<FoljDialog>();
        folj_->show(*this);   // redan öppen: lyfts
    };

    // FORMULÄRET — rader mot nederkanten, innehållet som golv
    // och en etikettkolumn, i en `.glazeui`-triplett
    auto* formBtn       = add<glaze::Button>(glaze::tr(demo::str::LabFormular));
    formBtn->name       = "formular";
    formBtn->anchorLeft = {"folj", 8};
    formBtn->anchorTop  = {"aktivt", 12};
    formBtn->onClick    = [this] {
        if (!form_)
            form_ = std::make_unique<FormDialog>();
        form_->show(*this);   // redan öppen: lyfts
    };

    // ---- timern: kodformen. Den går bara medan fönstret syns — ett dolt
    // eller stängt labb väcker inte appen fyra gånger i sekunden (regel 1).
    klocka_.name = "klocka";
    klocka_.setInterval(250);
    klocka_.onTick = [this] { visaGeometri(); };
    onShow  = [this] {
        visaGeometri();
        klocka_.start();
    };
    onHide  = [this] { klocka_.stop(); };
    onClose = [this](glaze::Widget*, glaze::CloseArgs& e) {
        if (!e.cancel)
            klocka_.stop();
    };
}

void WindowLab::visaGeometri()
{
    geometri_->setText(trs(demo::str::LabGeometri) + geometriText(geometry()));
    // Det aktiva fönstret — titeln, eller "inget" när appen inte har fokus.
    const glaze::AppWindow* a = glaze::app().activeWindow();
    aktivt_->setText(trs(demo::str::LabAktivt) +
                     (a ? a->title() : trs(demo::str::LabInget)));
}

void WindowLab::sparaGeometri()
{
    sparadGeo_ = geometry();
    sparad_->setText(trs(demo::str::LabSparad) + geometriText(*sparadGeo_));
}

// Den sparade geometrin på ett ANNAT labb, satt FÖRE `show()` — som en app
// gör med det den läst ur sin inställningsfil vid start.
void WindowLab::oppnaSparad()
{
    if (!sparadGeo_) {
        sparad_->setText(trs(demo::str::LabIngetSparat));
        return;
    }
    if (!nytt_)
        nytt_ = std::make_unique<WindowLab>();
    if (nytt_->isOpen()) {
        sparad_->setText(trs(demo::str::LabNyttOppet));
        return;
    }
    const glaze::WindowGeometry& g = *sparadGeo_;
    if (g.hasPosition)
        nytt_->setLocation(g.x, g.y);
    nytt_->setClientSize(g.width, g.height);
    // En sparad minimering öppnas normal — ett fönster som startar minimerat
    // är sällan vad användaren menade när det stängdes så.
    nytt_->setWindowState(g.state == gfx::WindowState::Minimized
                              ? gfx::WindowState::Normal
                              : g.state);
    nytt_->show(*this);
}
