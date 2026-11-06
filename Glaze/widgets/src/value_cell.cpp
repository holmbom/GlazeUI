// DE TYPADE CELLERNA — se value_cell.h.

#include "value_cell.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "glaze/language.h"
#include "glaze/strings.h"

#include "color_panel.h"
#include "number_text.h"
#include "prop_parse.h"

namespace glaze::valuecell {

namespace {

// Ett blocks vänsterkant i `content` när blocket är `w` brett och ställs vid
// den kant `align` säger.
float alignedX(const gfx::Rectf& content, float w, Align align)
{
    switch (align) {
    case Align::Center:
        return content.x + (content.w - w) / 2.0f;
    case Align::Right:
        return content.x + content.w - w;
    case Align::Left:
    default:
        return content.x;
    }
}

// Textens bredd — 0 för en tom text, så att ett utelämnat prefix inte tar plats.
float textW(const Ctx& c, const std::string& s)
{
    return s.empty() ? 0.0f : Draw::measure(c, s).w;
}

// Bildens sida: assetens storleksklass, annars stilens reservsida — 0 utan bild.
float imageSide(const Ctx& c, const ImageSource& img)
{
    if (!img.valid())
        return 0.0f;
    const float k = img.sizeClass();
    return k > 0.0f ? k : c.style->iconFallback;
}

// Knappcellens innehåll: bilden före texten, i knappens text- och bildläge.
Content buttonContent(const Ctx& c, const std::string& text,
                      const ImageSource& img, bool enabled)
{
    Content ct{img, text,
               enabled ? c.theme->colors.text : c.theme->colors.textDim,
               ImagePos::Left, c.style->iconGap, imageSide(c, img)};
    ct.imgEnabled = enabled;
    return ct;
}

} // namespace

// ---------------------------------------------------------------- KRYSSET

bool parseCheck(const std::string& value)
{
    return propparse::parseBool(value);
}

std::string checkText(bool on)
{
    return on ? "true" : "false";
}

// ---------------------------------------------------------------- FÄRGEN

std::optional<gfx::Color> parseColor(const std::string& value, bool allowAlpha)
{
    gfx::Color c{};
    if (!propparse::parseHexColor(numbertext::trim(value), c, true))
        return std::nullopt;
    if (!allowAlpha)
        c = colorpanel::opak(c);   // färgväljarens läsregel
    return c;
}

std::string colorText(gfx::Color c, bool allowAlpha)
{
    return propparse::hexColorText(c, allowAlpha);
}

// ------------------------------------------------------------------ TALET

bool isEmpty(const std::string& value, const NumberSpec& spec)
{
    return spec.allowEmpty && numbertext::trim(value).empty();
}

std::optional<double> numberValue(const std::string& value, const NumberSpec& spec)
{
    double v = 0.0;
    if (!numbertext::parseUser(value, v))
        return std::nullopt;
    return numbertext::effective(v, spec.min, spec.max, spec.decimals);
}

bool numberError(const std::string& value, const NumberSpec& spec)
{
    return !isEmpty(value, spec) && !numberValue(value, spec);
}

NumberCommit commitNumber(const std::string& userText, const NumberSpec& spec)
{
    if (isEmpty(userText, spec))
        return NumberCommit{Commit::Empty, std::string()};
    const std::optional<double> v = numberValue(userText, spec);
    if (!v)
        return NumberCommit{Commit::Invalid, std::string()};
    return NumberCommit{Commit::Ok, numbertext::format(*v, spec.decimals, ".")};
}

// Två tomma värden är samma; ett tomt och ett tal är det inte. Två tal jämförs
// efter klampningen och avrundningen — formatet är idempotent genom den.
bool sameNumber(const std::string& oldValue, const std::string& canonical,
                const NumberSpec& spec)
{
    const bool förutTom = isEmpty(oldValue, spec);
    const bool nuTom    = isEmpty(canonical, spec);
    if (förutTom || nuTom)
        return förutTom && nuTom;
    const std::optional<double> förut = numberValue(oldValue, spec);
    const std::optional<double> nu    = numberValue(canonical, spec);
    return förut && nu && *förut == *nu;
}

std::string displayNumber(const std::string& value, const std::string* pending,
                          const NumberSpec& spec, const std::string& sep)
{
    if (pending)
        return *pending;
    if (const std::optional<double> v = numberValue(value, spec))
        return numbertext::format(*v, spec.decimals, sep);
    if (isEmpty(value, spec))
        return spec.emptyText;
    return value;
}

std::string editNumberText(const std::string& value, const std::string* pending,
                           const NumberSpec& spec, const std::string& sep)
{
    if (!pending && isEmpty(value, spec))
        return std::string();
    return displayNumber(value, pending, spec, sep);
}

// ------------------------------------------------------------- GEOMETRIN

gfx::Rectf boxRect(const Ctx& c, const gfx::Rectf& content, Align align)
{
    const float s = c.style->checkSize;
    return gfx::Rectf{alignedX(content, s, align),
                      content.y + (content.h - s) / 2.0f, s, s};
}

// FÄLTET är redigerarens yta: talets text står `fieldPadX` innanför den, precis
// som i ett textfält, så att den ritade texten och den redigerade står på samma
// plats. Fältet börjar där prefixet slutar och slutar där suffixet börjar; utan
// suffix når det cellens kant (fältets luft ersätter cellens).
gfx::Rectf numberFieldRect(const Ctx& c, const gfx::Rectf& content,
                           const NumberSpec& spec)
{
    const float pad   = c.style->fieldPadX;
    const float left  = content.x + textW(c, spec.prefix) - pad;
    const float right = spec.suffix.empty()
                            ? content.x + content.w + pad
                            : content.x + content.w - textW(c, spec.suffix);
    return gfx::Rectf{left, content.y, std::max(0.0f, right - left), content.h};
}

// ---------------------------------------------------------------- MÅTTEN

float checkW(const Ctx& c)
{
    return c.style->checkSize;
}

float colorW(const Ctx& c, const std::string& value, bool allowAlpha,
             ColorDisplay display)
{
    const std::optional<gfx::Color> färg = parseColor(value, allowAlpha);
    if (!färg)
        return textW(c, value);
    switch (display) {
    case ColorDisplay::Hex:
        return textW(c, colorText(*färg, allowAlpha));
    case ColorDisplay::Both:
        return c.style->checkSize + c.style->checkGap +
               textW(c, colorText(*färg, allowAlpha));
    case ColorDisplay::Swatch:
    default:
        return c.style->colorCellSwatchW;
    }
}

// Exakt det `drawNumber` behöver: prefixet, talet och — med suffix — fältets
// luft före suffixet och suffixet (`numberFieldRect`). Utan suffix når fältet
// cellens kant, och talet slutar där innehållet slutar.
float numberW(const Ctx& c, const std::string& display, const NumberSpec& spec)
{
    const float suffix =
        spec.suffix.empty() ? 0.0f
                            : c.style->fieldPadX + textW(c, spec.suffix);
    return textW(c, spec.prefix) + textW(c, display) + suffix;
}

float buttonW(const Ctx& c, const std::string& text, const ImageSource& img)
{
    return Draw::measure(c, buttonContent(c, text, img, true)).w +
           2.0f * (c.style->buttonPadX + c.style->cellButtonInset);
}

// ------------------------------------------------------------- RITNINGEN

void drawCheckBox(const Ctx& c, const gfx::Rectf& box, CheckState state,
                  gfx::Color kant, gfx::Color bock)
{
    const auto& m = *c.style;
    Draw::frame(c, box, m.checkRadius, Pen{kant, m.borderWidth});
    switch (state) {
    case CheckState::Checked:
        Draw::check(c, box, Pen{bock, m.checkStroke});
        break;
    case CheckState::Indeterminate:
        Draw::checkDash(c, box, Pen{bock, m.checkStroke});
        break;
    case CheckState::Unchecked:
    default:
        break;
    }
}

void drawCheck(const Ctx& c, const gfx::Rectf& content, Align align, bool on,
               bool enabled, gfx::Color fg)
{
    const auto& col = c.theme->colors;
    drawCheckBox(c, boxRect(c, content, align),
                 on ? CheckState::Checked : CheckState::Unchecked,
                 enabled ? col.buttonBorder : col.border, fg);
}

// Läget avgör blocket, som ställs vid `align`: rutan ensam (`colorCellSwatchW`
// bred, `checkSize` hög), hextexten ensam, eller rutan i `checkSize` och
// hextexten efter den.
void drawColor(const Ctx& c, const gfx::Rectf& content, Align align,
               const std::string& value, bool allowAlpha, ColorDisplay display,
               bool enabled, gfx::Color fg)
{
    const auto&                     m    = *c.style;
    const auto&                     col  = c.theme->colors;
    const std::optional<gfx::Color> färg = parseColor(value, allowAlpha);
    if (!färg) {
        Draw::text(c, content, value, fg, align);
        return;
    }
    if (display == ColorDisplay::Hex) {
        Draw::text(c, content, colorText(*färg, allowAlpha), fg, align);
        return;
    }
    const float      w    = colorW(c, value, allowAlpha, display);
    const gfx::Rectf ruta = boxRect(
        c, {alignedX(content, w, align), content.y, w, content.h}, Align::Left);
    const bool       medText = display == ColorDisplay::Both;
    const gfx::Rectf box =
        medText ? ruta : gfx::Rectf{ruta.x, ruta.y, m.colorCellSwatchW, ruta.h};
    // Inaktiv tonas rutan med ikonernas mått, och med alfa ritas den över ett
    // rutmönster så att genomskinligheten syns.
    gfx::Color fyll = *färg;
    if (!enabled)
        fyll.a = static_cast<std::uint8_t>(
            std::lround(float(fyll.a) * (1.0f - m.iconFade)));
    if (allowAlpha)
        Draw::checker(c, box, m.colorSwatchRadius, m.colorCheckerCell,
                      col.panel, col.appWorkspace);
    Draw::rect(c, box, m.colorSwatchRadius, fyll,
               Pen{enabled ? col.buttonBorder : col.border, m.borderWidth});
    if (!medText)
        return;
    const float tx = box.x + box.w + m.checkGap;
    Draw::text(c, {tx, content.y, std::max(0.0f, content.x + content.w - tx),
                   content.h},
               colorText(*färg, allowAlpha), fg);
}

// Prefixet vänster, suffixet högerställt och talet i fältet emellan, där
// editorn också står.
void drawNumber(const Ctx& c, const gfx::Rectf& content, Align align,
                const std::string& display, const NumberSpec& spec,
                gfx::Color fg)
{
    const float pad = c.style->fieldPadX;
    if (!spec.prefix.empty())
        Draw::text(c, {content.x, content.y, textW(c, spec.prefix), content.h},
                   spec.prefix, fg);
    const gfx::Rectf fält = numberFieldRect(c, content, spec);
    Draw::text(c,
               {fält.x + pad, content.y, std::max(0.0f, fält.w - 2.0f * pad),
                content.h},
               display, fg, align);
    if (!spec.suffix.empty())
        Draw::text(c, content, spec.suffix, fg, Align::Right);
}

void drawError(const Ctx& c, const gfx::Rectf& cell)
{
    Draw::rect(c, cell, 0.0f, c.theme->colors.errorSurface);
}

gfx::Color errorText(const Ctx& c)
{
    return c.theme->colors.onError;
}

// KNAPPEN per UI i knappens färgroller, platt (ingen skugga, ingen laminering):
//   vila       `button`, kanten `buttonBorder` (Slate `border`)
//   hover      `buttonHover`, i Slate kanten `accent`
//   tryck      `buttonPress`; Glaze sänker innehållet, Minimalistic tänder
//              kanten i `accent`
//   inaktiv    `buttonDisabled`, kanten `border`, text och bild dämpade
void drawCellButton(const Ctx& c, const gfx::Rectf& cell, const std::string& text,
                    const ImageSource& img, bool enabled, bool pressed,
                    bool hovered)
{
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const gfx::Rectf b   = Draw::inset(cell, m.cellButtonInset);
    if (b.w <= 0.0f || b.h <= 0.0f)
        return;
    const float rad =
        m.buttonRadius >= 0.0f ? m.buttonRadius : std::min(b.w, b.h) / 2.0f;
    const gfx::Color fyll = !enabled ? col.buttonDisabled
                            : pressed ? col.buttonPress
                            : hovered ? col.buttonHover
                                      : col.button;
    const Content    ct   = buttonContent(c, text, img, enabled);

    switch (c.ui) {
    case UI::Glaze:
        Draw::rect(c, b, rad, fyll,
                   Pen{enabled ? col.buttonBorder : col.border, m.borderWidth});
        Draw::content(c,
                      enabled && pressed
                          ? gfx::Rectf{b.x, b.y + m.buttonPressShift, b.w, b.h}
                          : b,
                      ct);
        break;
    case UI::Minimalistic:
        Draw::rect(c, b, rad, fyll,
                   Pen{!enabled  ? col.border
                       : pressed ? col.accent
                                 : col.buttonBorder,
                       m.borderWidth});
        Draw::content(c, b, ct);
        break;
    case UI::Slate:
    default:
        Draw::rect(c, b, rad, fyll,
                   Pen{enabled && (hovered || pressed) ? col.accent : col.border,
                       m.borderWidth});
        Draw::content(c, b, ct);
        break;
    }
}

void drawCategoryRow(const Ctx& c, const gfx::Rectf& r, const std::string& text,
                     const ImageSource& img, bool expanded, bool enabled,
                     float padX)
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    Draw::rect(c, r, 0.0f, col.panelHeader);
    const float      halvW = expanded ? m.chevronW : m.chevronH;
    const float      halvH = expanded ? m.chevronH : m.chevronW;
    const gfx::Rectf e{r.x + padX, r.y, m.treeExpanderW, r.h};
    Draw::chevron(c,
                  {e.x + e.w / 2.0f - halvW, e.y + e.h / 2.0f - halvH,
                   2.0f * halvW, 2.0f * halvH},
                  expanded ? Dir::Down : Dir::Right, Pen{col.textDim, 0.0f});
    float       x    = e.x + e.w + m.gridPadX;
    const float sida = imageSide(c, img);
    if (sida > 0.0f) {
        Draw::stateGlyph(c, {x, r.y + (r.h - sida) / 2.0f, sida, sida}, img,
                         enabled);
        x += sida + m.iconGap;
    }
    Draw::text(c, {x, r.y, std::max(0.0f, r.x + r.w - x), r.h}, text,
               enabled ? col.text : col.textDim);
}

// ------------------------------------------------------------- TOOLTIPEN

TrText invalidToolTip()
{
    return tr(str::InvalidValue);
}

} // namespace glaze::valuecell
