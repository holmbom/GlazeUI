#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"
#include "second_window.h"
#include "widget_gallery.h"

class StartWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    StartWindow();

private:
    glaze::Label rubrik;
    glaze::Label intro1;
    glaze::Label intro2;
    glaze::Panel utseende;
    glaze::Label utsRubrik;
    glaze::Separator utsSep;
    glaze::Label uiLbl;
    glaze::Dropdown uiDd;
    glaze::Label themeLbl;
    glaze::Dropdown themeDd;
    glaze::Label styleLbl;
    glaze::Dropdown styleDd;
    glaze::Label temaLbl;
    glaze::Dropdown temaDd;
    glaze::Label skalaLbl;
    glaze::Dropdown skalaDd;
    glaze::Label sprakLbl;
    glaze::Dropdown sprakDd;
    glaze::Label utsHint;
    glaze::Panel prova;
    glaze::Label provaRubrik;
    glaze::Separator provaSep;
    glaze::Button okBtn;
    glaze::Button temaBtn;
    glaze::Checkbox animate;
    glaze::TextInput namn;
    glaze::Label status;
    glaze::Button nyttBtn;
    glaze::Button galleriBtn;
    glaze::Button stang;
    glaze::Panel panel;
    glaze::Label panelRubrik;
    glaze::Separator panelSep;
    glaze::Button inre;
    glaze::Label scrollTxt;
    glaze::Button klipps;
    glaze::Panel komp;
    glaze::Label kompRubrik;
    glaze::Separator kompSep;
    glaze::Picture kompBild;
    glaze::Link kompLank;
    glaze::Radio valA;
    glaze::Radio valB;
    glaze::Slider skjut;
    glaze::Label skjutVarde;
    glaze::Progress forlopp;
    glaze::Checkbox obestamd;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig (den
    // appendar bara nya handler-deklarationer). En medlemsfunktion per
    // event, formen (sender, args) — hård regel 6.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void okBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void okBtnHovered(glaze::Widget* sender, glaze::HoverArgs& e);
    void animateChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void temaBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void namnChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void nyttFonsterClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void galleriClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void temaDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void uiDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void themeDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void styleDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void skalaDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void stangClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void kompLankClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void valChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void skjutChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void obestamdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);

    // MENYERNA. STRUKTUREN står i `.glazeui` som platta satser (hård
    // regel 8 — allt går i BÅDA vägarna), så previewn visar menyraden med
    // sina rubriker. Kvar här är bara det som ÄR kod: handlerna, som
    // kopplas på TITELVÄG, och snabbmenyn som inte hänger på menyraden.
    void byggMenyer();
    void syncMenyTillstand();   // menyns kryss/radio mot faktiskt state
    void snabbmenyBegard(glaze::Widget* sender, glaze::ContextMenuArgs& e);

    // Snabbmenyn är en MEDLEM och inte en lokal variabel: `popupAt` är
    // ICKE-blockerande, så menyn måste överleva anropet. En lokal `Menu`
    // hade förstörts direkt — och dess destruktor stänger sina öppna
    // presentationer, så menyn hade blinkat förbi.
    glaze::Menu snabbmeny_;

    void frame(const glaze::Ctx& c) override;   // egen ritning under widgets
    bool event(const gfx::Event& e) override;   // F12 = damage-debug

    // Kaskaden: ett byte av UI fyller om tema- och mått-väljarna med
    // den nya familjens alternativ. Delas av uiDdChanged och onLoad, så
    // start och drift inte kan glida isär.
    void syncThemeAndStyleDropdowns();

    // SPRÅKET. Två metoder, och de gör olika saker:
    //
    //   `sprakDdChanged`  användarens val → `glaze::app().post(...)`.
    //                     POSTNINGEN ÄR ETT KRAV och inte försiktighet:
    //                     `Dropdown::commit()` fyrar `onChange` och
    //                     anropar `close()` FÖRST DÄREFTER, alltså mitt i
    //                     popupens egen klick-dispatch — och
    //                     `setLanguage` river popups SYNKRONT. Ett direkt
    //                     anrop hade rivit fönstret vars event pågår.
    //   `syncSprak`       fyller om det som är LISTOR (items är kvar
    //                     `std::string` — appen fyller om vid språkbyte)
    //                     och markerar det aktiva språket. Kallas från
    //                     `onLoad` OCH från `onLanguageChanged`, så start
    //                     och drift inte kan glida isär.
    void sprakDdChanged(glaze::Widget* sender, glaze::ChangeArgs& e);
    void syncSprak();
    void onLanguageChanged() override;

    SecondWindow  second_;    // användaren äger fönsterobjekten (medlem/stack)
    WidgetGallery galleri_;   // alla nitton widgets, normalt + inaktiverat
    int clicks_ = 0;
};
