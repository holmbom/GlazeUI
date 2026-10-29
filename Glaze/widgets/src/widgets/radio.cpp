// Radio — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// ring, prick, text, fokusring. Måtten kommer ur stilen (c.style),
// färgerna ur temat — inga tal och inga färger i den här filen. Även
// GRUPPEN bor här (syskonvandringen), inte i ramverket.

#include "glaze/widgets/radio.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("radio", Radio)

namespace {

// STÄMPLARNA (🐛 59): ett löpnummer för hela processen, så "senast satt"
// betyder samma sak i varje grupp. Bara UI-tråden sätter en radio (widgets
// är UI-trådens), och 64 bitar tar aldrig slut.
std::uint64_t nästaStämpel()
{
    static std::uint64_t n = 0;
    return ++n;
}

} // namespace

bool Radio::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {   // title = äldre alias (regel 4-arv)
        setText(value);
        return true;
    }
    if (key == "checked") {
        // SAMMA väg som sättaren — previewn och den kompilerade
        // `setChecked(…)` ger samma tillstånd (regel 8, 🐛 59).
        setChecked(propparse::parseBool(value));
        return true;
    }
    if (key == "group") {
        setGroup(value);
        return true;
    }
    return Widget::setProp(key, value);
}

// Gruppen är ETT tabbstopp. Ägaren av stoppet är den FÖRSTA ikryssade,
// annars gruppens första — bland de syskon som faktiskt KAN få fokus.
// Ingen ändring i AppWindow behövs; collectFocusable frågar per widget.
//
// isEnabled() i filtret är inte kosmetik: collectFocusable släpper ändå
// inte in en inaktiverad widget, så om en inaktiverad radio fick äga
// stoppet skulle gruppen bli helt onåbar med Tab.
//
// BESLUT (tabIndex): den här dynamiken BEHÅLLS oförändrad
// och ersätts INTE av en siffra. Den löser något ett tal inte kan — att
// stoppet VANDRAR till den ikryssade — och samspelet med `tabIndex` är
// gratis: gruppen bidrar med exakt ETT element till fokuslistan, så dess
// DFS-position ÄR gruppens plats i ordningen. Vill man flytta gruppen
// sätter man SAMMA tabIndex på alla medlemmar; dubbletter är harmlösa
// eftersom sorteringen är stabil. En riktig omskrivning till "gruppen är
// ETT tabbstopp med inredning" kräver komposit-tabbstoppet — en egen
// ström, inte den här.
bool Radio::focusable() const
{
    const Widget* p = parent();
    if (!p)
        return true;   // fristående radio (builderns preview) — eget stopp

    const Radio* owner = nullptr;
    for (const Widget* ch : p->children()) {
        const auto* r = dynamic_cast<const Radio*>(ch);
        if (!r || !r->isVisible() || !r->isEnabled() || !sameGroup(*r))
            continue;
        if (r->isChecked()) {
            owner = r;   // den VISADE — högst en per grupp (radio.h)
            break;
        }
        if (!owner)
            owner = r;
    }
    return owner == this || owner == nullptr;
}

// Det VISADE läget (🐛 59): flaggan satt, och inget syskon i SAMMA grupp har
// en flagga med en senare stämpel. Syskonen är förälderns barn just nu — så
// ett syskon som adopteras in efteråt avgör lika väl som ett som redan fanns,
// och `.glazeui`:s ordning (sättarna före trädbygget) ger samma läge som
// previewns (trädet först).
bool Radio::isChecked() const
{
    if (!checked_)
        return false;
    const Widget* p = parent();
    if (!p)
        return true;
    for (const Widget* ch : p->children()) {
        const auto* r = dynamic_cast<const Radio*>(ch);
        if (r && r != this && sameGroup(*r) && r->checked_ &&
            r->stamp_ > stamp_)
            return false;
    }
    return true;
}

// `true` sätter ALLTID en ny stämpel, också på en radio som redan är satt —
// annars hade två `setChecked(true)` på samma radio med ett syskon emellan
// gett olika läge beroende på om syskonet fanns när satsen kördes (det gör det
// i previewn men inte i appens `.glazeui`). Omritningen följer det VISADE
// läget i hela gruppen (regel 1: bara vid en ändring).
void Radio::setChecked(bool on)
{
    const bool före = isChecked();
    checked_        = on;
    if (on)
        stamp_ = nästaStämpel();
    if (isChecked() != före)
        invalidateGroup();
}

void Radio::invalidateGroup()
{
    invalidate();
    if (Widget* p = parent())
        for (Widget* ch : p->children())
            if (auto* r = dynamic_cast<Radio*>(ch);
                r && r != this && sameGroup(*r))
                r->invalidate();
}

gfx::Sizef Radio::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner, axel <= 0 är AUTO = ring + gap +
        // text.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        // `Draw::measure` och INTE `c.font->measure`:
        // fonten talar FYSISKA px, mätvägen LOGISKA. Draw är bryggan.
        const gfx::Sizef m   = Draw::measure(c, text());
        const float      box = c.style->radioSize;
        if (s.w <= 0.0f) s.w = box + c.style->radioGap + m.w;
        if (s.h <= 0.0f) s.h = std::max(box, m.h);
        return s;
    }
    }
}

// Ringen, v-centrerad i widgetens yta. Ritning, prick och fokusring utgår
// alla härifrån — en sanning om var ringen sitter.
gfx::Rectf Radio::boxRect(const Ctx& c) const
{
    const gfx::Rectf b = c.bounds();
    const float      s = c.style->radioSize;
    return gfx::Rectf{b.x, b.y + (b.h - s) / 2.0f, s, s};
}

gfx::Rectf Radio::dotRect(const Ctx& c) const
{
    const gfx::Rectf box = boxRect(c);
    const float      d   = c.style->radioDot;
    return gfx::Rectf{box.x + (box.w - d) / 2.0f, box.y + (box.h - d) / 2.0f,
                      d, d};
}

void Radio::draw(const Ctx& c)
{
    const gfx::Rectf b     = c.bounds();
    const gfx::Rectf box   = boxRect(c);
    const auto&      m     = *c.style;
    const auto&      col   = c.theme->colors;
    const bool       prick = isChecked();   // det VISADE läget, aldrig flaggan

    switch (c.ui) {
    // GLAZE — ringen är en LAMINERAD PLATTA, samma
    // material som kryssrutan och knappen. Att lamineringen klipps i ett
    // RUNT hörn är gratis: Draw::laminate fyller hela formen med radien den
    // får, så en cirkel lamineras med radien halva sidan.
    //
    // Pricken lamineras INTE — den är inte en yta man trycker på utan ett
    // VÄRDE ritat ovanpå ytan. Och den är `text`, inte `accent`, av exakt
    // samma MÄTTA skäl som kryssrutans bock: `accent` på den laminerade
    // kroppen mäter 2.40:1 i mörkt läge (under 3:1), `text` 5.90:1.
    //
    // De fyra tillstånden (princip 7):
    //   vila       `button` + laminering + kontur `buttonBorder`,
    //              prick `text`
    //   hover      kroppen går MOT accenten (`buttonHover`)
    //   tryck      lamineringen försvinner, kroppen `buttonPress`
    //   fokus      fristående ring runt RINGEN, aldrig runt texten
    //   inaktiv    `buttonDisabled`, kontur `border`, prick `textDim`
    case UI::Glaze:
        Draw::circle(c, box, boxFill(c));

        if (isEnabled() && !pressed())
            Draw::laminate(c, box, box.w / 2.0f, col.sheen, m.sheenLower,
                           col.sheenHigh, m.sheenUpper);

        Draw::frame(c, box, box.w / 2.0f,
                    Pen{isEnabled() ? col.buttonBorder : col.border,
                        m.radioStroke});

        if (prick)
            Draw::circle(c, dotRect(c),
                         isEnabled() ? col.text : col.textDim);

        Draw::text(c, {box.x + box.w + m.radioGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        box.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;

    // MINIMALISTIC — "Outline": ringen HAR ingen egen yta i vila, kanten är
    // formen.
    //
    // RINGEN STÅR STILL, och det är skillnaden mot kryssrutan i samma
    // familj: rutan FYLLS när den kryssas i, radion gör det inte. Skälet är
    // att radions valda läge redan HAR en egen form — pricken — medan
    // kryssrutans bock är en tunn glyf som behöver en yta bakom sig för att
    // läsas på avstånd. Att fylla båda hade dessutom gjort en radiogrupp
    // till en rad accentplattor, och en grupp där ETT val är fyllt läser
    // som en verktygsrad med ett aktivt läge, inte som ett enval.
    //
    // De fyra tillstånden (princip 7):
    //   vila       bara kant `buttonBorder`, prick `accent` när vald
    //   hover      svag fyllning innanför kanten (`buttonHover`)
    //   tryck      `buttonPress` — aldrig en förskjutning
    //   fokus      fristående ring runt RINGEN, halv tjocklek
    //   inaktiv    kant `border`, prick `textDim`, ingen fyllning
    case UI::Minimalistic:
        // YTAN FYLLS I VILA — se button.cpp. `boxFill()` bär
        // hela tillståndstabellen, så hover/tryck/inaktiv är oförändrade;
        // det som tillkommer är vilofärgen `button`.
        Draw::circle(c, box, boxFill(c));

        Draw::frame(c, box, box.w / 2.0f,
                    Pen{isEnabled() ? col.buttonBorder : col.border,
                        m.radioStroke});

        if (prick)
            Draw::circle(c, dotRect(c),
                         isEnabled() ? col.accent : col.textDim);

        Draw::text(c, {box.x + box.w + m.radioGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        box.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;

    // SLATE — ringen är en PLATTA, samma grammatik
    // som checkboxens ruta: fylld kropp, hårlinjekant, färgen växlar med
    // tillståndet. Platt — bara Button lyfter (skrivet undantag från
    // princip 5).
    //
    // De fyra tillstånden (princip 7):
    //   vila       `button` + hårlinje `border`, prick `accent`
    //   hover      `buttonHover`, kanten `accent`
    //   tryck      `buttonPress` (skilt från hover)
    //   fokus      fristående ring runt RINGEN, aldrig runt texten
    //   inaktiv    `buttonDisabled`; pricken är `textDim`
    case UI::Slate:
    default:   // okänd ritning faller till Slate — de två andra står ovan
        Draw::circle(c, box, boxFill(c), Pen{hotBorder(c), m.radioStroke});

        if (prick)
            Draw::circle(c, dotRect(c),
                         isEnabled() ? col.accent : col.textDim);

        Draw::text(c, {box.x + box.w + m.radioGap, b.y, b.w, b.h}, text(),
                   labelColor(c));

        // Ringen läggs runt RINGEN (som checkboxens ruta), inte texten.
        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(box, -m.focusOffset),
                        box.w / 2.0f + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }
}

// Ringens kropp per tillstånd — ordagrant Checkbox::boxFill och Buttons
// fillColor. De tre är plattor och ska växla likadant; skillnaden mellan
// dem ligger i FORMEN (rekt/cirkel), inte i hur många lägen de kan visa.
gfx::Color Radio::boxFill(const Ctx& c) const
{
    const auto& col = c.theme->colors;

    if (!isEnabled()) return col.buttonDisabled;
    if (pressed())    return col.buttonPress;
    if (hovered())    return col.buttonHover;
    return col.button;
}

// Användarvalet. Valet sätter flaggan och en ny stämpel och SLÄCKER
// syskonens flaggor — OCKSÅ när radion redan VISAS: efter ett användarval
// finns en flagga i gruppen, så en senare `setChecked(false)` inte tar fram
// ett gammalt val (radio.h: A och B satta, klick på den
// visade B, `B.setChecked(false)` hade gett tillbaka A). Var den redan visad ändras
// inget man ser: ingen omritning och inget onChange (WinForms-semantiken).
// Det händer bara i en levande app med trädet byggt, aldrig i `.glazeui`
// eller previewn, och kan därför vara eager. onChange och onCommit för samma
// val, och svaret säger om radion går att röra efteråt — en handler
// får riva den.
bool Radio::select()
{
    const bool visad = isChecked();
    checked_         = true;
    stamp_           = nästaStämpel();
    if (Widget* p = parent())
        for (Widget* ch : p->children())
            if (auto* r = dynamic_cast<Radio*>(ch);
                r && r != this && sameGroup(*r))
                r->checked_ = false;
    if (visad)
        return true;   // det visade valet är oförändrat
    invalidateGroup();
    return firePair(onChange, onCommit);   // nya värdet läses ur sender
}

void Radio::clicked()
{
    if (select())
        Widget::clicked();
}

// Gruppens ordning (synliga och aktiverade), för piltangenterna.
Radio* Radio::neighbour(int dir) const
{
    const Widget* p = parent();
    if (!p)
        return nullptr;

    std::vector<Radio*> g;
    for (Widget* ch : p->children()) {
        auto* r = dynamic_cast<Radio*>(ch);
        if (r && r->isVisible() && r->isEnabled() && sameGroup(*r))
            g.push_back(r);
    }
    const int n = static_cast<int>(g.size());
    if (n < 2)
        return nullptr;
    for (int i = 0; i < n; ++i)
        if (g[i] == this)
            return g[(i + dir + n) % n];
    return nullptr;
}

// SPACE väljer mig; piltangenterna flyttar valet inom gruppen
// (WinForms/OS-beteendet — gruppen är ETT tabbstopp, pilarna navigerar i
// den). Valet FÖLJER fokus, som i alla andra radiogrupper. Enter är INTE
// radioknappens: den bubblar till fönstrets
// standardknapp, som kryssrutans.
bool Radio::keyDown(gfx::Key k, std::uint32_t)
{
    if (k == gfx::Key::Space) {
        clicked();
        return true;
    }
    const int dir = (k == gfx::Key::Down || k == gfx::Key::Right)   ? 1
                    : (k == gfx::Key::Up || k == gfx::Key::Left)    ? -1
                                                                    : 0;
    if (dir == 0)
        return false;
    Radio* n = neighbour(dir);
    if (!n)
        return false;
    if (n->select())
        n->focus();   // focusable() är sant nu — den är ikryssad
    return true;
}

} // namespace glaze
