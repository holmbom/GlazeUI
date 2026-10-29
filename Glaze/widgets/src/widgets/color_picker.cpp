// ColorPicker — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och den
// gör det med FÅ RADER: fältet (dropdownens recept per familj), färgrutan och
// pilen. Popupen bor HÄR också, som `Dropdown::ListWindow`; dess INNEHÅLL —
// paletten, reglagen, hexfältet och ytan som bär dem — är den delade
// färgytan i den interna `../color_panel.h` (färgdialogen
// visar samma yta). Måtten kommer ur stilen (`color*`, fältets och
// dropdownpilens), färgerna ur temat och paletten ur `standardPalette()` —
// inga tal och inga färger i den här filen. Beslutens skäl står i
// color_picker.h.

#include "glaze/widgets/color_picker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <string>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../color_panel.h"
#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("colorpicker", ColorPicker)

using colorpanel::opak;
using colorpanel::trimma;

// ================================================================ POPUPEN
//
// Dropdownlistans fönsterform — `Chrome::None` + popup-attributet, storleken
// satt av ägaren före varje visning (en popup kan inte växa efteråt) — som
// TAR TANGENTBORDET (`setPopupTakesKeys`): ägarfönstret skickar
// tangenterna och tecknen hit medan popupen är öppen, och Escape som ingen
// tog stänger den. AppWindow-OBJEKTET återanvänds mellan öppningar,
// OS-fönstret återskapas per visning.
class ColorPicker::Popup final : public AppWindow {
public:
    explicit Popup(ColorPicker& p)
        : AppWindow("", 100, 40, gfx::Chrome::None), picker_(p)
    {
        setPopup(true);
        setPopupTakesKeys(true);
        panel_       = add<ColorPanel>();
        panel_->dock = Dock::Fill;   // popupen ÄR ytan
        // PALETTEN VÄLJER OCH STÄNGER — stängningen köas FÖRE händelsen
        // (menyns aktiveringsordning): appens `onChange` får öppna en ny
        // popup, och då ska det vara VÅR stängning som redan ligger i kön,
        // inte en som kommer efter den nya.
        panel_->onPick = [this](gfx::Color f) {
            close();
            picker_.colorFromUser(f);
        };
        // Reglagen: LIVE under draget, KLAR vid släppet och varje steg — då
        // med färgen som redan gäller. Hexfältet: klar, postat eller inte.
        panel_->onColor  = [this](gfx::Color f) {
            picker_.userColor(f, false, false);
        };
        panel_->onCommit = [this] {
            picker_.userColor(picker_.color(), true, false);
        };
        panel_->onHex = [this](gfx::Color f, bool postad) {
            picker_.userColor(f, true, postad);
        };
        panel_->onDone = [this] { close(); };
        onClose        = [this] { picker_.popupClosed(); };
    }

    ColorPanel& panel() { return *panel_; }

private:
    ColorPicker& picker_;
    ColorPanel*  panel_ = nullptr;
};

// ============================================================ FÄRGVÄLJAREN

ColorPicker::ColorPicker()  = default;   // Popup komplett här — se headern
ColorPicker::~ColorPicker() = default;

// Färgen LAGRAS med sin alfa och LÄSES opak när alfan inte tillåts
// (läsningens regel, så `setColor` och `setAllowAlpha` kan stå i vilken
// ordning som helst).
gfx::Color ColorPicker::color() const
{
    return allowAlpha_ ? color_ : opak(color_);
}

void ColorPicker::setColor(gfx::Color c)
{
    if (c == color_)
        return;
    color_      = c;
    commitBase_ = color();   // appens färg är inte användarens ändring
    if (open_ && popup_)
        popup_->panel().sync(color());   // en öppen popup följer med
    invalidate();
}

// `#RRGGBB` eller `#RRGGBBAA` — åtta siffror tas ALLTID (alfan lagras; den
// läses först när alfan tillåts, se `color()`).
void ColorPicker::setColor(std::string_view text)
{
    gfx::Color c{};
    if (!propparse::parseHexColor(trimma(text), c, true)) {
        gfx::log("ColorPicker(%s): '%.*s' är ingen färg #RRGGBB eller "
                 "#RRGGBBAA — ignoreras",
                 name.c_str(), static_cast<int>(text.size()), text.data());
        return;
    }
    setColor(c);
}

std::string ColorPicker::colorText() const
{
    return propparse::hexColorText(color(), allowAlpha_);
}

// ALFAN. Bytet ändrar vad `color()` svarar — appens ändring, så
// baslinjen följer med — och popupens rader, så en öppen popup stängs (den
// kan inte växa efter att den visats).
void ColorPicker::setAllowAlpha(bool on)
{
    if (allowAlpha_ == on)
        return;
    const gfx::Color förut = color();
    allowAlpha_            = on;
    if (color() != förut)
        commitBase_ = color();
    close();
    invalidate();
}

// ETT AVSLUTAT VAL — paletten och hexfältets commit (headern).
void ColorPicker::colorFromUser(gfx::Color c)
{
    userColor(c, true, false);
}

// ANVÄNDARVÄGARNA — tillståndet först, därefter popupen och
// omritningen, och händelserna SIST (en handler får göra vad som helst, även
// stänga popupen). `onChange` när färgen ändrades; `onCommit` när valet är
// KLART och färgen skiljer sig från förra commiten — ett reglage som dragits
// bort och tillbaka committar alltså ingenting. Ur ett fokusbyte eller en
// stängning POSTAS de två (`postEvent`).
void ColorPicker::userColor(gfx::Color c, bool klar, bool postad)
{
    if (!allowAlpha_)
        c = opak(c);   // den opaka väljarens väg in
    const bool ändrad = c != color();
    if (ändrad) {
        color_ = c;
        if (open_ && popup_)
            popup_->panel().sync(color());
        invalidate();
    }
    const bool commit = klar && color() != commitBase_;
    if (commit)
        commitBase_ = color();
    if (!ändrad && !commit)
        return;
    if (postad)
        postEvent([this, ändrad, commit] { fire(ändrad, commit); });
    else
        fire(ändrad, commit);
}

void ColorPicker::commitEdit()
{
    if (open_ && popup_)
        popup_->panel().commitHex();
}

// Vakten emellan (`Widget::fireEvent`): en onChange-handler får riva eller
// flytta färgväljaren, och då fyras inte onCommit.
void ColorPicker::fire(bool change, bool commit)
{
    if (change && !fireEvent(onChange))   // färgen läses ur sender (color())
        return;
    if (commit)
        fireEvent(onCommit);
}

bool ColorPicker::setProp(const std::string& key, const std::string& value)
{
    if (key == "color") {
        setColor(std::string_view(value));   // ogiltig: loggad, orörd
        return true;
    }
    if (key == "allowAlpha") {
        setAllowAlpha(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------- popupen

void ColorPicker::open()
{
    if (open_ || !isEnabled() || !appWindow() || !appWindow()->isOpen())
        return;
    openAt(behaviorCtx());
}

void ColorPicker::openAt(const Ctx& c)
{
    if (!popup_)
        popup_ = std::make_unique<Popup>(*this);
    ColorPanel& panel = popup_->panel();
    panel.setAllowAlpha(allowAlpha_);   // A-raden FÖRE måtten
    panel.sync(color());
    // STORLEKEN FÖRE VISNINGEN: en popup får sitt mått när den mappas och
    // kan inte växa efteråt. Logiska px, uppåt till hela — en avhuggen
    // bråkdel hade klippt marginalen.
    const gfx::Sizef s = panel.layout(c);
    popup_->setPopupAnchor(resolve(c));
    popup_->setClientSize(static_cast<int>(std::ceil(s.w)),
                          static_cast<int>(std::ceil(s.h)));
    open_ = true;
    popup_->show(*appWindow());
    // Fail-safe (dropdownens): kom fönstret aldrig upp ska kontrollen inte
    // fastna i öppet läge.
    if (!popup_->isOpen()) {
        gfx::log("ColorPicker(%s): popup-fönstret kom aldrig upp — stänger av",
                 name.c_str());
        popupClosed();
        return;
    }
    // Tangenterna börjar i paletten — pilarna arbetar direkt, och Tab tar
    // sig vidare till reglagen och hexfältet.
    panel.focusPalette();
    // Öppnades vi mitt i ett tryck håller backenden mus-capture på
    // ägarfönstret, och släppet når aldrig hit om det sker över popupen —
    // dropdownens rader, av samma skäl.
    appWindow()->window().releaseMouseCapture();
    pressed_ = false;
    invalidate();
}

void ColorPicker::close()
{
    if (open_ && popup_)
        popup_->close();   // uppskjuten → onClose → popupClosed
}

// Stängningen — vilket sätt det än var (valet, Enter, Escape, ett tryck
// utanför, fokusförlusten): en hexfärg som skrivits men inte committats
// committas, postat, eftersom `onClose` kan köras mitt i en
// dispatch (`App::closePopupsNow`). `open_` är redan släckt, så popupen
// synkas inte — den visas ju inte.
void ColorPicker::popupClosed()
{
    open_ = false;
    if (popup_)
        popup_->panel().closing();
    invalidate();   // pilen och kanten ritas om i ägarfönstret
}

// ------------------------------------------------------------- beteendet

// MED ÖPPEN POPUP når tangenterna aldrig hit — popupen tar tangentbordet
// (`setPopupTakesKeys`): ägarfönstret skickar dem dit först, också Tab, som
// vandrar i popupen. STÄNGD öppnar Space, Ned och F4 (kombinationsrutans
// tangent). Enter öppnar inte — den bubblar
// till fönstrets standardknapp, som dropdownens.
bool ColorPicker::keyDown(gfx::Key k, std::uint32_t)
{
    if (k == gfx::Key::Space || k == gfx::Key::Down || k == gfx::Key::F4) {
        open();
        return true;
    }
    return false;
}

// TOGGLE — dropdownens ordning: ett tryck medan popupen är öppen har redan
// köat stängningen (App::closePopups), `open_` är fortfarande sann, och
// vägen blir `close()` (idempotent) i stället för en återöppning.
void ColorPicker::mouseDown(const Ctx&, float, float)
{
    if (open_)
        close();
    else
        open();
    invalidate();
}

// En inaktiverad färgväljare har ingen öppen popup.
void ColorPicker::enabledChanged()
{
    if (!isEnabled())
        close();
}

// ------------------------------------------------------------- geometrin

// Pilen står där dropdownens står — högerkanten in en fältluft, v-centrerad.
gfx::Rectf ColorPicker::chevronRect(const Ctx& c, const gfx::Rectf& b) const
{
    const auto& m = *c.style;
    return gfx::Rectf{b.x + b.w - m.fieldPadX - 2.0f * m.chevronW,
                      b.y + b.h / 2.0f - m.chevronH, 2.0f * m.chevronW,
                      2.0f * m.chevronH};
}

// Färgrutan fyller fältet innanför luften, fram till luften före pilen.
gfx::Rectf ColorPicker::swatchRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float      i  = c.style->colorSwatchInset;
    const gfx::Rectf ch = chevronRect(c, b);
    return gfx::Rectf{b.x + i, b.y + i, std::max(0.0f, ch.x - i - (b.x + i)),
                      std::max(0.0f, b.h - 2.0f * i)};
}

gfx::Sizef ColorPicker::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner. AUTO-bredden är stilens, AUTO-höjden
        // FÄLTETS recept (dropdownens), så en färgväljare står i rad med ett
        // textfält och en vallista i varje stil. `Draw::lineHeight` och inte
        // fonten direkt — mätvägen talar LOGISKA px.
        const auto& m = *c.style;
        gfx::Sizef  s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        if (s.w <= 0.0f) s.w = m.colorPickerDefaultW;
        if (s.h <= 0.0f) s.h = Draw::lineHeight(c) + 2.0f * m.fieldPadY;
        return s;
    }
    }
}

// ------------------------------------------------------------- ritningen

void ColorPicker::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf ch  = chevronRect(c, b);
    const gfx::Rectf sw  = swatchRect(c, b);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const bool       på  = isEnabled();

    // FÄRGRUTAN är INNEHÅLL — samma i alla tre familjerna (paletten i
    // ../color_panel.cpp bär skälet). Inaktiv TONAS den med ikonernas mått
    // (`iconFade`, som `Content::imgOpacity`): färgen finns kvar men läser som
    // "inte nu", mot vilken fältyta som helst. Konturen är kontrollkanten, så
    // en vit ruta syns i ett vitt fält. Med ALFAN ritas färgen över
    // ett RUTMÖNSTER i temats två ytor, så genomskinligheten syns.
    gfx::Color fyll = color();
    if (!på)
        fyll.a = static_cast<std::uint8_t>(
            std::lround(float(fyll.a) * (1.0f - m.iconFade)));
    auto ruta = [&] {
        if (allowAlpha_)
            Draw::checker(c, sw, m.colorSwatchRadius, m.colorCheckerCell,
                          col.panel, col.appWorkspace);
        Draw::rect(c, sw, m.colorSwatchRadius, fyll,
                   Pen{på ? col.buttonBorder : col.border, m.borderWidth});
    };
    auto pil = [&] {
        Draw::chevron(c, ch, isOpen() ? Dir::Up : Dir::Down,
                      Pen{på ? col.textDim : col.border, 0.0f});
    };
    auto ring = [&] {
        if (focused() && på)
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        m.cornerRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
    };

    switch (c.ui) {
    // GLAZE — ett FÄLT, och fält LAMINERAS INTE:
    // dropdownens recept, med färgrutan där dropdownen har sin text.
    //
    // De fyra tillstånden (princip 7):
    //   vila       yta `panel`, kontur `buttonBorder`, pil `textDim`
    //   hover      konturen går till `accent`
    //   tryck      konturen står kvar i `accent` så länge popupen är ÖPPEN
    //   fokus      fristående ring utanför kanten
    //   inaktiv    kontur `border`, rutan tonad, pilen `border`
    case UI::Glaze:
        Draw::rect(c, b, m.cornerRadius, col.panel,
                   Pen{!på                    ? col.border
                       : isOpen() || hovered() ? col.accent
                                               : col.buttonBorder,
                       m.borderWidth});
        ruta();
        pil();
        ring();
        break;

    // MINIMALISTIC — "Outline": dropdownens recept — fältytan fylls i vila
    // kanten bär tillståndet, inaktiv yta `buttonDisabled`.
    //
    //   vila       `panel` + kant `buttonBorder`
    //   hover      kanten `accent`
    //   tryck      kanten `accent` medan popupen är ÖPPEN
    //   fokus      fristående ring (halv tjocklek)
    //   inaktiv    `buttonDisabled` + kant `border`, rutan tonad
    case UI::Minimalistic:
        Draw::rect(c, b, m.cornerRadius, på ? col.panel : col.buttonDisabled);
        Draw::frame(c, b, m.cornerRadius,
                    Pen{!på                    ? col.border
                        : isOpen() || hovered() ? col.accent
                                                : col.buttonBorder,
                        m.borderWidth});
        ruta();
        pil();
        ring();
        break;

    // SLATE — ett FÄLT: `panel`-yta, KANTEN bär
    // tillståndet — dropdownens recept rakt av.
    //
    //   vila       yta `panel`, hårlinje `border`
    //   hover      kanten `accent` (hotBorder)
    //   tryck      kanten `accent` medan popupen är ÖPPEN
    //   fokus      fristående ring utanför kanten
    //   inaktiv    rutan tonad, pilen `border`
    case UI::Slate:
    default:
        Draw::rect(c, b, m.cornerRadius, col.panel,
                   Pen{isOpen() && på ? col.accent : hotBorder(c),
                       m.borderWidth});
        ruta();
        pil();
        ring();
        break;
    }
}

} // namespace glaze
