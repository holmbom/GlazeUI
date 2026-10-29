// Button — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och den gör
// det med FÅ RADER: varje rad i draw() är en sak man ser. Måtten kommer ur
// stilen (c.style), färgerna ur temat (c.theme->colors) och ritandet ur
// Draw — inga tal och inga färger i den här filen.
//
// Den enda widget som har ALLA TRE ritgrenarna. De är tre
// OLIKA saker man ser, inte samma ritning med andra tal:
//   Slate         en rundad platta som LYFTER med raise-skuggan vid hover
//   Glaze         en LAMINERAD yta — två klippta ljusfält över en tonad
//                 kropp, bestämd kontur, kort låg skugga
//   Minimalistic  OUTLINE — ingen egen yta i vila; kanten ÄR formen
// Just det är skälet att ritningen är en switch och inte bara en tabell:
// hade skillnaden bara varit måtten hade Style räckt.
//
// STANDARDKNAPPEN — den knapp Enter trycker just nu i ett
// fönster med en acceptknapp (`AppWindow::isDefaultButton`) — tar befintliga
// ROLLER och inga nya mått: fyllningen är accentytan i sina tre tillstånd
// (`accentSurface`/`…Hover`/`…Press`) och texten `onAccent`, paret som
// contrast_gate redan grindar till 4,5:1. Per familj:
//   Slate         konturen i fyllningens färg
//   Glaze         UTAN laminering — det vita fältet sänker `onAccent` till
//                 2,91:1 på accentytan (mätt i listbox.cpp)
//   Minimalistic  kanten i fyllningens färg
// En inaktiverad knapp är aldrig standard.
//
// contentSize() talar LOGISKA px, som resten av widget-filen: `Draw::measure`
// svarar logiskt och skalningen sker en gång, i `Widget::resolve`.

#include "glaze/widgets/button.h"

#include <algorithm>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("button", Button)

bool Button::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {   // title = äldre alias (regel 4-arv)
        setText(value);
        return true;
    }
    if (key == "img") {
        // "lista/nyckel" — samma par som glaze::img. Miss ⇒ ogiltigt
        // handtag (loggas av img, första gången) — knappen ritar bara text.
        AssetHandle h;
        if (!propparse::parseImg(value, h))
            return false;
        setImg(h);
        return true;
    }
    if (key == "imagepos" || key == "imagePos") {
        if (value == "left")       setImagePos(ImagePos::Left);
        else if (value == "right") setImagePos(ImagePos::Right);
        else if (value == "above") setImagePos(ImagePos::Above);
        else if (value == "below") setImagePos(ImagePos::Below);
        else return false;
        return true;
    }
    // DIALOGRESULTATET — värdet kommer GEMENT ur
    // grammatikens `parseEnumTail` (`glaze::DialogResult::OK` ⇒ "ok"). Ett
    // okänt namn är ingen gissning: `false` ⇒ en opak rad.
    if (key == "dialogResult" || key == "dialogresult") {
        if (value == "none")        setDialogResult(DialogResult::None);
        else if (value == "ok")     setDialogResult(DialogResult::OK);
        else if (value == "cancel") setDialogResult(DialogResult::Cancel);
        else if (value == "yes")    setDialogResult(DialogResult::Yes);
        else if (value == "no")     setDialogResult(DialogResult::No);
        else if (value == "abort")  setDialogResult(DialogResult::Abort);
        else if (value == "retry")  setDialogResult(DialogResult::Retry);
        else if (value == "ignore") setDialogResult(DialogResult::Ignore);
        else return false;
        return true;
    }
    if (key == "iconSize") {
        float v = 0.0f;
        if (propparse::parseNumber(value, v))
            setIconSize(v);   // negativt ⇒ sättarens logg + no-op
        else
            gfx::log("Button(%s): ogiltig iconSize: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    return Widget::setProp(key, value);
}

void Button::setIconSize(float px)
{
    if (!(px >= 0.0f)) {
        gfx::log("Button(%s): setIconSize(%g) — 0 är AUTO, ignoreras",
                 name.c_str(), double(px));
        return;
    }
    if (iconSize_ == px)
        return;
    iconSize_ = px;
    invalidate();
}

// KLICKET: handlern FÖRST, genom vakten — den får riva
// knappen, flytta den eller stänga fönstret själv — och därefter resultatet, om
// knappen har ett och den fortfarande sitter i ett levande fönster.
// Stängningen är uppskjuten som alltid, så `onClose`-vetot gäller.
void Button::clicked()
{
    if (!fireEvent(onClick))
        return;   // knappen dog, lossades eller tappade sitt fönster
    if (dialogResult_ == DialogResult::None)
        return;
    if (AppWindow* w = appWindow())
        w->closeWithResult(dialogResult_);
}

void Button::performClick()
{
    if (isEnabled())
        clicked();
}

bool Button::shownAsDefault() const
{
    const AppWindow* w = appWindow();
    return w && w->isDefaultButton(*this);
}

// Knappens innehåll som EN sak — bild, text eller båda. Assetens
// storleksklass bestämmer ikonsidan; saknas den (en bild ur pixlar har
// ingen) faller den till stilens.
//
// INAKTIV IKON: texten dimmas med sin färg, och ikonen ritas med sänkt
// OPACITET (`Content::imgOpacity`, 1 − `Style::iconFade`), som tonar den mot
// plattan i vilken ritning och vilken palett som helst.
Content Button::content(const Ctx& c) const
{
    // En SATT ikonstorlek vinner över båda.
    const float k = iconSize_ > 0.0f ? iconSize_ : img_.sizeClass();

    Content ct{img_, text(), labelColor(c), imagePos_, c.style->iconGap,
               k > 0.0f ? k : c.style->iconFallback};
    if (!isEnabled())
        ct.imgOpacity = 1.0f - c.style->iconFade;
    return ct;
}

gfx::Sizef Button::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;   // helt satt storlek: rör ingenting

        // Autostorlek BARA för axlar användaren lämnat på 0 — en satt
        // width/height vinner alltid (en ikon får aldrig ändra en
        // befintlig apps layout).
        //
        // Gäller ÄVEN rena textknappar: de får mått när layouten inte satt
        // någon storlek (annars vore knappen osynlig), och ingen knapp som
        // redan HAR mått påverkas. Det är den här vägen som gör Style:ns
        // höjd till det man FÅR: Glazes Regular 14.72 + 2 × 6.3 = 27.3,
        // Minimalistics 12.16 + 2 × 2.92 = 18.0. Höjden SKRIVS aldrig, den
        // FALLER UT.
        //
        // ALLA tre ritningarna delar den här grenen med flit — axelregeln
        // (satt axel vinner, `<= 0` är AUTO) är gemensam, och en egen kopia
        // per UI hade kunnat glida isär från de andra.
        const gfx::Sizef m = Draw::measure(c, content(c));
        if (s.w <= 0.0f) s.w = m.w + 2.0f * c.style->buttonPadX;
        if (s.h <= 0.0f) s.h = m.h + 2.0f * c.style->buttonPadY;
        return s;
    }
    }
}

void Button::draw(const Ctx& c)
{
    const gfx::Rectf  b = c.bounds();
    const auto&       m = *c.style;
    const auto&       col = c.theme->colors;

    const float rad      = radius(c, b);
    const bool  standard = shownAsDefault();   // aldrig inaktiverad

    switch (c.ui) {
    // GLAZE — "Soft Laminate": en tonad accentkropp med två breda ljusa
    // fält över sig, en bestämd kontur och en kort låg skugga. Windows 11
    // ger siluetten och disciplinen, macOS ljusfördelningen — och djupet
    // görs med FÄLT, aldrig med en rand: en rand läser som en söm på en
    // 28 px hög knapp.
    //
    // De fyra tillstånden (princip 7) ligger i färgen och i ETT mått:
    //   vila       kropp + laminering + skugga
    //   hover      kroppen går mot accenten (den är redan för blek för
    //              att kunna ljusna)
    //   tryck      skuggan och lamineringen FÖRSVINNER och innehållet
    //              sjunker — knappen ligger platt mot ytan
    //   inaktiv    neutral kropp, neutral kontur, dimmad text; varken
    //              skugga eller laminering
    // STANDARDKNAPPEN är accentytan med `onAccent` och utan laminering (se
    // filhuvudet) — skuggan, konturen och trycket är knappens vanliga.
    case UI::Glaze:
        if (isEnabled() && !pressed())
            Draw::shadow(c, b, rad, 0.0f, m.buttonShadowDy, m.buttonShadowBlur,
                         col.shadow);

        Draw::rect(c, b, rad, fillColor(c));

        if (isEnabled() && !pressed() && !standard)
            Draw::laminate(c, b, rad, col.sheen, m.sheenLower, col.sheenHigh,
                           m.sheenUpper);

        Draw::frame(c, b, rad,
                    Pen{isEnabled() ? col.buttonBorder : col.border,
                        m.borderWidth});

        Draw::content(c, contentRect(c, b), content(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        rad + m.focusOffset, Pen{col.accent, m.focusWidth});
        break;

    // MINIMALISTIC — "Outline": knappen HAR ingen egen yta i vila. Kanten
    // ÄR formen, och det är därför den mäts hårdast av alla roller
    // (buttonBorder ≥ 3:1 mot bakgrunden i BÅDA lägena — grindat i
    // tests/contrast_gate.cpp).
    //
    // De fyra tillstånden (princip 7) ligger i FYLLNADEN och i kantens
    // färg — aldrig i en förskjutning: vid 18 px höjd är 1 px en artondel
    // av knappen och läser som ett hopp, inte som ett tryck.
    //   vila       bara kant + text (ytan är fönstrets)
    //   hover      en svag fyllning tänds INNANFÖR kanten
    //   tryck      fyllningen mörknar och kanten går mot accenten
    //   inaktiv    kant och text dimmas, ingen fyllning
    // STANDARDKNAPPEN fyller accentytan och ritar kanten i SAMMA färg —
    // fyllningen betyder tillståndet, familjens regel.
    case UI::Minimalistic:
        // YTAN FYLLS I VILA: familjens övriga teman ger komponenterna en
        // egen bakgrund, och Minimalistic gör det också i vila och inte bara
        // vid hover — annars flyter kontrollerna ihop med fönstret. Färgen
        // är `button` = `panel`, en ljushetssteg från `windowBg` i alla nio
        // paletter. `fillColor()` väljer tillståndsfärgen, så
        // hover/tryck/inaktiv följer den.
        Draw::rect(c, b, rad, fillColor(c));

        Draw::frame(c, b, rad,
                    Pen{!isEnabled()        ? col.border
                        : standard          ? fillColor(c)
                        : pressed()         ? col.accent
                                            : col.buttonBorder,
                        m.borderWidth});

        Draw::content(c, b, content(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        rad + m.focusOffset, Pen{col.accent, m.focusWidth});
        break;

    case UI::Slate:
    default:   // okänd ritning faller till Slate — övriga avviker per widget
        // Skuggan lyfter knappen ur ytan; trycket "sänker" den (skuggan bort).
        // STANDARDKNAPPEN bär konturen i fyllningens färg.
        if (isEnabled() && !pressed())
            Draw::shadow(c, b, rad, 0.0f, m.raiseDy,
                         hovered() ? m.raiseBlurHover : m.raiseBlur,
                         col.shadow);

        Draw::rect(c, b, rad, fillColor(c),
                   Pen{standard ? fillColor(c) : hotBorder(c), m.borderWidth});
        Draw::content(c, b, content(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset), rad + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }
}

// Innehållets yta. Den skiljer sig från knappens bara vid TRYCK, då den
// sjunker — det är hela tryckrörelsen, och den definieras här EN gång så
// att bild och text alltid följs åt.
gfx::Rectf Button::contentRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (!pressed() || !isEnabled())
        return b;
    return gfx::Rectf{b.x, b.y + c.style->buttonPressShift, b.w, b.h};
}

// Knappens radie i logiska px. Negativt mått = PILLER, och då är radien
// halva kortsidan — samma uttryck som Sliders tumme och scrollistans
// tumme; det är formens definition, inte ett gömt designtal.
float Button::radius(const Ctx& c, const gfx::Rectf& b) const
{
    const float r = c.style->buttonRadius;
    return r >= 0.0f ? r : std::min(b.w, b.h) / 2.0f;
}

// Fyllnaden är knappens EGEN: den har ett press-läge som t.ex. checkboxen
// saknar, så den delas inte. STANDARDKNAPPEN tar accentytans tre tillstånd;
// en inaktiverad knapp är aldrig standard och faller till den neutrala.
gfx::Color Button::fillColor(const Ctx& c) const
{
    const auto& col = c.theme->colors;

    if (!isEnabled()) return col.buttonDisabled;
    if (shownAsDefault()) {
        if (pressed()) return col.accentSurfacePress;
        if (hovered()) return col.accentSurfaceHover;
        return col.accentSurface;
    }
    if (pressed())     return col.buttonPress;
    if (hovered())     return col.buttonHover;
    return col.button;
}

// Texten läser mot sin EGEN bakgrund, inte mot fönstrets: står den på en
// fylld accentyta är det `onAccent` som gäller (vit i ljust läge, mörk i
// mörkt — annars klarar den ljusa accenten aldrig 4.5:1). En inaktiv
// knapp har ingen accentyta och faller därför till basens dimmade text.
gfx::Color Button::labelColor(const Ctx& c) const
{
    // Standardknappens accentyta bär `onAccent` (paret är grindat till
    // 4.5:1 i contrast_gate). Den vanliga kroppen är vald så att den bär den
    // VANLIGA textfärgen i båda lägena (svart på #cbbafb = 9.8:1, vit på
    // #422f78 = 11.0:1), och för den behövs ingen egen roll.
    if (isEnabled() && shownAsDefault())
        return c.theme->colors.onAccent;
    // APPENS FÄRG på en aktiv knapp — accentytan
    // ovan vinner som markeringen, och en inaktiv dimmas av basen.
    if (isEnabled() && color_)
        return *color_;
    return textColor(c);
}

// Space/Enter aktiverar en fokuserad knapp — WinForms-pariteten. Framtida
// textwidgets (TextInput) överrider keyDown själva och ärver INTE detta:
// mellanslag är text där, inte ett klick. En fokuserad knapp tar alltså
// sitt EGET Enter, före fönstrets standardknapp.
bool Button::keyDown(gfx::Key k, std::uint32_t)
{
    if (k == gfx::Key::Space || k == gfx::Key::Return) {
        clicked();
        return true;
    }
    return false;
}

} // namespace glaze
