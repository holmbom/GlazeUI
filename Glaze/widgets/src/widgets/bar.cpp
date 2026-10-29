// Bar — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3): draw()/
// contentSize() bor HÄR med switch (c.ui). BETEENDET — flex-raden —
// bor också här: placeChild är layoutChildren, tillståndslös som
// dock-carvingen (räknas fram per anrop, ingen cachad layout).

#include "glaze/widgets/bar.h"

#include <algorithm>
#include <cstdlib>

#include "glaze/draw.h"
#include "glaze/widgets/spacer.h"

#include "../prop_parse.h"

namespace glaze {

// Registrerat typnamn — GlazeBuilders palett/preview och headless-tester
// skapar barer via registret; barnen byggs med add/adopt.
GLAZE_REGISTER_WIDGET("bar", Bar)

Spacer* Bar::addSpacer(float grow)
{
    return add<Spacer>(grow);
}

bool Bar::setProp(const std::string& key, const std::string& value)
{
    if (key == "height") {
        height = std::strtof(value.c_str(), nullptr);
        return true;
    }
    if (key == "plain") {
        setPlain(propparse::parseBool(value));   // `wrap`s form
        return true;
    }
    return Panel::setProp(key, value);
}

// TVÄRAXELN. En fylld rad ger barnet hela barens spann; en REN rad
// barnets eget mått (`measure` talar LOGISKA px — skalan läggs på här, mot
// `c.area`), centrerat. Ett barn utan eget mått (0) fyller baren.
std::pair<float, float> Bar::crossSpan(const Widget& child, const Ctx& c,
                                       bool row) const
{
    const float areaPos = row ? c.area.y : c.area.x;
    const float areaLen = row ? c.area.h : c.area.w;
    if (!plain_)
        return {areaPos, areaLen};
    const gfx::Sizef m   = measure(child, c);
    const float      own = (row ? m.h : m.w) * c.scale;
    if (own <= 0.0f)
        return {areaPos, areaLen};
    return {areaPos + (areaLen - own) / 2.0f, own};
}

// Flex-raden (`placeChild`): barnen packas vänster→höger i
// add-ordning mot c.area (= barens childArea, fysiska px) — en STÅENDE bar
// går till `placeChildVertical` nedan. Varje barn tar
// sin mätta bredd plus sin grow-andel av det LEDIGA utrymmet, och barens
// fulla höjd — widgetarna v-centrerar sitt innehåll i recten. Dolda barn
// — och de som står utanför flödet (`inFlow`) — tar ingen plats
// (som dock-carvingen). Ryms inte barnen (luckan < 0)
// får växande barn inget extra och raden rinner över åt höger — klippet
// (Panel::clip) tar resten. O(n) per barn = O(n²) per rad, försumbart.
//
// ENHETERNA: `measure()` svarar i LOGISKA px
// medan `c.area` är FYSISK. Multiplikationen med `c.scale` sker därför här,
// på konsumtionssidan — samma brygga som `Widget::resolve` har. Glöms den
// blir flexraden rätt vid skala 1 och kollapsar vid skala 2, alltså exakt
// den sortens fel som bara syns på en HiDPI-skärm.
gfx::Rectf Bar::placeChild(const Widget& child, const Ctx& c) const
{
    if (isVertical())
        return placeChildVertical(child, c);

    // LUCKAN: fysiska px, och bara MELLAN två synliga barn — n
    // synliga barn har n − 1 luckor. Med luckan 0 (varje bar utom
    // ToolBar) är båda passen oförändrade.
    const float gap = childGap(c) * c.scale;

    // Pass 1: summera mätta bredder + grow-vikter (+ luckorna, som är
    // FASTA och alltså äter av samma lediga utrymme som en mätt bredd).
    float fixedW = 0.0f, growSum = 0.0f;
    int   synliga = 0;
    for (const Widget* ch : children()) {
        if (!inFlow(*ch))
            continue;
        fixedW  += measure(*ch, c).w * c.scale;
        growSum += ch->flexGrow();
        ++synliga;
    }
    if (synliga > 1)
        fixedW += gap * static_cast<float>(synliga - 1);
    const float leftover = std::max(0.0f, c.area.w - fixedW);

    // Pass 2: packa fram till barnet. Luckan läggs FÖRE varje synligt barn
    // utom det första, så ett dolt barn varken tar plats eller lucka.
    float x      = c.area.x;
    bool  första = true;
    for (const Widget* ch : children()) {
        float w = 0.0f;
        if (inFlow(*ch)) {
            if (!första)
                x += gap;
            första = false;
            w = measure(*ch, c).w * c.scale;
            if (ch->flexGrow() > 0.0f && growSum > 0.0f)
                w += leftover * ch->flexGrow() / growSum;
        }
        if (ch == &child) {
            const auto [y, h] = crossSpan(child, c, true);
            return gfx::Rectf{x, y, w, h};
        }
        x += w;
    }
    return gfx::Rectf{};   // inte barens barn — tom rect (ska inte hända)
}

// DEN STÅENDE FLEXEN — samma två pass som den liggande,
// med axlarna bytta: barnen packas uppifrån i add-ordning, var och ett med
// sin mätta HÖJD plus sin grow-andel, och barens fulla bredd. En egen
// funktion och inte en gren i varje rad ovan: den liggande vägen ska vara
// orörd av den stående.
gfx::Rectf Bar::placeChildVertical(const Widget& child, const Ctx& c) const
{
    const float gap = childGap(c) * c.scale;

    float fixedH = 0.0f, growSum = 0.0f;
    int   synliga = 0;
    for (const Widget* ch : children()) {
        if (!inFlow(*ch))
            continue;
        fixedH  += measure(*ch, c).h * c.scale;
        growSum += ch->flexGrow();
        ++synliga;
    }
    if (synliga > 1)
        fixedH += gap * static_cast<float>(synliga - 1);
    const float leftover = std::max(0.0f, c.area.h - fixedH);

    float y      = c.area.y;
    bool  första = true;
    for (const Widget* ch : children()) {
        float h = 0.0f;
        if (inFlow(*ch)) {
            if (!första)
                y += gap;
            första = false;
            h = measure(*ch, c).h * c.scale;
            if (ch->flexGrow() > 0.0f && growSum > 0.0f)
                h += leftover * ch->flexGrow() / growSum;
        }
        if (ch == &child) {
            const auto [x, w] = crossSpan(child, c, false);
            return gfx::Rectf{x, y, w, h};
        }
        y += h;
    }
    return gfx::Rectf{};
}

// En vanlig bar packar kant i kant — se headern.
float Bar::childGap(const Ctx&) const
{
    return 0.0f;
}

// …och drar hårlinjen hela vägen — se headern.
Bar::EdgeInsets Bar::innerEdgeInsets(const Ctx&) const
{
    return EdgeInsets{};
}

gfx::Sizef Bar::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // Som panelen dimensioneras baren av layouten (Dock=Top tar
        // höjden härifrån, bredden ur dockningen) — men höjden har en
        // default när ingen satts (som textinputens bredd).
        //
        // AXELREGELN: baren får INGEN
        // auto-BREDD. En bar är alltid dockad eller flexad
        // (TopBar/StatusBar), så en defaultbredd hade aldrig kunnat vinna
        // över dockningen — bara ljugit om vad widgeten är.
        //
        // STÅENDE byter axlarna: tjockleken är BREDDEN, med samma
        // default, och längden har ingen auto.
        if (isVertical())
            return gfx::Sizef{width > 0.0f ? width : c.style->barHeight,
                              height};
        const float h = height > 0.0f ? height : c.style->barHeight;
        return gfx::Sizef{width, h};
    }
    }
}

// En vanlig bar är fyrkantig. TopBar överrider.
float Bar::topRadius(const Ctx&) const
{
    return 0.0f;
}

// Barens yta. Är de övre hörnen rundade ritas ytan som en rundad rekt och
// den NEDRE halvan fylls fyrkantigt igen — `Draw::rect` har en radie per
// FYRKANT, inte per hörn, och en helrundad namnlist hade lämnat två ljusa
// flisor där den möter innehållet.
void Bar::fillBody(const Ctx& c, gfx::Color fill) const
{
    const gfx::Rectf b = c.bounds();
    const float      r = topRadius(c);
    if (r <= 0.0f) {
        Draw::rect(c, b, 0.0f, fill);
        return;
    }
    Draw::rect(c, b, r, fill);
    Draw::rect(c, gfx::Rectf{b.x, b.y + r, b.w, b.h - r}, 0.0f, fill);
}

// Barens INNERKANT i logiska px — den sida som vetter mot innehållsytan.
// En TopBar (Dock::Top) gränsar nedåt, en StatusBar (Dock::Bottom) uppåt;
// en odockad bar behandlas som en topplist, vilket är vad en bar är när
// ingen sagt något annat. Linjen ritas HÄR och inte som en `border` på
// panelen: en bar ska ha kant mot INNEHÅLLET, aldrig runt om. En STÅENDE bar
// gränsar åt höger vid Left och åt vänster vid Right, och luckorna
// räknas då uppifrån (`left`) och nedifrån (`right`). En bar som inte vetter
// mot något innehåll (`drawsInnerEdge()`, ett flytande verktygsfält) ritar
// ingen — vakten står HÄR och inte i varje ritgren, så ingen av de tre kan
// glömma den.
void Bar::innerEdge(const Ctx& c, gfx::Color col) const
{
    if (!drawsInnerEdge())
        return;
    const gfx::Rectf b  = c.bounds();
    const EdgeInsets in = innerEdgeInsets(c);
    const Pen        pen{col, c.style->borderWidth};
    if (isVertical()) {
        const float x = dock == Dock::Right ? b.x : b.x + b.w;
        Draw::line(c, x, b.y + in.left, x, b.y + b.h - in.right, pen);
        return;
    }
    const float y = dock == Dock::Bottom ? b.y : b.y + b.h;
    Draw::line(c, b.x + in.left, y, b.x + b.w - in.right, y, pen);
}

void Bar::draw(const Ctx& c)
{
    // REN RAD: varken yta eller hårlinje — barnen ritas direkt på
    // det som ligger under. Gäller alla tre ritningarna, så vakten står före
    // `switch`.
    if (plain_)
        return;
    switch (c.ui) {
    // GLAZE — en YTA med BESTÄMD KANT. Remsan är
    // `titlebar` som i Slate, och Glaze-regeln "kontur överallt" gäller
    // ytor också: en hårlinje längs INNERKANTEN, alltså den sida som vetter
    // mot innehållet.
    //
    // LINJEN RITAS I `frame` OCH INTE I `border`. De två rollerna är olika
    // saker: `border` är KONTROLLENS hårlinje och mäter 3.5:1 i Lavender,
    // medan `frame` är FÖNSTERKANTEN och är medvetet mjukare (#c6c5e2 mot
    // #7b7c9e i ljust läge). Barens innerkant är en FÖNSTER-linje — samma
    // sak som ramen runt fönstret — så den har fönstrets roll. I de
    // genererade paletterna är `frame == border`, så skillnaden syns bara i
    // Lavender.
    //
    // De fyra tillstånden (princip 7): inget av dem finns på baren SJÄLV.
    // Tillstånden bor i dess BARN (CaptionButton, Label).
    case UI::Glaze:
        fillBody(c, c.theme->colors.windowBg);
        innerEdge(c, c.theme->colors.frame);
        break;

    // MINIMALISTIC — "en bar är en HÅRLINJE mot innehållet, inte en färgad
    // remsa". Remsan fylls i `windowBg`, inte
    // i `titlebar`: baren FÖRSVINNER visuellt och bara linjen står kvar.
    //
    // Att den fylls ALLS är ett beslut: "ingen fyllning" går tekniskt
    // — men CaptionButtons återställ-glyf måste fylla sitt överlapp med
    // NÅGOT för att de två rutorna ska läsas isär, och den fyllnaden hade
    // lämnat en färgplätt mitt i en ofylld bar. `windowBg` ger samma intryck
    // som ingen fyllning och håller chromet helt. TopBar, CaptionButton och
    // den här raden är ETT paket och ändras tillsammans.
    case UI::Minimalistic:
        fillBody(c, c.theme->colors.windowBg);
        innerEdge(c, c.theme->colors.frame);
        break;

    // SLATE — en YTA med SAMMA hårlinje som
    // de två andra. `StatusBar` ärver ritningen; `TopBar` lägger bara till
    // rundade övre hörn via `topRadius()`.
    //
    // Linjen behövs: i de HANDSKRIVNA paletterna är `titlebar` och
    // `windowBg` identiska (Windows 243/243, "sömlös mot windowBg" står
    // utskrivet i theme.cpp), och i de GENERERADE ger ljushetstrappan
    // `titlebar` 0.930 mot `windowBg` 0.960 — en avvikande remsa. Färgskiftet
    // ensamt ger alltså ingen tydlig gräns.
    //
    // ALLA TRE FYLLER `windowBg`. Skälet är kapseln:
    // dess kropp härleds ur `titlebar`, så på en `titlebar`-bar hade
    // tonen hamnat 0.003 från underlaget och försvunnit — medan den syns i
    // Minimalistic, vars bar redan är `windowBg`. Att fylla likadant
    // överallt tar bort skillnaden i stället för att lägga en `switch`
    // i chromet, och namnlisten förblir familjeoberoende. `titlebar` lever
    // kvar som kapselkroppens bas och som `titlebarText`s partner.
    //
    // De fyra tillstånden (princip 7): inget av dem finns på baren SJÄLV.
    // Tillstånden bor i dess BARN (CaptionButton, Label) — en yta har inget
    // läge, den bär saker som har det.
    case UI::Slate:
    default:
        fillBody(c, c.theme->colors.windowBg);
        innerEdge(c, c.theme->colors.frame);
        break;
    }
}

} // namespace glaze
