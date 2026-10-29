#include "tray_window.h"

#include <vector>

#include "about_dialog.h"

TrayWindow::TrayWindow() : glaze::AppWindow(glaze::tr(demo::str::TrayTitel))
{
    const bool stod = glaze::TrayIcon::isSupported();

    // AKTIVITETSFÄLTET är ett ATTRIBUT och sätts före show(): ingen knapp där
    // när ikonen vid klockan är vägen tillbaka till fönstret. Utan systemfält
    // står knappen kvar — annars fanns ingen väg alls.
    setShowInTaskbar(!stod);
    setContentSize(440, 220);

    // Bara stängknappen: ett fönster som bor i systemfältet döljs, det
    // minimeras inte, och det har inget att maximera.
    setMinimizeBox(false);
    setMaximizeBox(false);

    // ---- texten — radbruten och STRÄCKT mellan fönstrets kanter
    //: höjden följer den bredd layouten ger, så den följer
    // språket och stilen utan ett spikat mått, och syskonankarna nedanför
    // flyttar med. 440 − 2 × 16 = 408, samma bredd som det tal som stod här.
    auto* rad1    = add<glaze::Label>(glaze::tr(demo::str::TrayRad1));
    rad1->name    = "rad1";
    rad1->x       = 16;
    rad1->y       = 16;
    rad1->right   = 16;
    rad1->anchors = {true, true, true, false};
    rad1->setWrap(true);

    auto* rad2 = add<glaze::Label>(
        glaze::tr(stod ? demo::str::TrayRad2 : demo::str::TrayInteStod));
    rad2->name      = "rad2";
    rad2->x         = 16;
    rad2->right     = 16;
    rad2->anchors   = {true, true, true, false};
    rad2->anchorTop = {"rad1", 6};
    rad2->setWrap(true);
    rad2->setRole(glaze::Label::Role::Dim);

    // ---- kryssrutorna — utan systemfält finns inget att animera eller stänga
    // till, och då är de av.
    animera_ = add<glaze::Checkbox>(glaze::tr(demo::str::AnimeraIkonen));
    animera_->name      = "animera";
    animera_->x         = 16;
    animera_->anchorTop = {"rad2", 16};
    animera_->onChange  = [this] { animera(animera_->isChecked()); };

    stangTill_ = add<glaze::Checkbox>(glaze::tr(demo::str::StangTillFaltet));
    stangTill_->name      = "stangTill";
    stangTill_->x         = 16;
    stangTill_->anchorTop = {"animera", 8};
    stangTill_->setChecked(stod);
    if (!stod) {
        animera_->disable();
        stangTill_->disable();
    }

    // ---- knapparna nere till höger. "Avsluta" skapas FÖRST: ett syskonankare
    // pekar bara bakåt, och "Dölj fönstret" står till vänster om den.
    auto* avslutaBtn    = add<glaze::Button>(glaze::tr(demo::str::MenuAvsluta));
    avslutaBtn->name    = "avsluta";
    avslutaBtn->anchors = {false, false, true, true};
    avslutaBtn->right   = 12;
    avslutaBtn->bottom  = 12;
    avslutaBtn->onClick = [this] { avsluta(); };

    auto* doljBtn        = add<glaze::Button>(glaze::tr(demo::str::DoljFonstret));
    doljBtn->name        = "dolj";
    doljBtn->anchors     = {false, false, false, true};
    doljBtn->bottom      = 12;
    doljBtn->anchorRight = {"avsluta", 8};
    doljBtn->onClick     = [this] { hide(); };
    if (!stod)
        doljBtn->disable();

    // ---- statusraden: ikonens händelser, så att de går att se.
    statusBar.show();
    auto* lead = statusBar.add<glaze::Spacer>();
    lead->setGrow(0);
    lead->width = 12;
    status_ = statusBar.add<glaze::Label>(glaze::tr(demo::str::TrayStatus));
    status_->setRole(glaze::Label::Role::Dim);

    // ---- IKONEN. Ingen `setIcon`: förvalet är GlazeUI-ikonen (och
    // programfilens egen där den finns). Bilderna är animationens; de växlar
    // bara medan ikonen syns och animationen är startad.
    tray_.name = "tray";
    tray_.setToolTip(glaze::tr(demo::str::TrayTips));
    tray_.setFrames({glaze::img("toolbar", "info"), glaze::img("toolbar", "ok"),
                     glaze::img("toolbar", "gear"),
                     glaze::img("toolbar", "cancel")});
    tray_.setInterval(400);

    // MENYN — `appIcon`s platta form, och den TOMMA stigen `{}` är roten.
    // Ordningen är den `ikonMeny` läser (poster 0–2).
    tray_.addItem({}, glaze::tr(demo::str::VisaFonstret));
    tray_.addItem({}, glaze::tr(demo::str::DoljFonstret));
    tray_.addCheck({}, glaze::tr(demo::str::AnimeraIkonen));
    tray_.addItem({}, glaze::tr(demo::str::MenuOm));
    tray_.addSeparator({});
    tray_.addItem({}, glaze::tr(demo::str::MenuAvsluta));

    tray_.setHandler(glaze::tr(demo::str::VisaFonstret), [this] { show(); });
    tray_.setHandler(glaze::tr(demo::str::DoljFonstret), [this] { hide(); });
    tray_.setHandler(glaze::tr(demo::str::AnimeraIkonen), [this] {
        // Menyn har REDAN bytt sin bock (aktiveringen ändrar tillståndet
        // FÖRE handlern) — kryssrutan och ikonen följer efter.
        animera(!tray_.isAnimating());
    });
    // Dialogen är MODAL — medan den är öppen fyras ikonens klick inte, de
    // lyfter dialogen. Den har ingen ägare: systemfältets meny är APPENS och
    // inte fönstrets, och fönstret kan vara dolt när den öppnas.
    tray_.setHandler(glaze::tr(demo::str::MenuOm), [] {
        AboutDialog dlg;
        dlg.run();
    });
    tray_.setHandler(glaze::tr(demo::str::MenuAvsluta), [this] { avsluta(); });

    // HÄNDELSERNA i fullformen — sender är IKONEN, inte en widget.
    tray_.onClick = [this](glaze::TrayIcon* s, glaze::ClickArgs& e) {
        ikonKlickad(s, e);
    };
    tray_.onDoubleClick = [this](glaze::TrayIcon* s, glaze::ClickArgs& e) {
        ikonDubbelklickad(s, e);
    };
    tray_.onContextMenu = [this](glaze::TrayIcon* s, glaze::TrayMenuArgs& e) {
        ikonMeny(s, e);
    };
    tray_.onHover = [this](glaze::TrayIcon* s, glaze::HoverArgs& e) {
        ikonHover(s, e);
    };

    // Ikonen läggs i fältet när fönstret ÖPPNAS — också vid en dold start,
    // där `onLoad` fyras men inte `onShow`. Samtidigt LÅSES storleken
    // (min == max = ej ändringsbart) till den klientyta `setContentSize`
    // just räknats om till — ur stilens FAKTISKA chrome, så demon aldrig
    // behöver känna namnlistens eller statusradens höjd.
    onLoad = [this] {
        const gfx::Sizef k = clientSize();
        const int w = static_cast<int>(k.w + 0.5f);
        const int h = static_cast<int>(k.h + 0.5f);
        setMinimumSize(w, h);
        setMaximumSize(w, h);
        tray_.show();
    };
    onClose = [this](glaze::Widget* s, glaze::CloseArgs& e) { stangs(s, e); };
}

// ---------------------------------------------------------------- handlers

void TrayWindow::ikonKlickad(glaze::TrayIcon*, glaze::ClickArgs&)
{
    status_->setText(glaze::tr(demo::str::IkonKlick));
    if (isHidden())
        show();
    else
        hide();
}

void TrayWindow::ikonDubbelklickad(glaze::TrayIcon*, glaze::ClickArgs&)
{
    // Ett dubbelklick har redan gett ett klick (som växlade fönstret) — det
    // här visar det, dolt eller inte.
    status_->setText(glaze::tr(demo::str::IkonDubbelklick));
    show();
}

void TrayWindow::ikonMeny(glaze::TrayIcon* sender, glaze::TrayMenuArgs&)
{
    // FÖRE menyn öppnas — den tar sin ögonblicksbild när den öppnas, så det
    // som sätts här syns i den. Ikonens meny och tillstånd nås genom SENDER
    // (regel 6), fönstrets läge genom fönstret.
    std::vector<glaze::MenuItem>& poster = sender->menu().items;
    poster[0].enabled = isHidden();     // Visa fönstret
    poster[1].enabled = !isHidden();    // Dölj fönstret
    poster[2].checked = sender->isAnimating();
    status_->setText(glaze::tr(demo::str::IkonMeny));
}

void TrayWindow::ikonHover(glaze::TrayIcon*, glaze::HoverArgs& e)
{
    status_->setText(glaze::tr(e.entered ? demo::str::PekarenPa
                                         : demo::str::PekarenAv));
}

// STÄNG TILL SYSTEMFÄLTET: krysset döljer fönstret och appen lever vidare i
// ikonen. Bara FÖNSTRETS EGEN stängning vetas (`CloseReason::UserClosing`)
// — ett appavslut (Cmd+Q, appmenyns Avsluta) släpps igenom, annars
// hade appen aldrig gått att avsluta på macOS. Menyns "Avsluta" går genom
// `close()`, som också är `UserClosing`, och sätter därför flaggan först. Utan
// systemfält stängs det alltid — ett dolt fönster utan ikon hade inte gått att
// nå igen.
void TrayWindow::stangs(glaze::Widget*, glaze::CloseArgs& e)
{
    // Utan systemfält är kryssrutan okryssad och avstängd — då stängs alltid.
    e.cancel = e.reason == glaze::CloseReason::UserClosing &&
               stangTill_->isChecked() && !avslutar_;
    if (e.cancel)
        hide();
}

void TrayWindow::animera(bool pa)
{
    animera_->setChecked(pa);   // programmatiskt — fyrar inget onChange
    if (pa)
        tray_.startAnimation();
    else
        tray_.stopAnimation();
}

void TrayWindow::avsluta()
{
    avslutar_ = true;
    close();   // uppskjutet — `stangs` ser flaggan och släpper igenom
}
