#include "dock_window.h"

namespace {

// MENYSTIGARNA byggs av SAMMA id som `.glazeui` skriver — matchningen är id
// mot id, alltså språkoberoende (start_window.cpp-mönstret).
glaze::MenuPath visa(std::uint32_t id)
{
    return glaze::MenuPath({glaze::tr(demo::str::DocknVisa), glaze::tr(id)});
}

glaze::MenuPath layoutMeny(std::uint32_t id)
{
    return glaze::MenuPath({glaze::tr(demo::str::DocknLayout), glaze::tr(id)});
}

} // namespace

// Startlogik bor i onLoad — konstruktorn är genererad plumbing. Strukturen
// (menyerna, panelerna, flikgruppen och stapeln) står i `.glazeui`; här är
// bara det som ÄR kod: handlerna.
void DockWindow::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    // STARTLAYOUTEN som sträng — `.glazeui` har redan format den, så den
    // läses tillbaka i stället för att skrivas en gång till här.
    start_ = dock.layout();

    // VISA: `activate()` öppnar en stängd panel, väljer dess flik och fäller
    // ut den — "visa den här panelen", var den än är.
    menuBar.setHandler(visa(demo::str::DocknUtforskare),
                       [this] { utforskare.activate(); });
    menuBar.setHandler(visa(demo::str::DocknSok), [this] { sok.activate(); });
    menuBar.setHandler(visa(demo::str::DocknEgenskaper),
                       [this] { egenskaper.activate(); });
    menuBar.setHandler(visa(demo::str::DocknUtdata),
                       [this] { utdata.activate(); });
    // En FLYTANDE panel öppnas i sitt fönster, som lyfts.
    menuBar.setHandler(visa(demo::str::DocknBevakning),
                       [this] { bevakning.activate(); });

    // LAYOUT: strängen sparas och läses tillbaka (`layout()`/`setLayout()`).
    // Den programmatiska vägen fyrar aldrig `onChange`, så statusraden
    // skrivs här.
    menuBar.setHandler(layoutMeny(demo::str::DocknSpara), [this] {
        sparad_ = dock.layout();
        statusTxt.setText(trs(demo::str::DocknSparadPrefix) + sparad_);
    });
    menuBar.setHandler(layoutMeny(demo::str::DocknAterstall), [this] {
        if (sparad_.empty()) {
            statusTxt.setText(glaze::tr(demo::str::DocknIngenSparad));
            return;
        }
        if (dock.setLayout(sparad_))
            visaLayout();
    });
    menuBar.setHandler(layoutMeny(demo::str::DocknStart), [this] {
        if (dock.setLayout(start_))
            visaLayout();
    });
    // EN GENVÄG UTAN MODIFIERARE: "M" kör "Mät"
    // med fokus på en knapp, men ett m som skrivs i ett fält — också i en
    // FLYTANDE panels fält, där fönstrets reserv frågar ägarens genvägar —
    // skrivs och kör ingenting: text vinner över genvägen.
    menuBar.setHandler(layoutMeny(demo::str::DocknMat), [this] {
        statusTxt.setText(glaze::tr(demo::str::DocknMatKord));
    });

    // AKTIVERINGEN: dubbelklick eller Enter på en fil skriver den i
    // statusraden — raden läses ur sender, som i listvyn.
    filer.onActivate = [this] {
        statusTxt.setText(filer.items()[std::size_t(filer.selectedIndex())]);
    };

    // RULLGARDINERNA: menyernas STRUKTUR står i `.glazeui`, handlerna
    // per stig här — en post i roten `{}` är sin egen text, samma id som
    // `.glazeui` skriver. "Ny" har menyn bakom hela knappen; "Ångra" är
    // delad — knappen ångrar det senaste (`verktygKlickat`), pilen visar
    // historiken.
    for (const std::uint32_t id :
         {demo::str::NyDokument, demo::str::NyMapp})
        ny.setHandler(glaze::tr(id), [this, id] {
            statusTxt.setText(trs(demo::str::MenyvalPrefix) + trs(id));
        });
    for (const std::uint32_t id :
         {demo::str::HistorikSkriv, demo::str::HistorikFlytta,
          demo::str::HistorikRadera})
        angra.setHandler(glaze::tr(id), [this, id] {
            statusTxt.setText(trs(demo::str::AngratPrefix) + trs(id));
        });

    // TERMINALEN: OS:ets skal, startat HÄR och aldrig i
    // konstruktorn. Snabbmenyn är appens (paketet äger inga strängar) och
    // återanvänder Redigera-fältets Kopiera och Klistra in; Kopiera är
    // inaktiv utan markering.
    menuBar.setHandler(visa(demo::str::DocknTerminal),
                       [this] { terminalPanel.activate(); });
    terminalMeny_.addItem(glaze::tr(demo::str::DocknKopiera)).onClick =
        [this] { terminal.copy(); };
    terminalMeny_.addItem(glaze::tr(demo::str::DocknKlistra)).onClick =
        [this] { terminal.paste(gfx::clipboardText()); };
    terminal.onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
        terminalMeny_.items[0].enabled = terminal.hasSelection();
        terminalMeny_.popupAt(*s, e.x, e.y);
    };
    if (!terminal.start())
        statusTxt.setText(trs(demo::str::TermFel) + terminal.lastError());
    else
        visaLayout();
}

// Programmet i terminalen avslutades — slutkoden läses ur sendern (regel 6).
void DockWindow::terminalSlut(glaze::Widget*, glaze::EventArgs&)
{
    statusTxt.setText(trs(demo::str::TermSlut) + std::to_string(terminal.exitCode()));
}

// Titeln programmet satt (OSC 0/2) blir flikens rubrik; utan titel "Terminal".
void DockWindow::terminalTitel(glaze::Widget*, glaze::EventArgs&)
{
    if (terminal.title().empty())
        terminalPanel.setText(glaze::tr(demo::str::DocknTerminal));
    else
        terminalPanel.setText(terminal.title());
}

// Användaren ändrade layouten — en panel dragen, en flik vald, ett skikt
// draget, en panel infälld eller stängd. Platsen att spara strängen på.
void DockWindow::dockAndrad(glaze::Widget*, glaze::ChangeArgs&)
{
    visaLayout();
}

void DockWindow::visaLayout()
{
    statusTxt.setText(trs(demo::str::DocknLayoutPrefix) + dock.layout());
}

// En verktygsknapp — knappens text eller, för en ikonknapp, dess namn.
void DockWindow::verktygKlickat(glaze::Widget* sender, glaze::ClickArgs&)
{
    const auto* knapp = dynamic_cast<glaze::ToolButton*>(sender);
    if (!knapp)
        return;
    statusTxt.setText(trs(demo::str::DocknVerktygPrefix) +
                      (knapp->text().empty() ? knapp->name : knapp->text()));
}

// ETT FÄLT UNDER KÖRNING: värden adopterar det, och det hamnar SIST i
// sitt band — `band()` är 0, alltså det yttersta bandet vid överkanten. Ett
// ägt barn (`add`), så det lever med värden. Titeln är det flytande fältets
// rubrik.
void DockWindow::nyttFaltKlickat(glaze::Widget*, glaze::ClickArgs&)
{
    auto* falt  = dock.add<glaze::ToolBar>();
    falt->name  = "extra" + std::to_string(++extra_);
    falt->setText(glaze::tr(demo::str::DocknExtra));
    auto* knapp = falt->add<glaze::ToolButton>(glaze::tr(demo::str::DocknExtra));
    knapp->name = falt->name + "Knapp";
    knapp->onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) {
        verktygKlickat(s, e);
    };
    visaLayout();
}
