#pragma once
#include <memory>
#include <string>
#include <string_view>

#include <gfx/color.h>

#include "glaze/widget.h"

// ColorPicker — FÄRGVÄLJAREN (wxColourPickerCtrl).
// En FÄRGRUTA i ett fält: kontrollen visar den valda färgen, och ett tryck
// (eller Space/Ned/F4 — Enter bubblar till fönstrets standardknapp)
// öppnar en POPUP med tre sätt att välja —
// en PALETT med förvalda färger, tre reglage (röd · grön · blå) och ett
// HEXFÄLT (`#RRGGBB`). MÖNSTER: widgeten ritar SIG SJÄLV — draw()/
// contentSize() bor i src/widgets/color_picker.cpp med switch (c.ui) (hård
// regel 3); popupen och dess innehåll bor HELT i cpp:n, som
// `Dropdown::ListWindow`.
//
// VÅR, ALDRIG OS:ETS (behovsinventeringen: "Dialogerna är VÅRA"). Och en
// POPUP, inte en modal dialog: wxColourPickerCtrl öppnar en dialog, men en
// färg väljs bäst med resultatet synligt bakom — och dropdownens popup-
// kontrakt (`setPopupAnchor`: under kontrollen, vänd uppåt vid skärmkant,
// också på Wayland som saknar skärmrymd) ger det gratis.
//
// Reglagen är RÖD/GRÖN/BLÅ och inte nyans/mättnad/ljushet — VALT, med skäl:
//   * VÄRDET ÄR TRE BYTE (`gfx::Color`), och hexfältet är samma tre byte i
//     bas 16. Med RGB visar reglagen och fältet SAMMA sak i två former, varje
//     reglageläge är exakt ett värde, och ett reglage ändrar aldrig de andra.
//   * HSL-vägen förlorar: 8-bitars RGB går inte att räkna tur och retur genom
//     heltals-HSL, och NYANSEN är odefinierad för grått — ett mättnadsreglage
//     som passerar noll hade tappat nyansreglagets läge, alltså tillstånd som
//     inte är värdet. Att bära det tillståndet vid sidan av är precis den
//     dubbla sanning huset undviker.
//   * wxGenericColourDialog — wx:s egen, plattformsoberoende dialog och
//     alltså den närmaste förebilden till en dialog som är VÅR — är exakt
//     det här: en palett och tre RGB-reglage.
//
// VALET GÄLLER DIREKT, och TVÅ EVENTS säger hur långt det kommit:
// `onChange` när färgen ändrats och `onCommit` när användaren är KLAR —
//   PALETTEN   ett tryck på en ruta (eller Enter på den markerade) väljer OCH
//              stänger, dropdownradens recept: `onChange` och `onCommit`
//              tillsammans. Pilarna i paletten flyttar bara MARKÖREN.
//   Reglagen   LIVE under draget (`onChange` per läge — reglagets eget
//              recept); `onCommit` när musknappen SLÄPPS och färgen ändrats,
//              och vid varje tangent- och hjulsteg (då efter `onChange`).
//   HEXFÄLTET  committas som talfältets text: vid ENTER, när fokus lämnar det
//              (Tab vidare i popupen) och när popupen STÄNGS — då är det också
//              fokus som går. En tolkbar text blir färgen, `onChange` och
//              `onCommit` tillsammans; det som inte tolkas lämnar färgen
//              orörd, och texten skrivs om till den färg som gäller. Medan man
//              skriver är texten användarens. Fältet är inte live.
// En commit som sker i ett fokusbyte eller en stängning POSTAS till
// pump-svansen (`Widget::postEvent`); färgen sätts ändå direkt. ESCAPE och ett
// tryck UTANFÖR stänger popupen och ÅTERSTÄLLER INGENTING — det som gått live
// står kvar, och en hexfärg som skrivits committas av stängningen. Escape och
// klicket utanför betyder alltså samma sak; en Escape som ångrade hade gett
// den en annan betydelse än klicket, utan att det syns vilken.
//
// TANGENTBORDET: popupen tar det så länge den är öppen — Tab vandrar mellan
// paletten, reglagen och hexfältet, pilarna arbetar i den som har fokus,
// Enter stänger (i paletten: väljer och stänger), Escape stänger. Det är ett
// PLATTFORMSDELTA hur tangenterna når popupen, och det bor i fönstret
// (`AppWindow::setPopupTakesKeys`): på Windows/macOS/X11 landar de i
// ägarfönstret, som skickar dem vidare, på Wayland får popupen dem själv.
//
// HJULET: reglagen i popupen är värdewidgetar och tar hjulet BARA
// med fokus; den stängda kontrollen tar det aldrig (dropdownens regel).
//
// VÄRDET ÄR OPAKT som förval: `color()` bär alfa 255 — wxColourPickerCtrl
// utan `wxCLRP_SHOW_ALPHA`. ALFAN är opt-in med
// `setAllowAlpha(true)`: popupen får en fjärde reglagerad, A, hexfältet tar
// `#RRGGBBAA` (sex siffror är opakt, CSS:s regel), paletten behåller den
// aktuella alfan vid ett val, och färgrutan ritas över ett rutmönster så att
// genomskinligheten syns. Färgen LAGRAS med sin alfa och LÄSES opak när
// alfan inte tillåts — läsningens regel, så satsordningen inte avgör:
//     farg.setColor("#3366FF80");
//     farg.setAllowAlpha(true);
// ger alfan 0x80 i båda ordningarna.
//
// HÅRD REGEL 8 — formen i `.glazeui` är HEXTEXTEN, ett SETTER-fält:
//     farg.setColor("#3366FF");
// PLATT (en strängliteral), samma text som hexfältet visar och samma form som
// `Label`s `color`-nyckel alltid tagit — och därmed en vanlig textrad i
// GlazeBuilders egenskapspanel. `setColor(gfx::Color{…})` är kodens form och
// står bredvid; de två ger exakt samma tillstånd (och previewn utför båda,
// den senare med `gfx::Color`-literalens fyra kanaler, som `Label`). OGILTIG
// TEXT — fel antal siffror, något som inte är hex — LOGGAS och lämnar färgen
// ORÖRD; aldrig en gissning. `farg.setAllowAlpha(true);` är också ett
// setter-fält.
//
// INTE HÄR med flit: en EGEN palett (en lista appen fyller hade krävt
// kollektionsvokabulären `addPaletteColor`/…), "senast använda" färger,
// pipett — och TOOLTIPS i popupen: ramverket visar ingen tooltip medan en
// popup lever (src/tooltip.cpp), och popupen ÄR en popup. Samma yta som en
// MODAL dialog är `glaze::ColorDialog` (glaze/color_dialog.h).

namespace glaze {

class ColorPicker : public Widget {
public:
    // BÅDA out-of-line (color_picker.cpp): popupen är ofullständig här, och
    // även en defaultad inline-ctor kräver medlemmarnas destruktorer.
    ColorPicker();
    ~ColorPicker() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern är
    // den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG onChange.
    //
    // Färgen — LAGRAD med sin alfa, läst opak utan `allowAlpha` (se ovan).
    // En öppen popup följer med.
    void setColor(gfx::Color c);
    // TEXTFORMEN `#RRGGBB` eller `#RRGGBBAA` (`#` valfritt, versaler eller
    // gemener, blanksteg runt om tål den). Allt annat ⇒ `gfx::log` + färgen
    // ORÖRD.
    void setColor(std::string_view text);

    // Den EFFEKTIVA färgen: opak utan `allowAlpha`.
    gfx::Color  color() const;
    // Färgen i textformen — `#` och sex VERSALA hexsiffror (åtta med
    // alfan), det hexfältet visar och `setColor(text)` tar.
    std::string colorText() const;

    // ALFAN (se ovan). Ett byte stänger en öppen popup.
    void setAllowAlpha(bool on);
    bool allowAlpha() const { return allowAlpha_; }

    // ANVÄNDAREN ändrade färgen — läs den ur sender (`color()`), regel 6 —
    // och är KLAR med den (`onCommit`, se ovan). Baslinjen för `onCommit` är
    // färgen vid förra commiten; den sätts också när fokus kommer och av en
    // programmatisk `setColor` som byter färgen.
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;

    // Det SKRIVNA i en öppen popups hexfält committas NU och synkront, som
    // Enter, och popupen står kvar (widget.h). Reglagen har inget som väntar.
    void commitEdit() override;

    bool isOpen() const { return open_; }
    void open();    // no-op utan ett öppet fönster, och när den är inaktiv
    void close();   // uppskjuten, som varje popups

    bool focusable() const override { return true; }

    // setProp-egenskaper: color (textformen ovan — ogiltig färg loggas och
    // nyckeln räknas ändå som känd) och allowAlpha.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;
    void       mouseDown(const Ctx& c, float mx, float my) override;
    void       focusGained() override { commitBase_ = color(); }
    void       focusLost() override { close(); }
    void       enabledChanged() override;

    // BORTTAGNINGENS DENY-CHECK — dropdownens: en färgväljare med ÖPPEN popup
    // får inte lossas ur trädet (stängningen är uppskjuten).
    bool       hasOpenPopup() const override { return isOpen(); }

    // ANVÄNDARVÄGEN för ett AVSLUTAT val — paletten och hexfältets commit:
    // `onChange` när färgen ändrades och `onCommit` när den skiljer sig från
    // förra commiten (ListBoxens `selectFromUser`-prejudikat). Protected så
    // en subklass, och en grind, når den. Reglagens drag och släpp har egna
    // vägar in i samma `userColor`.
    void colorFromUser(gfx::Color c);

private:
    class Popup;   // popupfönstret och dess innehåll — color_picker.cpp

    // ALLA ANVÄNDARVÄGAR går hit. `klar` = användaren är klar (onCommit kan
    // fyras), annars LIVE (bara onChange — reglagets drag). `postad` = vi
    // står i ett fokusbyte eller en stängning, och eventen väntar på
    // pump-svansen; färgen sätts ändå direkt.
    void userColor(gfx::Color c, bool klar, bool postad);
    void fire(bool change, bool commit);

    // GEOMETRIN — EN sanning i widget-lokala LOGISKA px, som tar ytan `b`:
    // pilen står där dropdownens står, färgrutan fyller resten av fältet.
    gfx::Rectf chevronRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf swatchRect(const Ctx& c, const gfx::Rectf& b) const;

    void openAt(const Ctx& c);
    void popupClosed();   // ur popupens onClose

    std::unique_ptr<Popup> popup_;   // lat — skapas vid första open
    gfx::Color             color_{};   // svart, som wx och WinForms — RÅ alfa
    gfx::Color             commitBase_{};   // den EFFEKTIVA vid förra commiten
    bool                   open_       = false;
    bool                   allowAlpha_ = false;
};

} // namespace glaze
