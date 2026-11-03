#pragma once
#include <glaze/glaze.h>

#include <memory>
#include <string>
#include <vector>

#include "form_dialog.h"

namespace glaze {
class Terminal;
}

// SlateWindow — ARBETSBÄNKEN för Slates utseende, `demo --slate`: EN stor
// rullande sida med varje komponent på en gång, de flesta i både aktivt och
// inaktiverat läge, så att en ändring i Slates ritning, färger eller mått går
// att bedöma utan att leta bland flikar. Hover, tryck och öppna listor provas
// med musen.
//
// Fönstret låser UI:t till Slate och gör temat lätt att byta: remsan överst
// (◀ vallistan ▶ och läget), menyn Tema, F7/F8 för föregående och nästa tema,
// F9 för att växla mellan ljust och mörkt, och de tre lägesglyferna intill
// fönsterknapparna.
//
// Komponenterna står också där de hamnar i en app: i menyraden, i tre
// verktygsfält (två liggande och ett stående) och i STATUSRADEN, som bär en av
// varje kontroll för att visa hur de ser ut i den låga remsan.
//
// VERKTYGSFÄLTEN är flyttbara, i en `DockHost` runt sidan. Menyn Visa visar och
// döljer dem och lägger till och tar bort fler, så att den stora programikonen
// går att prova med och utan ett fält vid överkanten.
//
// SIDAN: raderna är `Rad` (en ren Bar) och cellerna delar radens bredd efter
// sin vikt, så allt följer fönstrets storlek. Texterna är literaler — det här
// är en verkstad för utseendet och inte en del av demons översatta yta.
class SlateWindow : public glaze::AppWindow {
public:
    SlateWindow();

private:
    // Bygget, ett block per del av fönstret.
    void byggMenyer();
    void byggTemaFalt();
    void byggVerktygsFalt();
    void byggStatusrad();
    void byggSida();

    // Sidans rader, uppifrån och ned. Varje funktion fyller en rad.
    void radKnapparOchVal();
    void radInmatning();
    void radListor();
    void radRemsorOchYtor();
    void radDockningOchText();
    void radDialoger();

    // En ny rad med höjden `h`, ankrad under den förra.
    glaze::Bar* rad(float h);

    // TEMAT: EN väg för remsan, menyn och tangenterna. `valjTema` tar ett index
    // i Slates temalista (utanför listan går det runt), och båda synkar
    // remsan, menyn och statusraden.
    void valjTema(int index);
    void valjLage(glaze::ThemeMode m);
    void vaxlaLage();      // till motsatsen av det som syns, också från System
    bool morktNu() const;
    void synkaUtseende();
    // Glyfen och menyposten, som följer varandra. De två ger namnlistens fyra lägen.
    void vaxlaStorIkon();
    void vaxlaMenyer();

    // Verktygsfälten i menyn Visa. `synkaFalt` följer också fältens egna
    // stängknappar, som går genom värdens `onChange`.
    void byggFaltMeny();
    void laggTillFalt();
    void taBortFalt();
    void synkaFalt();

    void visa(const std::string& text);   // statusradens första fält

    glaze::Panel*  sida_    = nullptr;
    std::string    sistaRad_;
    int            radNr_   = 0;

    int              temaIndex_ = 0;
    glaze::Dropdown* temaDd_    = nullptr;
    glaze::Menu*     temaMeny_  = nullptr;
    glaze::ToolButton* lageSystem_ = nullptr;
    glaze::ToolButton* lageLjust_  = nullptr;
    glaze::ToolButton* lageMorkt_  = nullptr;
    glaze::ToolButton* glyfSystem_ = nullptr;   // i namnlisten
    glaze::ToolButton* glyfLjust_  = nullptr;   // i namnlisten
    glaze::ToolButton* glyfMorkt_  = nullptr;   // i namnlisten
    glaze::ToolButton* storIkon_   = nullptr;   // i namnlisten
    glaze::ToolButton* menyGlyf_   = nullptr;   // i namnlisten

    glaze::DockHost* dock_         = nullptr;
    glaze::ToolBar*  temaFalt_     = nullptr;
    glaze::ToolBar*  verktyg_      = nullptr;
    glaze::ToolBar*  verktygSida_  = nullptr;
    std::vector<glaze::ToolBar*> extraFalt_;
    int              extraNr_      = 0;

    // Förloppet som rör sig, så att fyllnaden går att bedöma i rörelse.
    glaze::Timer     takt_;
    glaze::Progress* rorligt_ = nullptr;
    float            forlopp_ = 0.0f;

    glaze::Terminal* terminal_ = nullptr;   // startas i `onLoad`

    // Sidans och statusradens snabbmeny, dialogerna (behållna så att de öppnar
    // med förra valet) och det icke-modala formuläret.
    glaze::Menu                 snabbmeny_;
    glaze::FileDialog           filDialog_;
    glaze::ColorDialog          fargDialog_;
    std::unique_ptr<FormDialog> formular_;
};
