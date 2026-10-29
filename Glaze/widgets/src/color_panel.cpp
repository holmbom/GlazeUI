// Färgytan — se color_panel.h för formen och de två lägena, och
// glaze/widgets/color_picker.h för besluten (RGB och inte HSL, valet gäller
// direkt, hexformen). Den bär popupens och DIALOGENS läge samt ALFAN.
// Inga tal och inga färger i filen: måtten är stilens, färgerna temats och
// paletten `standardPalette()`.

#include "color_panel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <type_traits>

#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/slider.h"

#include "prop_parse.h"

namespace glaze {

using colorpanel::kanalTak;
using colorpanel::opak;
using colorpanel::trimma;

// ================================================================ PALETTEN

int ColorSwatches::antal()
{
    return static_cast<int>(standardPalette().colors.size());
}

int ColorSwatches::kolumner()
{
    return std::max(1, standardPalette().columns);
}

int ColorSwatches::rader()
{
    return (antal() + kolumner() - 1) / kolumner();
}

// RGB och inte alla fyra kanalerna: paletten är opak, och en
// halvgenomskinlig röd är fortfarande palettens röda.
int ColorSwatches::indexOf(gfx::Color c)
{
    const auto& f = standardPalette().colors;
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i].r == c.r && f[i].g == c.g && f[i].b == c.b)
            return static_cast<int>(i);
    return -1;
}

void ColorSwatches::setCurrent(gfx::Color c)
{
    current_ = c;
    cursor_  = indexOf(c);
    invalidate();
}

gfx::Rectf ColorSwatches::cellRect(const Ctx& c, int i) const
{
    const auto& m    = *c.style;
    const float steg = m.colorCellSize + m.colorCellGap;
    return gfx::Rectf{static_cast<float>(i % kolumner()) * steg,
                      static_cast<float>(i / kolumner()) * steg,
                      m.colorCellSize, m.colorCellSize};
}

int ColorSwatches::indexAt(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r  = resolve(c);
    const float      lx = Draw::localX(c, r, mx);
    const float      ly = Draw::localY(c, r, my);
    for (int i = 0; i < antal(); ++i)
        if (cellRect(c, i).contains(lx, ly))
            return i;
    return -1;
}

void ColorSwatches::pick(int i)
{
    if (i >= 0 && i < antal() && onPick)
        onPick(standardPalette().colors[std::size_t(i)]);
}

gfx::Sizef ColorSwatches::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner, AUTO är rutnätet — kolumnerna ur
        // datan, rutornas sida och luften ur stilen.
        const auto& m = *c.style;
        gfx::Sizef  s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        const float k = static_cast<float>(kolumner());
        const float r = static_cast<float>(rader());
        if (s.w <= 0.0f)
            s.w = k * m.colorCellSize + (k - 1.0f) * m.colorCellGap;
        if (s.h <= 0.0f)
            s.h = r * m.colorCellSize + (r - 1.0f) * m.colorCellGap;
        return s;
    }
    }
}

void ColorSwatches::draw(const Ctx& c)
{
    const auto& m    = *c.style;
    const auto& col  = c.theme->colors;
    const auto& f    = standardPalette().colors;
    const int   vald = indexOf(current_);
    // Ringarna står i LUFTEN mellan rutorna — halva luften ut från rutans
    // kant, så två grannars ringar aldrig rör varandra.
    const float ut = m.colorCellGap / 2.0f;
    auto ring = [&](int i, const Pen& penna) {
        Draw::frame(c, Draw::inset(cellRect(c, i), -ut),
                    m.colorSwatchRadius + ut, penna);
    };

    switch (c.ui) {
    // ALLA TRE FAMILJERNA RITAR PALETTEN LIKADANT, och det är ett BESLUT
    // (Picture- och Label-prejudikatet): en färgruta är INNEHÅLL, och den
    // ska visa exakt den färg man får — en laminering i Glaze eller en
    // ram i stället för en fyllning i Minimalistic hade gjort rutan till en
    // annan färg än värdet. Konturen är kontrollkanten `buttonBorder`, så
    // en vit ruta syns mot en vit yta; markeringarna är temats roller:
    //   vald     (= kontrollens färg)  ring `text`, fokusringens tjocklek
    //   pekaren                        ring `textDim`, hårlinje
    //   markören (paletten har fokus)  ring `accent`, fokusringens tjocklek
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        for (int i = 0; i < antal(); ++i)
            Draw::rect(c, cellRect(c, i), m.colorSwatchRadius, f[std::size_t(i)],
                       Pen{col.buttonBorder, m.borderWidth});
        if (vald >= 0)
            ring(vald, Pen{col.text, m.focusWidth});
        if (hovered() && hover_ >= 0 && hover_ != vald)
            ring(hover_, Pen{col.textDim, m.borderWidth});
        if (focused() && cursor_ >= 0)
            ring(cursor_, Pen{col.accent, m.focusWidth});
        break;
    }
}

bool ColorSwatches::keyDown(gfx::Key k, std::uint32_t)
{
    const int n   = antal();
    const int kol = kolumner();
    // Första pilen utan markör går till första rutan; sedan flyttar pilarna
    // i rutnätet och stannar i kanten (upp/ner byter aldrig kolumn). I
    // DIALOGEN är markören valet (`live_`), så en flytt väljer också.
    auto flytta = [&](int ny) {
        ny = std::clamp(ny, 0, n - 1);
        if (ny != cursor_) {
            cursor_ = ny;
            invalidate();
            if (live_)
                pick(cursor_);
        }
        return true;
    };
    if (cursor_ < 0 && (k == gfx::Key::Left || k == gfx::Key::Right ||
                        k == gfx::Key::Up || k == gfx::Key::Down))
        return flytta(0);
    switch (k) {
    case gfx::Key::Left:
        return flytta(cursor_ - 1);
    case gfx::Key::Right:
        return flytta(cursor_ + 1);
    case gfx::Key::Up:
        return flytta(cursor_ - kol >= 0 ? cursor_ - kol : cursor_);
    case gfx::Key::Down:
        return flytta(cursor_ + kol < n ? cursor_ + kol : cursor_);
    case gfx::Key::Home:
        return flytta(0);
    case gfx::Key::End:
        return flytta(n - 1);
    case gfx::Key::Return:
    case gfx::Key::Space:
        // Utan markör finns inget att välja — Enter går då vidare till ytan
        // (`descendantKeyDown`), som stänger. I dialogen är markören redan
        // vald, och Enter går till fönstrets standardknapp.
        if (cursor_ < 0 || live_)
            return false;
        pick(cursor_);
        return true;
    default:
        return false;
    }
}

void ColorSwatches::mouseDown(const Ctx& c, float mx, float my)
{
    const int i = indexAt(c, mx, my);
    if (i < 0)
        return;
    cursor_ = i;
    invalidate();
    pick(i);
}

void ColorSwatches::mouseMove(const Ctx& c, float mx, float my)
{
    const int i = indexAt(c, mx, my);
    if (i != hover_) {   // hård regel 1: bara vid ändring
        hover_ = i;
        invalidate();
    }
}

// ================================================================ HEXFÄLTET

bool ColorHexField::textInput(const char* utf8)
{
    std::string ok;
    for (const char* p = utf8; *p; ++p) {
        const char ch = *p;
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F') || ch == '#')
            ok.push_back(ch);
    }
    if (ok.empty())
        return true;   // sväljs — inget annat hör hemma i en hexfärg
    return TextInput::textInput(ok.c_str());
}

bool ColorHexField::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (k == gfx::Key::Return)
        return onEnter && onEnter();   // svaret: TAGEN eller vidare
    return TextInput::keyDown(k, mods);
}

void ColorHexField::focusLost()
{
    TextInput::focusLost();
    if (onLeave)
        onLeave();
}

void ColorHexField::commitEdit()
{
    TextInput::commitEdit();
    if (onCommitNow)
        onCommitNow();
}

// ==================================================================== YTAN

ColorPanel::ColorPanel()
{
    swatches_         = add<ColorSwatches>();
    swatches_->name   = "palette";
    swatches_->onPick = [this](gfx::Color f) {
        // Paletten är KULÖREN: den aktuella alfan står kvar.
        if (alfa_)
            f.a = aktuell_.a;
        if (onPick)
            onPick(f);
    };

    const std::uint32_t id[]   = {str::ColorRed, str::ColorGreen,
                                  str::ColorBlue, str::ColorAlpha};
    const char*         namn[] = {"red", "green", "blue", "alpha"};
    // `extent_v` och inte `std::size(reglage_)`: en medlem nämnd i ett
    // konstant uttryck använder `this`, och GCC 14 (Debian) vägrar det.
    constexpr std::size_t kKanaler = std::extent_v<decltype(reglage_)>;
    static_assert(std::size(id) == kKanaler && std::size(namn) == kKanaler);
    for (std::size_t i = 0; i < std::size(reglage_); ++i) {
        etikett_[i] = add<Label>(tr(id[i]));
        reglage_[i] = add<Slider>();
        reglage_[i]->setMin(0.0f);
        reglage_[i]->setMax(kanalTak());
        reglage_[i]->name     = namn[i];
        reglage_[i]->onChange = [this] {
            if (onColor)
                onColor(frånReglagen());
        };
        // Reglagets egen commit (släppet, ett steg) är färgens.
        reglage_[i]->onCommit = [this] {
            if (onCommit)
                onCommit();
        };
        tal_[i]       = add<Label>();
        tal_[i]->name = std::string(namn[i]) + "Value";   // lang_gate:rad namn
        tal_[i]->setAlign(Align::Right);
    }
    // A-raden syns bara med alfan (`setAllowAlpha`).
    etikett_[alfaRad()]->hide();
    reglage_[alfaRad()]->hide();
    tal_[alfaRad()]->hide();

    hexEtikett_    = add<Label>(tr(str::ColorHex));
    hex_           = add<ColorHexField>();
    hex_->name     = "hex";
    // Skrivandet prövas INTE — det märker bara att texten nu är
    // användarens, så att `sync` lämnar den i fred tills den committats.
    hex_->onChange = [this] { hexRedigeras_ = true; };
    hex_->onEnter  = [this] {
        if (!hexKlar(false))
            return true;    // ytan revs — rör ingenting mer
        if (dialog_)
            return false;   // standardknappen tar Enter, med färgen satt
        if (onDone)
            onDone();
        return true;
    };
    hex_->onLeave     = [this] { hexKlar(true); };
    hex_->onCommitNow = [this] { hexKlar(false); };
}

int ColorPanel::kanaler() const
{
    // Alla rader med alfan, annars alla utom den sista (A).
    return static_cast<int>(alfa_ ? std::size(reglage_) : alfaRad());
}

void ColorPanel::setAllowAlpha(bool on)
{
    if (alfa_ == on)
        return;
    alfa_ = on;
    for (Widget* w : {static_cast<Widget*>(etikett_[alfaRad()]),
                      static_cast<Widget*>(reglage_[alfaRad()]),
                      static_cast<Widget*>(tal_[alfaRad()])})
        on ? w->show() : w->hide();
    hexRedigeras_ = false;   // hexformen byts — fältet skrivs om av `sync`
    sync(aktuell_);
}

void ColorPanel::setDialogMode(bool on)
{
    dialog_ = on;
    swatches_->setLive(on);
    invalidate();
}

void ColorPanel::commitHex()
{
    hexKlar(false);
}

void ColorPanel::closing()
{
    hexKlar(true);
}

void ColorPanel::focusPalette()
{
    swatches_->focus();
}

gfx::Sizef ColorPanel::layout(const Ctx& c)
{
    // Måtten från FÖRRA öppningen nollas först: en satt axel vinner i
    // mätningen (axelregeln), så utan det hade en popup som öppnats i
    // Slate mätts med Slates palett och reglage även efter ett byte till
    // Minimalistic.
    for (Widget* barn : children())
        barn->width = barn->height = 0.0f;

    const auto&      m   = *c.style;
    const float      pad = m.colorPopupPad;
    const float      gap = m.colorPopupGap;
    const gfx::Sizef pal = measure(*swatches_, c);

    // Etikettkolumnen är den BREDASTE etiketten på det aktiva språket
    // ("Punainen" på finska), värdekolumnen kanalens största tal.
    float etikettW = Draw::measure(c, hexEtikett_->text()).w;
    for (int i = 0; i < kanaler(); ++i)
        etikettW = std::max(etikettW, Draw::measure(c, etikett_[i]->text()).w);
    const float talW = Draw::measure(
                           c, std::to_string(static_cast<int>(kanalTak())))
                           .w;
    const float radH  = std::max(measure(*reglage_[0], c).h,
                                 Draw::lineHeight(c));
    const float fältH = measure(*hex_, c).h;
    // Hellre bredare än paletten än ett reglage under sin minsta längd.
    const float inre =
        std::max(pal.w, etikettW + gap + m.colorSliderMinW + gap + talW);

    auto ställ = [](Widget* w, float vänster, float topp, float bredd,
                    float höjd) {
        w->x      = vänster;
        w->y      = topp;
        w->width  = bredd;
        w->height = höjd;
    };
    ställ(swatches_, pad + (inre - pal.w) / 2.0f, pad, pal.w, pal.h);
    float radY = pad + pal.h + gap;   // inte `y` — det är widgetens fält
    for (int i = 0; i < kanaler(); ++i) {
        ställ(etikett_[i], pad, radY, etikettW, radH);
        ställ(reglage_[i], pad + etikettW + gap, radY,
              inre - etikettW - 2.0f * gap - talW, radH);
        ställ(tal_[i], pad + inre - talW, radY, talW, radH);
        radY += radH + gap;
    }
    ställ(hexEtikett_, pad, radY, etikettW, fältH);
    ställ(hex_, pad + etikettW + gap, radY, inre - etikettW - gap, fältH);
    return gfx::Sizef{inre + 2.0f * pad, radY + fältH + pad};
}

// Hexfältet skrivs bara när användaren INTE har skrivit i det: medan man
// skriver är texten användarens (talfältets regel), och commiten skriver om
// den. Frågan är "har användaren skrivit", inte "har fältet fokus" — fokus i
// popupen står kvar mellan öppningarna, och ett fält som bara HADE fokus ska
// ändå visa färgen när popupen öppnas igen.
void ColorPanel::sync(gfx::Color f)
{
    aktuell_               = f;
    const std::uint8_t k[] = {f.r, f.g, f.b, f.a};
    for (std::size_t i = 0; i < std::size(reglage_); ++i) {
        reglage_[i]->setValue(static_cast<float>(k[i]));
        tal_[i]->setText(std::to_string(static_cast<int>(k[i])));
    }
    swatches_->setCurrent(f);
    if (!hexRedigeras_)
        hex_->setText(propparse::hexColorText(f, alfa_));
}

void ColorPanel::draw(const Ctx& c)
{
    // I DIALOGEN är ytan fönstrets (filhuvudet) — ytan ritar ingenting.
    if (dialog_)
        return;
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    // YTAN är dropdownlistans och tooltipens: en yta man arbetar i,
    // fyrkantig (popupfönstret är OPAKT — ett rundat hörn hade visat
    // fönsterbakgrunden i hörnen).
    switch (c.ui) {
    // GLAZE — fältets recept: `panel` med den BESTÄMDA konturen.
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel,
                   Pen{col.buttonBorder, m.borderWidth});
        break;
    // MINIMALISTIC — popupen ÄR ett eget fönster (ListBoxens
    // `ownsSurface`): ytan fylls, kanten är formen.
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.panel,
                   Pen{col.buttonBorder, m.borderWidth});
        break;
    // SLATE — en YTA: `panel` och hårlinjen `border`.
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel, Pen{col.border, m.borderWidth});
        break;
    }
}

bool ColorPanel::descendantKeyDown(gfx::Key k, std::uint32_t)
{
    if (k != gfx::Key::Return || dialog_)
        return false;
    if (onDone)
        onDone();
    return true;
}

gfx::Color ColorPanel::frånReglagen() const
{
    gfx::Color    f   = aktuell_;
    std::uint8_t* k[] = {&f.r, &f.g, &f.b, &f.a};
    for (int i = 0; i < kanaler(); ++i)
        *k[i] = static_cast<std::uint8_t>(std::lround(
            std::clamp(reglage_[i]->value(), 0.0f, kanalTak())));
    return f;
}

// HEXFÄLTETS COMMIT — Enter (`postad` falsk), fokusförlusten och
// stängningen (sanna). Bara en text användaren SKRIVIT prövas: en tolkbar
// blir färgen (klar — `onChange` och `onCommit` hos ägaren), det som inte
// tolkas (halvskrivet, för många siffror) lämnar färgen orörd. Med alfan tas
// åtta siffror, och sex är opakt (CSS:s regel). Texten skrivs ALLTID om till
// färgen som gäller, så en halvskriven försvinner och en giltig får sin
// kanoniska form.
bool ColorPanel::hexKlar(bool postad)
{
    gfx::Color f{};
    const bool skrivet = hexRedigeras_;
    hexRedigeras_      = false;
    if (skrivet && propparse::parseHexColor(trimma(hex_->text()), f, alfa_)) {
        aktuell_ = alfa_ ? f : opak(f);   // också när popupen redan stängts
        if (onHex) {
            const HandlerGuard vakt(*this);
            onHex(aktuell_, postad);
            if (!vakt.ok())
                return false;
        }
    }
    hex_->setText(propparse::hexColorText(aktuell_, alfa_));
    return true;
}

} // namespace glaze
