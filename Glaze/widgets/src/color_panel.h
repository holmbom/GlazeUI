#pragma once
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>

#include <gfx/color.h>

#include "glaze/widget.h"
#include "glaze/widgets/textinput.h"

// FÄRGYTAN — INTERN header (`text_field.h`s och
// `sash.h`s prejudikat): paletten, reglagen och hexfältet som
// färgväljarens popup OCH färgdialogen (glaze/color_dialog.h) visar.
// Delad i den här headern och inte TU-lokal i color_picker.cpp: en
// andra kopia i dialogen vore två färgytor att hålla i takt. Måtten
// kommer ur stilen (`color*`), färgerna ur temat och paletten ur
// `standardPalette()`; beslutens skäl står i glaze/widgets/color_picker.h.
//
// TVÅ LÄGEN, och skillnaden är bara vad ett VAL betyder:
//   popupen   paletten väljer OCH stänger (dropdownradens recept), pilarna
//             flyttar bara en markör, Enter stänger (`onDone`), och ytan
//             ritas — popupen är ett eget fönster
//   dialogen  (`setDialogMode(true)`) inget stänger förrän OK eller Avbryt:
//             paletten väljer utan att stänga, MARKÖREN ÄR VALET (pilarna
//             väljer direkt), Enter lämnas åt fönstrets standardknapp (ett
//             skrivet hexvärde committas först), och ytan är fönstrets
// ALFAN (`setAllowAlpha(true)`) ger en fjärde reglagerad, A, och
// låter hexfältet ta `#RRGGBBAA`; paletten är opak och BEHÅLLER den aktuella
// alfan vid ett val (paletten är kulören), och markeringen jämför RGB.

namespace glaze {

class Label;
class Slider;

// De små delade räknestegen — färgväljaren, ytan och dialogen läser samma.
namespace colorpanel {

// En kanals tak — `gfx::Color`s BYTE, typens egen gräns och inget valt tal.
inline float kanalTak()
{
    return static_cast<float>(std::numeric_limits<std::uint8_t>::max());
}

// Färgen utan genomskinlighet — den opaka väljarens väg in.
inline gfx::Color opak(gfx::Color c)
{
    c.a = std::numeric_limits<std::uint8_t>::max();
    return c;
}

inline std::string_view trimma(std::string_view s)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string_view::npos)
        return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

} // namespace colorpanel

// PALETTEN — rutorna är DATA och inte widgets (ListBoxens skäl: ETT
// tabbstopp, och fyrtio knappar hade varit fyrtio). Pilarna flyttar en
// MARKÖR, Enter/Space och ett tryck VÄLJER. Ordningen och rutnätets bredd är
// DATANS (`standardPalette()`), så filen har inget eget tal för dem.
class ColorSwatches final : public Widget {
public:
    // Valt — palettens färg, OPAK (ytan lägger på alfan).
    std::function<void(gfx::Color)> onPick;

    // Den färg kontrollen HAR — markeras (RGB, alfan räknas inte), och
    // markören börjar där (−1 när färgen inte finns i paletten: första pilen
    // går då till första rutan).
    void setCurrent(gfx::Color c);
    // MARKÖREN ÄR VALET (dialogen): pilarna väljer direkt, och Enter/Space
    // lämnas vidare.
    void setLive(bool on) { live_ = on; }

    bool focusable() const override { return true; }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;
    void       mouseDown(const Ctx& c, float mx, float my) override;
    void       mouseMove(const Ctx& c, float mx, float my) override;

private:
    static int antal();
    static int kolumner();
    static int rader();
    static int indexOf(gfx::Color c);   // RGB — alfan räknas inte

    // Rutan i widget-lokala LOGISKA px — ritningen och träffen läser samma.
    gfx::Rectf cellRect(const Ctx& c, int i) const;
    int        indexAt(const Ctx& c, float mx, float my) const;
    void       pick(int i);

    gfx::Color current_{};
    int        cursor_ = -1;   // markören (det provisoriska) — −1 = ingen
    int        hover_  = -1;   // rutan under pekaren
    bool       live_   = false;
};

// HEXFÄLTET — en riktig `TextInput` med tre tillägg och inget annat
// (talfältets `SpinBox::Field`-form): bara HEXTECKEN och `#` tas emot (en
// inklistring prövas av tolkningen i stället), Enter COMMITTAR texten, och
// FOKUSFÖRLUST committar den. Ur `focusLost` POSTAS eventen — kroken
// rör bara färgen och fältets egen text, aldrig en användar-handler direkt.
class ColorHexField final : public TextInput {
public:
    // Enter: committa — svaret säger om tangenten är TAGEN (popupen stänger
    // och tar den; dialogen släpper den till standardknappen).
    std::function<bool()> onEnter;
    std::function<void()> onLeave;   // fokusförlust: committa (postat)
    // `AppWindow::commitEdits()` når den skrivna texten genom den här
    // (färgdialogens OK — en knapp som klickas utan att fokus flyttar).
    std::function<void()> onCommitNow;

    void commitEdit() override;

protected:
    bool textInput(const char* utf8) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void focusLost() override;
};

// YTAN — paletten överst, sedan en rad per kanal (etikett · reglage · värdet)
// och hexraden. Barnen är RIKTIGA widgets med satta mått — `layout()` räknar
// ut dem ur stilen och de uppmätta etiketterna innan popupen visas, för ett
// popupfönster kan inte växa efteråt (Wayland). Tab går i adopt-ordning:
// paletten, röd, grön, blå, (alfa,) hexfältet.
class ColorPanel final : public Widget {
public:
    std::function<void(gfx::Color)> onPick;    // paletten valde (med alfan)
    std::function<void(gfx::Color)> onColor;   // reglagens drag — LIVE
    std::function<void()>           onCommit;  // reglaget släppt/stegat — KLAR
    // Hexfältets commit — färgen och om eventen ska POSTAS (fokusförlust,
    // stängning) eller fyras direkt (Enter).
    std::function<void(gfx::Color, bool)> onHex;
    std::function<void()>           onDone;    // Enter i popupen — stäng

    ColorPanel();

    // A-raden och hexfältets åtta siffror (filhuvudet). Före `layout()`.
    void setAllowAlpha(bool on);
    // Dialogens läge (filhuvudet). Före `layout()`.
    void setDialogMode(bool on);

    // `commitEdit` (widget.h) når hit genom ägaren: det SKRIVNA committas nu
    // och synkront, som Enter — men popupen står kvar.
    void commitHex();
    // POPUPEN STÄNGS — fokus går ur hexfältet på det mest bestämda sättet.
    // En skriven text committas (postat: stängningen kan ske mitt i en
    // dispatch), och fältet står med färgens text till nästa öppning.
    void closing();

    // Placerar barnen och svarar med ytans storlek i LOGISKA px. Mäts med
    // ÄGARENS Ctx (fonten, stilen och skalan är app-globala, och popupen har
    // ingen font förrän den visats).
    gfx::Sizef layout(const Ctx& c);

    // ALLT VISAR FÄRGEN — reglagen, värdena, paletten och hexfältet, med
    // PROGRAMMATISKA settrar (inga event, ingen eko-loop). Hexfältet skrivs
    // bara när användaren INTE har skrivit i det (se cpp:n).
    void sync(gfx::Color f);

    void focusPalette();

protected:
    void draw(const Ctx& c) override;
    // Enter som det fokuserade (ett reglage, paletten utan markör) tackade
    // nej till betyder KLAR i popupen — den stängs. I dialogen lämnas den
    // vidare till fönstrets standardknapp.
    bool descendantKeyDown(gfx::Key k, std::uint32_t mods) override;

private:
    gfx::Color frånReglagen() const;
    // HEXFÄLTETS COMMIT — se cpp:n. Svarar om ytan går att röra
    // efteråt: en direkt commit kör ägarens handlers, som får riva den.
    bool hexKlar(bool postad);
    // Antalet reglagerader som syns: R, G, B och — med alfan — A, som är
    // den SISTA raden (`alfaRad`).
    int         kanaler() const;
    std::size_t alfaRad() const { return std::size(reglage_) - 1; }

    ColorSwatches* swatches_ = nullptr;
    Label*         etikett_[4]{};
    Slider*        reglage_[4]{};
    Label*         tal_[4]{};
    Label*         hexEtikett_ = nullptr;
    ColorHexField* hex_        = nullptr;
    gfx::Color     aktuell_{};
    bool           hexRedigeras_ = false;   // skrivet, inte committat
    bool           alfa_         = false;
    bool           dialog_       = false;
};

} // namespace glaze
