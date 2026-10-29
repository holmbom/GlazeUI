// Checkbox — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och den gör
// det med FÅ RADER: varje rad i draw() är en sak man ser. Ritandet går via
// Draw, måtten ur stilen (c.style) och färgerna ur temat — inga tal och
// inga färger i den här filen. Beteendet (toggle + onChange + keyDown) bor
// också här: hela widgeten i EN fil + headern.

#include "glaze/widgets/checkbox.h"

#include <algorithm>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("checkbox", Checkbox)

bool Checkbox::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {   // title = äldre alias (regel 4-arv)
        setText(value);
        return true;
    }
    if (key == "checked") {
        setChecked(propparse::parseBool(value));
        return true;
    }
    if (key == "threeState") {
        setThreeState(propparse::parseBool(value));
        return true;
    }
    // Enumets namn GEMENA (`parseEnumTail`s form); ett annat namn är okänt
    // värde och nyckeln svarar false, som Labels `align`.
    if (key == "checkState") {
        if (value == "unchecked")          setCheckState(CheckState::Unchecked);
        else if (value == "checked")       setCheckState(CheckState::Checked);
        else if (value == "indeterminate") setCheckState(CheckState::Indeterminate);
        else return false;
        return true;
    }
    return Widget::setProp(key, value);
}

gfx::Sizef Checkbox::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner, axel <= 0 är AUTO = ruta + gap +
        // text.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        // `Draw::measure` och INTE `c.font->measure`:
        // fonten talar FYSISKA px, mätvägen LOGISKA. Draw är bryggan.
        const gfx::Sizef m   = Draw::measure(c, text());
        const float      box = c.style->checkSize;
        if (s.w <= 0.0f) s.w = box + c.style->checkGap + m.w;
        if (s.h <= 0.0f) s.h = std::max(box, m.h);
        return s;
    }
    }
}

// MÄRKET — bocken eller mellanlägets streck, i rutan och i pennans färg. Ett
// ställe för alla tre ritningarna: det som skiljer familjerna är rutan och
// färgen, inte vad ett läge ser ut som. Strecket är `Draw::minus` med
// bockens penna — tecknets proportioner är Draws.
void Checkbox::drawMark(const Ctx& c, const gfx::Rectf& box,
                        gfx::Color col) const
{
    const Pen penna{col, c.style->checkStroke};
    switch (state_) {
    case CheckState::Checked:       Draw::check(c, box, penna); break;
    case CheckState::Indeterminate: Draw::minus(c, box, penna); break;
    case CheckState::Unchecked:     break;
    }
}

// Rutan, v-centrerad i widgetens yta. Ritning, bock och fokusring utgår
// alla härifrån — en sanning om var rutan sitter.
gfx::Rectf Checkbox::boxRect(const Ctx& c) const
{
    const gfx::Rectf b = c.bounds();
    const float      s = c.style->checkSize;
    return gfx::Rectf{b.x, b.y + (b.h - s) / 2.0f, s, s};
}

void Checkbox::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf box = boxRect(c);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;

    switch (c.ui) {
    // GLAZE — rutan är en LAMINERAD PLATTA: samma
    // material som knappen, bara mindre. Receptet är familjens och inte
    // widgetens: tonad kropp → två klippta ljusfält → bestämd kontur.
    // Lamineringen säger "det här är en materialiserad, aktiv yta"; en ruta
    // är precis det.
    //
    // BOCKEN ÄR `text`, INTE `accent` — mätt, inte valt. Kroppen är redan
    // accenttonad, och lamineringen ljusar upp den ytterligare: `accent` på
    // den LAMINERADE kroppen mäter **2.40:1 i mörkt läge** (rå 4.00), alltså
    // under 3:1 för en tillståndsmarkering. `text` mäter 12.20 / 5.90 på
    // samma yta. Det är dessutom samma resonemang som knappens etikett:
    // en Glaze-kropp är vald för att bära den VANLIGA textfärgen,
    // och accent på accent säger ändå ingenting.
    //
    // De fyra tillstånden (princip 7):
    //   vila       `button` + laminering + kontur `buttonBorder`,
    //              bock `text`
    //   hover      kroppen går MOT accenten (`buttonHover`) — den är för
    //              blek för att kunna ljusna
    //   tryck      lamineringen FÖRSVINNER och kroppen går till
    //              `buttonPress`; rutan ligger platt mot ytan
    //   fokus      fristående ring runt RUTAN, aldrig runt texten
    //   inaktiv    `buttonDisabled`, neutral kontur `border`, bock `textDim`,
    //              ingen laminering
    //   mellanläge  strecket i bockens färg, `text`/`textDim`
    case UI::Glaze:
        Draw::rect(c, box, m.checkRadius, boxFill(c));

        if (isEnabled() && !pressed())
            Draw::laminate(c, box, m.checkRadius, col.sheen, m.sheenLower,
                           col.sheenHigh, m.sheenUpper);

        Draw::frame(c, box, m.checkRadius,
                    Pen{isEnabled() ? col.buttonBorder : col.border,
                        m.borderWidth});

        drawMark(c, box, isEnabled() ? col.text : col.textDim);

        Draw::text(c, {box.x + box.w + m.checkGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        m.checkRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;

    // MINIMALISTIC — "Outline", men med passets DJÄRVASTE drag
    //. Familjens första regel är "ingen egen
    // yta i vila"; dess andra är "FYLLNING BETYDER TILLSTÅND". Ikryssad ÄR
    // ett tillstånd — därför är det enda stället där regel två slår regel
    // ett: den ikryssade rutan fylls med `accentSurface` och bocken ritas i
    // `onAccent`.
    //
    // Skälet är avläsbarhet, inte smak: en tom kant med en accentbock i
    // läser som "en bock ritad bredvid en ruta", medan en fylld ruta läser
    // som PÅ på en meters håll. Paret är dessutom grindat — `onAccent` mot
    // `accentSurface` mäter minst 4.65:1 över nio hues × två lägen
    // (tests/contrast_gate.cpp), så draget kostar ingen tydlighet.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ej ikryssad: BARA kant `buttonBorder` (ytan är fönstrets)
    //              ikryssad:    fylld `accentSurface`, bock `onAccent`
    //   hover      fyllningen tänds innanför kanten (`buttonHover`) resp.
    //              går till `accentSurfaceHover`
    //   tryck      `buttonPress` resp. `accentSurfacePress` — aldrig en
    //              förskjutning: noll djup rakt igenom
    //   fokus      fristående ring runt RUTAN, halv tjocklek
    //   inaktiv    kanten dimmas till `border`, bocken till `textDim`,
    //              ingen fyllning alls
    //   mellanläge är ETT TILLSTÅND som ikryssad: rutan fylls på
    //              samma sätt och strecket ritas i `onAccent` — Windows 11
    //              ritar mellanläget så
    case UI::Minimalistic: {
        // YTAN FYLLS I VILA — se button.cpp. Den IKRYSSADE
        // rutan bär sin accentyta, och den TOMMA rutan får en
        // vilofärg, så en okryssad ruta inte är ett hål i fönstret.
        const bool filled = isChecked() && isEnabled();
        if (filled)
            Draw::rect(c, box, m.checkRadius,
                       pressed()   ? col.accentSurfacePress
                       : hovered() ? col.accentSurfaceHover
                                   : col.accentSurface);
        else
            Draw::rect(c, box, m.checkRadius, boxFill(c));

        Draw::frame(c, box, m.checkRadius,
                    Pen{isEnabled() ? col.buttonBorder : col.border,
                        m.borderWidth});

        drawMark(c, box, filled ? col.onAccent : col.textDim);

        Draw::text(c, {box.x + box.w + m.checkGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        m.checkRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }

    // SLATE — rutan är en PLATTA: fylld kropp,
    // hårlinjekant, färgen växlar med tillståndet. Samma grammatik som
    // knappen, men PLATT — bara Button lyfter, och det undantaget från
    // princip 5 behåller sin betydelse bara så länge det är ensamt. En 2 px
    // oskärpa under en 18 px ruta hade dessutom bara blivit brus.
    //
    // De fyra tillstånden (princip 7):
    //   vila       `button` + hårlinje `border`, bock `accent`
    //   hover      `buttonHover`, kanten `accent`
    //   tryck      `buttonPress` (skilt från hover)
    //   fokus      fristående ring runt RUTAN, aldrig runt texten
    //   inaktiv    `buttonDisabled` (egen fyllning i den inaktiva kroppen), bock och text `textDim`
    //   mellanläge  strecket i bockens färg, `accent`/`textDim`
    case UI::Slate:
    default:   // okänd ritning faller till Slate — de två andra står ovan
        Draw::rect(c, box, m.checkRadius, boxFill(c),
                   Pen{hotBorder(c), m.borderWidth});

        drawMark(c, box, isEnabled() ? col.accent : col.textDim);

        Draw::text(c, {box.x + box.w + m.checkGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        // Ringen läggs runt RUTAN (som hover-markeringen), inte hela texten.
        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        m.checkRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }
}

// Rutans kropp per tillstånd. Samma trappa som Buttons fillColor — det ÄR
// poängen med att kalla dem båda plattor: skillnaden mellan en knapp och en
// kryssruta ska ligga i formen, inte i hur många lägen kroppen kan visa.
gfx::Color Checkbox::boxFill(const Ctx& c) const
{
    const auto& col = c.theme->colors;

    if (!isEnabled()) return col.buttonDisabled;
    if (pressed())    return col.buttonPress;
    if (hovered())    return col.buttonHover;
    return col.button;
}

void Checkbox::clicked()
{
    // ANVÄNDARENS växling — den enda vägen som fyrar onChange
    // (programmatisk `setChecked` gör det aldrig; kontraktet gäller alla
    // widgets). Fältet skrivs direkt och invalideringen görs av
    // `Widget::clicked`-vägen som ändå ritar om.
    //
    // TRE LÄGEN (WinForms): med `threeState` Unchecked → Checked →
    // Indeterminate → Unchecked; utan växlar klicket mellan tom och
    // ikryssad, och ett mellanläge appen satt räknas som ikryssat och blir
    // tomt (WinForms `Checked = !Checked`).
    if (threeState_)
        state_ = state_ == CheckState::Unchecked ? CheckState::Checked
                 : state_ == CheckState::Checked ? CheckState::Indeterminate
                                                 : CheckState::Unchecked;
    else
        state_ = isChecked() ? CheckState::Unchecked : CheckState::Checked;
    invalidate();
    // onChange och onCommit för samma växling, därefter onClick — med
    // vakten frågad emellan: en handler får riva eller flytta kryssrutan.
    // Nya värdet läses ur sender (isChecked()).
    if (firePair(onChange, onCommit))
        Widget::clicked();
}

// SPACE togglar en fokuserad checkbox (via clicked → onChange). Enter är
// INTE kryssrutans: den bubblar till fönstrets
// standardknapp, som i Win32-dialoger, WinForms och macOS — annars hade Enter
// i en dialog berott på vilken kontroll som råkade ha fokus.
bool Checkbox::keyDown(gfx::Key k, std::uint32_t)
{
    if (k == gfx::Key::Space) {
        clicked();
        return true;
    }
    return false;
}

} // namespace glaze
