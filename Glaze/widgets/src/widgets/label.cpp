// Label — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3), och den gör
// det med FÅ RADER: varje rad i draw() är en sak man ser. Ritandet går via
// Draw, måtten via stilen (c.style) och färgerna via temat — inga tal
// och inga färger i den här filen. Kortaste mallen att kopiera.

#include "glaze/widgets/label.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <gfx/platform.h>

#include "../prop_parse.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("label", Label)

namespace {

// Namngivna färger är ROLLER som följer aktivt tema; #rrggbb är den enda
// fasta färgen (Custom).
bool parseColor(const std::string& v, Label& l)
{
    if (v == "text")   { l.setRole(Label::Role::Text);   return true; }
    if (v == "dim")    { l.setRole(Label::Role::Dim);    return true; }
    if (v == "accent") { l.setRole(Label::Role::Accent); return true; }
    // `titlebar` måste finnas här: utan raden föll verktygen tyst tillbaka på
    // `Text`, och en TopBar-titel ritades i fel färg i previewn medan appen
    // kompilerade. `heading` bär dessutom STORLEKEN: utan raden
    // hade previewn ritat en rubrik i brödtextens storlek, alltså en
    // WYSIWYG-lögn.
    if (v == "titlebar") { l.setRole(Label::Role::Titlebar); return true; }
    if (v == "heading")  { l.setRole(Label::Role::Heading);  return true; }
    // `custom`: `setRole(Role::Custom)` är ett lagligt publikt anrop som
    // emitteras och round-trippar, och utan raden lämnades rollen orörd: appen
    // ritade sin egen färg, previewn föregående temaroll.
    //
    // Att `setColor` sätter `Custom` SJÄLV löser det VANLIGA tvåanropsfallet
    // men INTE det här — en fristående `setRole(Custom)` ÅTERAKTIVERAR en
    // färg som redan ligger i `color_`, och det är en meningsfull operation.
    //
    // Semantiken är entydig: raden byter ROLL och rör inte färgen, precis som
    // de fem raderna ovan. Den som skriver `custom` menar den färg som redan
    // är satt.
    if (v == "custom")   { l.setRole(Label::Role::Custom);   return true; }
    // `#rrggbb` — den DELADE textformen (`propparse::parseHexColor`;
    // färgväljaren läser och skriver samma form). Här krävs `#`: utan det hade
    // ett rollnamn och en färg kunnat se likadana ut. En egen `strtoul`-läsning
    // vore fel här: den släpper igenom "# 12345" och "#+12345", för `strtoul`
    // hoppar blanksteg och tar ett tecken.
    gfx::Color färg{};
    if (!v.empty() && v[0] == '#' && propparse::parseHexColor(v, färg)) {
        // ETT anrop: `setColor` sätter `Custom` SJÄLV, så ordningen "färgen på
        // plats när Custom slår till" bor i settern, och textvägen ÄRVER den
        // i stället för att upprepa den — de två kan inte glida
        // isär.
        l.setColor(färg);
        return true;
    }
    return false;
}

} // namespace

bool Label::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {
        // C++-API:t heter alltid text; "title" lever kvar som äldre alias
        // så äldre filer fortsätter läsas (hård regel 4).
        setText(value);
        return true;
    }
    if (key == "color" || key == "role") {
        // "role" = C++-fältnamnet (builderns preview sätter fält via
        // setProp); "color" är det äldre nyckelnamnet — samma värden.
        if (!parseColor(value, *this))
            gfx::log("setProp(%s): ogiltig färg: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "wrap") {
        // Samma form som varje annan bool-egenskap (checked/border/clip):
        // allt som inte är yes/true/1 är false — ingen egen felgren.
        setWrap(propparse::parseBool(value));
        return true;
    }
    if (key == "align") {
        Align a = Align::Left;
        if (propparse::parseAlign(value, a))
            setAlign(a);
        else
            gfx::log("setProp(%s): ogiltig align: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "elide") {
        setElide(propparse::parseBool(value));   // `wrap`s form
        return true;
    }
    if (key == "vAlign") {
        VAlign v = VAlign::Center;
        if (propparse::parseVAlign(value, v))
            setVAlign(v);
        else
            gfx::log("setProp(%s): ogiltig vAlign: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "buddy") {
        setBuddy(value);   // ett syskons namn — ett okänt är tyst egen höjd
        return true;
    }
    if (key == "widthGroup") {
        setWidthGroup(value);   // gruppnamnet — tomt = ingen grupp
        return true;
    }
    if (key == "haloWidth") {
        float v = 0.0f;
        if (propparse::parseNumber(value, v))
            setHaloWidth(v);   // negativt ⇒ sättarens logg + no-op
        else
            gfx::log("setProp(%s): ogiltig haloWidth: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    return Widget::setProp(key, value);
}

// GLORIANS BREDD — 0 är stilens (`Button::setIconSize`s form).
void Label::setHaloWidth(float px)
{
    if (!(px >= 0.0f)) {
        gfx::log("Label(%s): setHaloWidth(%g) — 0 är stilens bredd, ignoreras",
                 name.c_str(), double(px));
        return;
    }
    if (haloWidth_ == px)
        return;
    haloWidth_ = px;
    invalidate();
}

// Den satta bredden eller stilens, aldrig mer än `overdraw` — den marginal
// damage-ytan räknar med utanför etikettens rect. 0 utan gloria.
float Label::haloWidthAt(const Ctx& c) const
{
    if (!halo_)
        return 0.0f;
    return std::min(haloWidth_ > 0.0f ? haloWidth_ : c.style->labelHaloWidth,
                    c.style->overdraw);
}

// BREDDGRUPPEN. Den gamla gruppen tappar en medlem och den
// nya får en, så båda ritas om — hela fönstret, för fälten som ankrats mot
// medlemmarna flyttar med. (Två tomma namn är lika och når aldrig hit.)
void Label::setWidthGroup(std::string grupp)
{
    if (widthGroup_ == grupp)
        return;
    widthGroup_ = std::move(grupp);
    if (AppWindow* f = appWindow())
        f->invalidate();
    else
        invalidate();
}

// Mitt mått kan ha ändrats (`setText`, `setRole`). I en grupp ändrar det
// GRUPPENS bredd, och medlemmarna i andra föräldrar och fälten ankrade mot dem
// är inte dirty — fönstret ritas om. Annars bara jag.
void Label::sizeMayChange()
{
    if (!widthGroup_.empty())
        if (AppWindow* f = appWindow()) {
            f->invalidate();
            return;
        }
    invalidate();
}

// Jag eller en förfader visades eller doldes (widget.h): en dold medlem räknas
// inte i gruppen, så de andra medlemmarnas bredd kan ha ändrats.
void Label::shownChanged()
{
    if (!widthGroup_.empty())
        if (AppWindow* f = appWindow())
            f->invalidate();
}

gfx::Sizef Label::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: en satt axel (> 0) vinner, en axel <= 0 är AUTO och
        // fylls av textens motsvarande mått. Per AXEL — att sätta bredden
        // ska inte tvinga fram en höjd.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        // RADBRUTEN AUTO-HÖJD för en SATT bredd — genom samma krok som
        // `resolve()` frågar med den bredd layouten GER (sträckt eller
        // dockad, se tabellen i label.h), så de två aldrig glider isär. Utan
        // satt bredd svarar kroken inte, och då gäller enradsmätningen nedan
        // — inte en gissad brytbredd: en gissning hade gett en höjd som inte
        // stämmer med det som ritas efteråt.
        if (const std::optional<float> h = heightForWidth(c, width)) {
            s.h = *h;   // grannen är redan inräknad i kroken
            return s;
        }
        // MÄT MED ROLLENS FONT — annars ljuger autostorleken, och det är
        // just autostorleken som gör Style-axeln ärlig (en rubrik som
        // mäts i brödtextens font får en yta som klipper sin egen text).
        //
        // VÄGEN ÄR `Draw::measure` OCH INTE `roleFont(c)->measure`: fonten
        // talar FYSISKA px, Draw delar med skalan. En direkt fontmätning
        // blandar enheter — den ser ut som en passthrough men är en fysisk
        // mätning, och en sed över `* c.scale` lämnar den orörd och tyst fel.
        const gfx::Sizef t = Draw::measure(roleCtx(c), text());
        // BREDDGRUPPEN: AUTO-bredden är gruppens.
        if (s.w <= 0.0f) s.w = widthGroup_.empty() ? t.w : groupWidth(c);
        // GRANNEN: AUTO-höjden är minst grannens mätta höjd.
        if (s.h <= 0.0f) s.h = std::max(t.h, buddyHeight(c));
        return s;
    }
    }
}

// Bredden UTAN grupp — det en medlem bidrar med till sin grupp: en satt
// `width` (axelregeln — den ger kolumnen ett golv), annars texten mätt med
// ROLLENS font som EN rad.
float Label::naturalWidth(const Ctx& c) const
{
    if (width > 0.0f)
        return width;
    return Draw::measure(roleCtx(c), text()).w;
}

// GRUPPENS BREDD: den bredaste medlemmens naturliga
// bredd i FÖNSTRETS träd — roten via `parent()`, och därifrån nedåt med en
// stack som aldrig kliver in i ett dolt delträd, så en medlem räknas bara när
// hela kedjan till roten syns. Jag själv är alltid med (en dold medlem som är
// ett ankarmål får gruppens bredd för sitt hål). Varje medlem mäts med SIN
// roll — `roleCtx` är medlemmens, inte min. Ingen medlems mätning frågar en
// annan widgets `contentSize`, så det finns ingen rekursion; körs i
// `resolve()` och loggar därför aldrig.
float Label::groupWidth(const Ctx& c) const
{
    const Widget* rot = this;
    while (rot->parent())
        rot = rot->parent();
    float                      bredast = naturalWidth(c);
    std::vector<const Widget*> kvar{rot};
    while (!kvar.empty()) {
        const Widget* w = kvar.back();
        kvar.pop_back();
        if (!w->isVisible())
            continue;
        if (w != this)
            if (const auto* l = dynamic_cast<const Label*>(w);
                l && l->widthGroup_ == widthGroup_)
                bredast = std::max(bredast, l->naturalWidth(c));
        for (const Widget* ch : w->children())
            kvar.push_back(ch);
    }
    return bredast;
}

// HÖJD FÖR BREDD — kontraktet i widget.h, tabellen i label.h.
// `measureWrapped` och kroken talar SAMMA enhet (logiska px): bredden går in
// rakt av och höjden ut likaså — skalningen sker hos konsumenten
// (`resolve`, `dockedSize`). Mätningen sker med ROLLENS font, som
// enradsmätningen och ritningen. Grannens höjd är ett golv här också.
std::optional<float> Label::heightForWidth(const Ctx& c, float w) const
{
    if (!wrap_ || height > 0.0f || w <= 0.0f)
        return std::nullopt;
    return std::max(Draw::measureWrapped(roleCtx(c), text(), w).h,
                    buddyHeight(c));
}

// GRANNEN: första SYSKON med namnet som inte är jag och inte en Label —
// en etikett som granne hade kunnat peka tillbaka, och då hade mätningen
// aldrig tagit slut. Dold eller inte: ett syskonankares mål räknas också när
// det är dolt. Körs i `resolve()`, så den loggar aldrig.
float Label::buddyHeight(const Ctx& c) const
{
    if (buddy_.empty() || !parent())
        return 0.0f;
    for (const Widget* s : parent()->children())
        if (s != this && s->name == buddy_ && !dynamic_cast<const Label*>(s))
            return Widget::measure(*s, c).h;
    return 0.0f;
}

TrText Label::toolTipAt(const Ctx& c, float x, float y) const
{
    if (!toolTipToken().empty())
        return Widget::toolTipAt(c, x, y);   // appens egen tooltip vinner
    if (elide_ && !wrap_ &&
        Draw::elide(roleCtx(c), text(), Draw::bounds(c, resolve(c)).w) != text())
        return text_;   // TOKENET — tooltipen följer språket som texten
    return {};
}

void Label::draw(const Ctx& c)
{
    switch (c.ui) {
    // GLAZE och MINIMALISTIC faller igenom till Slate, och det är ett
    // BESLUT — inte en lucka i designen.
    //
    // TEXT ÄR TEXT. Glazes formspråk är ytor (laminering, kontur, skugga)
    // och Minimalistics är kanten som form; ingendera har någon mening på en
    // etikett. En laminerad etikett vore en knapp, en konturerad etikett en
    // ruta med text i, och båda hade gjort skärmen stökigare utan att göra
    // något tydligare (princip 1 och 2: hierarkin byggs av typografi och
    // luft). Det som ÄR olika mellan familjerna hanteras av de andra
    // axlarna: FÄRGEN kommer ur temat och STORLEKEN ur stilens `uiFontSize`
    // — en Minimalistic-etikett är redan 10 px där Slates är 13.
    //
    // Att grenarna står HÄR i stället för att bara underförstås i `default:`
    // är hela poängen: identisk ritning i tre familjer ska vara skrivet.
    case UI::Glaze:
    case UI::Minimalistic:

    // SLATE — INNEHÅLL, varken yta eller kontroll.
    // En etikett med ram eller fyllning vore en försämring — hierarkin byggs
    // av typografi och luft (princip 2), inte av rutor.
    //
    // De fyra tillstånden (princip 7):
    //   vila       rollens färg (text/dim/accent/titlebar/custom)
    //   hover      — (en etikett är inte en kontroll)
    //   tryck      —
    //   fokus      — (inget tabbstopp)
    //   inaktiv    `textDim`, oavsett roll (roleColor)
    case UI::Slate:
    default:   // okänd ritning faller till Slate
        // Texten v-centreras i widgetens yta: auto-labels är exakt så höga
        // som texten, i en Bar-flex är ytan barens fulla höjd. SIDLED
        // styrs av `align` — synligt först när ytan är bredare än texten
        // (satt width, dubbelankare eller dockad remsa).
        //
        // RADBRUTEN text bryts mot den yta widgeten FICK (`c.bounds()`) —
        // alltså även när bredden kom ur ett dubbelankare eller en dockad
        // remsa. HÖJDEN följer med i de fallen
        // (`heightForWidth`, frågad med samma bredd: `c.bounds().w` är
        // widgetens fysiska bredd delad med skalan, och kroken frågas med
        // exakt den kvoten), så ytan rymmer redan alla rader.
        //
        // KLIPPET STÅR KVAR för de fall där YTAN och inte texten ger höjden
        // — `Dock::Fill`, Left/Right och Bar-flexen. Där hade raderna annars
        // ritats rakt ut över syskonen, som varken vet om dem eller
        // invalideras av dem. Klippt blir felet i stället SYNLIGT och lokalt:
        // texten tar slut vid den egna kanten, precis som Dropdowns fälttext
        // gör när den inte får plats. Klippet skadar inget i de rätta fallen
        // — där rymmer ytan redan alla rader, för höjden räknades ur dem.
        //
        // HÖJDLED: Center är hela ytan rakt av — samma bild bit för
        // bit — och Top/Bottom en rect lika hög som TEXTBLOCKET (en rad, de
        // brutna raderna) mot sin kant, där Draw centrerar blocket. KORTNINGEN
        // gäller en rad; med `wrap` bryts texten i stället.
    {
        const Ctx        rc = roleCtx(c);
        const gfx::Rectf b  = c.bounds();
        gfx::Rectf       r  = b;
        if (vAlign_ != VAlign::Center) {
            const float blockH = wrap_ ? Draw::measureWrapped(rc, text(), b.w).h
                                       : Draw::lineHeight(rc);
            r = gfx::Rectf{b.x, vAlign_ == VAlign::Top ? b.y : b.y + b.h - blockH,
                           b.w, blockH};
        }
        // Texten i EN färg i ett Ctx: glorian ritar den en gång
        // per punkt på ringen i sin färg, därefter ritas texten själv ovanpå.
        const auto rita = [&](const Ctx& tc, gfx::Color col) {
            if (wrap_)
                Draw::textWrapped(tc, r, text(), col, align_);
            else if (elide_)
                Draw::textElided(tc, r, text(), col, align_);
            else
                Draw::text(tc, r, text(), col, align_);
        };
        // Klippet följer glorian ut: en gloria klippt vid etikettens kant
        // vore avskuren precis där texten börjar.
        const float hw = haloWidthAt(c);
        if (wrap_)
            Draw::clip(c, Draw::inset(b, -hw));
        if (halo_)
            Draw::halo(rc, hw, [&](const Ctx& hc) { rita(hc, *halo_); });
        rita(rc, roleColor(c));
        if (wrap_)
            Draw::endClip(c);
        break;
    }
    }
}

// Färgen är en ROLL som följer temat (Custom är undantaget: fast färg);
// inaktiverad text är alltid dämpad.
gfx::Color Label::roleColor(const Ctx& c) const
{
    const auto& col = c.theme->colors;
    if (!isEnabled())
        return col.textDim;
    switch (role_) {
    case Role::Dim:      return col.textDim;
    case Role::Accent:   return col.accent;
    case Role::Titlebar: return col.titlebarText;
    case Role::Custom:   return color_;
    // Heading faller till `text` med FLIT: en rubrik skiljer sig genom
    // STORLEK, inte genom att vara accentfärgad. Vill man ha båda sätter
    // man Accent och storleken separat — men då är det två val, och det
    // ska synas att det är två.
    default:             return col.text;
    }
}

// Rollens FONT. Fonten är en fönsterresurs och kommer in via
// Ctx; `headingFont` FÅR vara null (fönster vars renderer inte finns än),
// och då är brödtextens font rätt svar — en rubrik i fel storlek är bättre
// än ingen text alls.
gfx::Font* Label::roleFont(const Ctx& c) const
{
    if (role_ == Role::Heading && c.headingFont)
        return c.headingFont;
    return c.font;
}

// Ctx med rollens font isatt — kopian är hela mekanismen: Draw läser
// `c.font`, så ett byte av storlek är ett byte av fält och inte ett nytt
// argument genom hela primitivkedjan.
Ctx Label::roleCtx(const Ctx& c) const
{
    Ctx rc = c;
    rc.font = roleFont(c);
    return rc;
}

} // namespace glaze
