#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include "glaze/menu.h"
#include "glaze/widget.h"

// AppIcon — PROGRAMIKONEN och PROGRAMMENYN (Office
// 2007-knappen). Ett publikt VÄRDE-FÄLT på `glaze::AppWindow`
// (`window.appIcon`), skapat med fönstret och adopterat LÅNAT i
// konstruktorn — menuBar/statusBar-prejudikatet, med en skillnad: den är
// SYNLIG som förval, för varje GlazeUI-fönster har en ikon.
//
// **TRE TILLSTÅND** — och `hide()` är ortogonalt mot alla tre:
//   * `isNoIcon()`   INGEN ikon alls: ingen plats i namnlisten och ingen
//                    ikon hos OS:et (aktivitetsfältet, Alt+Tab, dockan).
//   * giltig `icon()` appens egen bild — EN bild räcker (gärna 256 × 256);
//                    den räknas om till varje storlek, och `setSmallIcon`
//                    ger en handritad liten vid sidan av. Ett `ImageSource`:
//                    en asset eller en `glaze::Bitmap` — pixlarna räknas om på
//                    CPU:n till både namnlisten och OS:et och blir skarpa.
//                    Ändras bitmapens pixlar når de OS:et med ett nytt
//                    `setIcon(bild)` (en ny version är en ändring); en
//                    `RenderTexture` kan inte bli en OS-ikon (loggas).
//   * annars         FÖRVALET, GlazeUI-ikonen som är inbäddad i ramverket.
//   `appIcon.hide()` döljer ikonen I FÖNSTRET men lämnar OS-ikonen kvar
//   (WinForms `ShowIcon = false`) — ramverkets dialoger gör det.
//
// **PLATSEN** sitter i CHROME-trädet som TopBar: ett rotbarn utanför
// dock-flödet, placerat mot hela klientytan — vänstra hörnet på Windows och
// Linux, HÖGRA på macOS (där sitter fönsterknapparna till vänster). Den är
// en kvadrat med namnlistens höjd, och med `setLarge(true)` OCH en synlig
// menyrad täcker den namnlist + menyrad och menyraden börjar bredvid. Stor
// är en BEGÄRAN: utan menyrad blir ikonen liten. Geometrin är EN sanning,
// `reservedWidth`, som namnlistens motvikt, menyradens rubriker och
// namnlistens hårlinje läser.
//
// **PROGRAMMENYN** byggs med MENYRADENS vokabulär (hård regel 8), där den
// TOMMA stigen är roten:
//
//     appIcon.addItem({}, glaze::tr(1002), "Ctrl+N");
//     appIcon.addSubmenu({}, "Exportera");
//     appIcon.addItem("Exportera", "PDF");
//     appIcon.setHandler({"Exportera", "PDF"}, [this] { exportera(); });
//
// Klick på ytan öppnar den; utan poster händer ingenting. Ytan är KLICKBAR
// och inte dragyta (`HitRole::Client`). Genvägarna går genom fönstrets
// acceleratorväg EFTER menyraden — programmenyn ersätter inget i den.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() i
// src/widgets/app_icon.cpp med `switch (c.ui)`.

namespace glaze {

class AppIcon : public Widget {
public:
    AppIcon();
    ~AppIcon();

    // ---------------------------------------------------------------- bilden
    // Ogiltig bild = FÖRVALET (GlazeUI-ikonen). Settrarna invaliderar och
    // lägger om OS-fönstrets ikon när fönstret är öppet.
    void               setIcon(ImageSource img);
    const ImageSource& icon() const { return icon_; }
    // Den LILLA bilden vid sidan av (en handritad 16 eller 32) — valregeln
    // tar den i de storlekar den räcker till och den stora över dem.
    void               setSmallIcon(ImageSource img);
    const ImageSource& smallIcon() const { return small_; }
    // INGEN IKON ALLS — varken i namnlisten eller hos OS:et.
    void setNoIcon(bool on);
    bool isNoIcon() const { return noIcon_; }
    // STOR ikon över namnlist + menyrad — en begäran, som bara gäller medan
    // menyraden syns.
    void setLarge(bool on);
    bool isLarge() const { return large_; }

    // ---------------------------------------------------------- programmenyn
    // MenuBars platta form, med den TOMMA stigen `{}` som roten. Okänd stig
    // loggas och ignoreras (regel 4-arvet).
    void addItem(const MenuPath& path, TrText text,
                 const std::string& shortcut = "");
    void addCheck(const MenuPath& path, TrText text, bool checked = false);
    void addRadio(const MenuPath& path, TrText text, bool checked = false);
    void addSeparator(const MenuPath& path);
    void addSubmenu(const MenuPath& path, TrText text);

    // Handlern på en stig — TEMPLATE av MenuBar::setHandlers skäl (den
    // parameterlösa formen adapteras i `Handler::operator=`). Previewn
    // utför den inte; den kompilerade koden gör det.
    template <class F>
    void setHandler(const MenuPath& path, F&& f)
    {
        if (MenuItem* it = itemAtPath(path))
            it->onClick = std::forward<F>(f);
        else
            logMissingPath("setHandler", path);
    }

    // MENYPOSTENS TILLSTÅND — MenuBars tre platta mutatorer (samma namn på
    // menyraden och systemfältet, och SAMMA tillstånd som trädformen
    // `itemAtPath(stig)->icon = …`): postens bild, `enabled` och kryss.
    // Okänd stig loggas och ignoreras; den tomma stigen är menyn och ingen
    // post. Ett kryss på en radiopost avmarkerar inte syskonen.
    void setItemIcon(const MenuPath& path, ImageSource icon);
    void setItemEnabled(const MenuPath& path, bool on);
    void setItemChecked(const MenuPath& path, bool on);

    // Posten på en stig, eller nullptr. Den tomma stigen är menyn och ingen
    // post, så den svarar nullptr.
    MenuItem* itemAtPath(const MenuPath& path);

    // TRÄD-API:t för handkod — SAMMA tillstånd som den platta formen.
    // Referensen är stabil (menyn är ett fält).
    Menu& menu() { return menu_; }

    void openMenu();
    void closeMenu();
    bool isMenuOpen() const { return open_; }

    // ACCELERATORVÄGEN — fönstret frågar EFTER menyraden. true = konsumerad.
    bool tryShortcut(gfx::Key k, std::uint32_t mods);

    // Programmenyn öppnas med musen och genvägarna — ikonen är inget
    // tabbstopp och stjäl aldrig fokus (menyradens val).
    bool focusable() const override { return false; }

    // GEOMETRINS EN SANNING i LOGISKA px: hur brett ikonen tar av raden,
    // räknat från fönsterkanten på sin sida (`onRightSide`). 0 när den är
    // dold, saknar plats (`noIcon`, ingen namnlist) eller — för `Menu` —
    // inte är stor.
    enum class Row { Title, Menu };
    float reservedWidth(const Ctx& c, Row row) const;
    // HÖGRA hörnet på macOS, vänstra annars.
    static constexpr bool onRightSide()
    {
#ifdef __APPLE__
        return true;    // fönsterknapparna sitter till vänster på macOS
#else
        return false;
#endif
    }

    // Bildparet som GÄLLER — egen bild eller ramverkets förval. EN regel för
    // namnlistens ritning och OS-ikonen (`AppWindow::applyWindowIcon`).
    std::pair<ImageSource, ImageSource> effectiveIcons() const;

    // setProp-egenskaper: icon, smallIcon ("lista/nyckel"), large, noIcon;
    // resten tar basen. Programmenyn byggs med add-satserna, inte med props.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float mx, float my) override;

private:
    // Täcker ikonen namnlist + menyrad JUST NU (begärd, med plats och med
    // synlig menyrad)?
    bool showsLarge() const;
    // Platsen ändrades (storlek, synlighet) — rita om och lägg om syskonen.
    void placeChanged();
    // OS-fönstrets ikon följer bilden (`AppWindow::applyWindowIcon`).
    void pushWindowIcon();

    static void logMissingPath(const char* fn, const MenuPath& path);

    ImageSource icon_{};
    ImageSource small_{};
    bool        noIcon_ = false;
    bool        large_  = false;
    bool        open_   = false;   // programmenyn visas
    Menu        menu_;
};

} // namespace glaze
