#pragma once
#include <string>

#include <glaze/glaze.h>
#include <glaze_terminal/terminal.h>   // sidopaketet Terminal — UTANFÖR regionen

#include "demo_text.h"

// DOCKNINGEN — `demo --dock`: en
// dockningsyta med fem paneler och ett dokument i huvudytan. Utforskaren och
// Sök är FLIKAR i samma grupp, Egenskaper är STAPLAD under dem och Utdata
// står vid nederkanten. Utforskaren har ett minsta och ett största mått.
// Egenskaper är ONÅLAD: ett handtag vid vänsterkanten som fäller ut
// panelen som OVERLAY ovanpå dokumentet — den fälls in när pekaren och fokus
// lämnat den, och knappnålen i dess rubrik fäster den i stapeln. Ytterst står
// VERKTYGSFÄLTEN i band: Standard och Redigera i band 0 överst, Felsök i
// band 1 under dem och Verktyg på högkant vid vänsterkanten — varje fält
// dras i sitt grepp till ett annat band, en annan plats eller en annan kant —
// utom Redigera, som bara dockas uppe eller nere (`setAllowedEdges`).
// Layout-menyns "Mät" har genvägen M utan modifierare: den körs med fokus på
// en knapp, men ett m som skrivs i ett fält skrivs (text vinner över genvägen).
// "Ny" och "Ångra" är rullgardinsknappar (Ångra är DELAD), och "Nytt
// verktygsfält" adopterar ett fält under körning, som hamnar sist i sitt band.
// Bevakning STARTAR FLYTANDE (`setFloating(true)` i `.glazeui`): ett eget
// fönster mitt över huvudfönstret, ägt av det. Ett dubbelklick på rubriken —
// eller "Docka" i dess meny — dockar den vid högerkanten, och ett rubrikdrag
// ut ur fönstret lägger vilken panel som helst flytande där spöket stod.
//
// Hela startlayouten står i `.glazeui` — trädbygget ger ordningen, och EFTER
// det formar värdens `dockPanel` (panelerna som identifierare) flikgruppen
// och stapeln, och `activate()` väljer fliken. Det är hård regel 8 för
// dockningen: designern och koden är samma väg. Menyernas STRUKTUR står där
// också; handlerna är kod (`onLoad`).
//
// TERMINALEN är en flik bredvid Utdata: OS:ets skal i
// sidopaketets `glaze::Terminal`, startat i `onLoad`. Titeln programmet sätter
// blir flikens rubrik, slutkoden hamnar i statusraden, och snabbmenyn har
// Kopiera och Klistra in (paketet har ingen egen meny — den är appens).
// Ctrl+Tab och ett musklick är vägarna ut; Tab och Escape går till skalet.
//
// Visa-menyn tar fram en panel (`activate()` öppnar en stängd, väljer dess
// flik och fäller ut den), Layout-menyn sparar och läser tillbaka strängen,
// och statusraden visar `layout()` efter varje ändring användaren gör.
class DockWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    DockWindow();

private:
    glaze::Label statusTxt;
    glaze::DockHost dock;
    glaze::ToolBar standard;
    glaze::ToolButton ny;
    glaze::ToolButton angra;
    glaze::Separator standardSep;
    glaze::ToolButton nyttFalt;
    glaze::ToolBar redigera;
    glaze::ToolButton klipp;
    glaze::ToolButton kopiera;
    glaze::ToolButton klistra;
    glaze::ToolBar felsok;
    glaze::ToolButton kor;
    glaze::ToolButton stega;
    glaze::ToolBar verktyg;
    glaze::ToolButton verktygOk;
    glaze::ToolButton verktygAvbryt;
    glaze::ToolButton verktygInfo;
    glaze::ToolButton verktygKugg;
    glaze::DockPanel utforskare;
    glaze::Label minMax;
    glaze::ListBox filer;
    glaze::DockPanel sok;
    glaze::Label sokEtikett;
    glaze::TextInput sokFalt;
    glaze::DockPanel egenskaper;
    glaze::PropertyGrid egenskapsLista;
    glaze::DockPanel utdata;
    glaze::Label utdataText;
    glaze::DockPanel terminalPanel;
    glaze::Terminal terminal;
    glaze::DockPanel bevakning;
    glaze::Label bevakningText;
    glaze::ListBox bevakningLista;
    glaze::Panel dokument;
    glaze::Label dokumentText;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void dockAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void verktygKlickat(glaze::Widget* sender, glaze::ClickArgs& e);
    void nyttFaltKlickat(glaze::Widget* sender, glaze::ClickArgs& e);
    void terminalSlut(glaze::Widget* sender, glaze::EventArgs& e);
    void terminalTitel(glaze::Widget* sender, glaze::EventArgs& e);

    void visaLayout();

    std::string start_;    // startlayoutens sträng — Layout ▸ Startlayouten
    std::string sparad_;   // den sparade — en riktig app lagrar den på disk
    int         extra_ = 0;   // fälten "Nytt verktygsfält" har skapat
    glaze::Menu terminalMeny_;   // terminalens snabbmeny — appens, inte paketets
};
