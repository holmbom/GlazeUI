#pragma once
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "glaze/menu.h"
#include "glaze/widgets/bar.h"

// MenuBar — menyraden. Ett publikt VÄRDE-FÄLT på
// `glaze::AppWindow` (`window.menuBar`), skapat med fönstret, adopterat i
// konstruktorn direkt efter TopBar och DOLT tills något gör
// `menuBar.show();` — exakt statusBar-prejudikatet, hela vägen: dold widget
// skär ingen remsa, trädordningen blir deterministisk, och `show()` är en
// riktig C++-sats som därför kan stå i `.glazeui`.
//
// **DEN LIGGER I KLIENTYTAN, inte i chromet.** TopBar är chrome och dess
// barn får hit-roller i `AppWindow::pushHitRegions`; menyraden ska inte
// dit — den får ingen Caption-roll, och hit-test-kartan behöver därför inte
// röras alls. Den kan heller inte hamna ovanpå TopBar, som resolvas mot en
// separat chrome-ctx.
//
// **TITLARNA ÄR DATA, inte barn** — och det är en nödvändighet, inte en
// smaksak: `AppWindow::collectFocusable` rekurserar OVILLKORLIGT i barnen,
// så en rubrik som widget hade blivit ett tabbstopp per meny. Samma val som
// ListBox och PropertyGrid gjorde för sina rader.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — `draw()`/`contentSize()` i
// src/widgets/menu_bar.cpp med `switch (c.ui)`. Ytan ärvs från `Bar`
// (`fillBody`/`innerEdge`), rubrikerna ritas här.
//
// MENYN BYGGS I BÅDA VÄGARNA (hård regel 8): STRUKTUREN som platta
// `.glazeui`-satser (`addMenu`/`addItem`/`addSeparator`/`addSubmenu`/
// `addCheck`/`addRadio` med TITELVÄG som stig — se nedan), HANDLERNA i kod
// via `setHandler`. Previewn utför strukturen och visar därför menyraden
// med sina rubriker; `setHandler` utförs medvetet INTE, eftersom argumentet
// är en lambda och previewn aldrig kör användarkod — samma behandling som
// event-kopplingar redan får.
//
// Träd-API:t (`addMenu(...) -> Menu&`) finns kvar för handkod och
// producerar EXAKT samma tillstånd; det kan bara inte emitteras, eftersom
// grammatiken tar satser och inte uttryck med returvärden.

namespace glaze {

class MenuBar : public Bar {
public:
    MenuBar();
    ~MenuBar();

    // Lägg till en rubrik och få MENYN att fylla. Referensen är STABIL
    // (menyerna ligger bakom `unique_ptr`) — till skillnad från
    // `Menu::addItem`, som pekar in i en vektor. Det är inte en
    // inkonsekvens utan en nödvändighet: `Menu` är varken kopierbar eller
    // flyttbar, eftersom dess öppna presentationer pekar tillbaka på den.
    Menu& addMenu(TrText title);

    int   menuCount() const { return static_cast<int>(entries_.size()); }
    // Den UPPLÖSTA rubriken. `menuTitleToken` bär identiteten.
    const std::string& menuTitle(int i) const;
    const TrText&      menuTitleToken(int i) const;
    Menu* menuAt(int i);
    void  clearMenus();

    // ---------------------------------------------------- DEN PLATTA FORMEN
    //
    // HÅRD REGEL 8: allt ska gå att göra i BÅDA vägarna. Träd-API:t ovan
    // (`addMenu(...)` → `Menu&` → `addItem(...)` → `MenuItem&`) är bekvämt
    // i handkod men kan ALDRIG emitteras: grammatiken tar satser, inte
    // uttryck med returvärden och referenser in i vektorer.
    //
    // Därför den här formen, som är rena `<id>.<metod>(argument);`-satser
    // och därmed både SKRIVBAR av GlazeBuilder och LÄSBAR av previewn:
    //
    //     menuBar.addMenu("Arkiv");
    //     menuBar.addItem("Arkiv", "Nytt fönster", "Ctrl+N");
    //     menuBar.addSeparator("Arkiv");
    //     menuBar.addSubmenu("Utseende", "Läge");
    //     menuBar.addRadio({"Utseende", "Läge"}, "System");
    //
    // STIGEN är TITELVÄGEN, inte index — den säger vad den betyder, medan
    // "1.0" hade krävt att man räknar. Den är en
    // **SEGMENTERAD `MenuPath`** och inte en sträng splittad på `/`:
    // segmenten är var för sig ett token (literal eller `glaze::tr(id)`),
    // och ett enda `TrText` kan aldrig bära två stabila id.
    // `/`-formen UTGÅR — ett snedstreck i ett literalsegment avvisas med
    // en loggrad, så migreringen SYNS i stället för att tystna.
    //
    // Tom stig = ogiltig. Okänd stig loggas och ignoreras (regel 4-arvet):
    // en meny som fått en ny rubrik ska inte spränga previewn.
    //
    // De två vägarna producerar EXAKT samma tillstånd — `addItem` nedan
    // anropar samma `Menu::addItem` som handkoden.
    void addItem(const MenuPath& path, TrText text,
                 const std::string& shortcut = "");
    void addCheck(const MenuPath& path, TrText text, bool checked = false);
    void addRadio(const MenuPath& path, TrText text, bool checked = false);
    void addSeparator(const MenuPath& path);
    void addSubmenu(const MenuPath& path, TrText text);

    // Handlern på en stig. `.glazeui` emitterar den som en vanlig
    // Call-sats med en lambda som argument; previewn UTFÖR den inte (den
    // ritar bara, precis som den hoppar över event-kopplingar), medan den
    // kompilerade koden gör det.
    // TEMPLATE med flit: `Handler` adapterar den PARAMETERLÖSA formen i sin
    // `operator=`, inte i sin konstruktor, så en `Handler`-PARAMETER hade
    // tvingat varje anropsställe till fullformen. Med forwarding får
    // menyposter exakt samma frihet som widgetarnas event (regel 6):
    //     menuBar.setHandler({"Arkiv", "Avsluta"}, [this] { close(); });
    template <class F>
    void setHandler(const MenuPath& path, F&& f)
    {
        if (MenuItem* it = itemAtPath(path))
            it->onClick = std::forward<F>(f);
        else
            logMissingPath("setHandler", path);
    }

    // MENYPOSTENS TILLSTÅND i den platta formen (`.glazeui`): postens bild (en asset eller pixlar),
    // om den går att välja och dess kryss.
    //     menuBar.setItemIcon({"Arkiv", "Spara"}, glaze::img("toolbar", "ok"));
    //     menuBar.setItemEnabled({"Arkiv", "Spara"}, false);
    //     menuBar.setItemChecked({"Visa", "Rutnät"}, true);
    // SAMMA tillstånd som trädformen `itemAtPath(stig)->enabled = false;`, och
    // samma tre namn på programikonen och systemfältet (en implementation,
    // src/menu_path.h). Okänd stig loggas och ignoreras. Ett kryss på en
    // radiopost avmarkerar inte syskonen — det gör bara användarens val.
    void setItemIcon(const MenuPath& path, ImageSource icon);
    void setItemEnabled(const MenuPath& path, bool on);
    void setItemChecked(const MenuPath& path, bool on);

    // Posten på en titelväg, eller nullptr. Publik för testerna och för
    // appkod som vill ändra en post byggd i `.glazeui`.
    MenuItem* itemAtPath(const MenuPath& path);

    // ACCELERATORVÄGEN: fråga varje meny, rekursivt, efter en
    // AKTIVERAD post vars genväg matchar. true = konsumerad.
    // `AppWindow::handleEvent` anropar den EFTER att den fokuserade
    // widgeten tackat nej och FÖRE Tab-vandringen — en permanent grab hade
    // gått före fokuserad widget och stulit pilar och text.
    bool tryShortcut(gfx::Key k, std::uint32_t mods);

    // Öppna/stäng en rubriks meny programmatiskt (menyradens egen
    // navigering använder dem, och testerna).
    void openMenu(int i);
    void closeMenu();
    int  openIndex() const { return open_; }

    // Titlarna är data ⇒ INGET tabbstopp, av samma skäl som raderna i en
    // lista inte är widgets.
    bool focusable() const override { return false; }

    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;

private:
    struct Entry {
        TrText                title;   // TOKEN
        std::unique_ptr<Menu> menu;    // STABIL adress — se addMenu
    };

    // GEOMETRIN ÄR EN SANNING (regel 3): ritningen och hit-testen läser
    // samma två, i widget-lokala LOGISKA px.
    gfx::Rectf titleRect(const Ctx& c, int i) const;
    int        titleAt(const Ctx& c, float mx, float my) const;
    // Var den första rubriken börjar — efter den stora programikonen
    // annars vid kanten.
    float      titleStart(const Ctx& c) const;

    // Stigupplösningen för den platta formen — vektorn posterna ska in i.
    std::vector<MenuItem>* resolvePath(const MenuPath& path);
    static void logMissingPath(const char* fn, const MenuPath& path);

    void setHover(int i);

    std::vector<Entry> entries_;
    int open_  = -1;   // rubrik med öppen meny
    int hover_ = -1;
};

} // namespace glaze
