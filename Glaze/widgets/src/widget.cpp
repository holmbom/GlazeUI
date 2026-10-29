// Widget-basens implementation: layoutlösning (resolve), damage-bokföring
// (invalidate/paintBounds) och default-måttet. De inbyggda widgetarna bor
// i varsin fil (label.cpp, button.cpp, ...) och ritar SIG SJÄLVA i sin egen
// draw() — delade rit-idiom bor i glaze/draw.h (hård regel 3).

#include "glaze/widget.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

#include <gfx/platform.h>

#include "glaze/app.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "menu_query.h"
#include "prop_parse.h"

namespace glaze {

// Basens egenskaper (nyckel/värde-text) — de alla widgets delar. Okänt
// värde för en känd nyckel loggas HÄR och ignoreras (returnera ändå true —
// nyckeln är vår); okänd nyckel = false, då loggar anroparen (regel 4).
bool Widget::setProp(const std::string& key, const std::string& value)
{
    using namespace propparse;

    if (key == "size") {
        if (!parseSize(value, width, height))
            gfx::log("setProp(%s): ogiltig size (förväntar BxH): %s "
                     "(ignoreras)", name.c_str(), value.c_str());
        return true;
    }
    if (key == "pos") {
        const auto v = splitComma(value);
        if (v.size() != 2) {
            gfx::log("setProp(%s): ogiltig pos (förväntar x,y): %s "
                     "(ignoreras)", name.c_str(), value.c_str());
            return true;
        }
        x = std::strtof(v[0].c_str(), nullptr);
        y = std::strtof(v[1].c_str(), nullptr);
        return true;
    }
    if (key == "dock") {
        if (!parseDock(value, dock))
            gfx::log("setProp(%s): ogiltig dock (förväntar none/left/top/"
                     "right/bottom/fill): %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "anchor") {
        if (!parseAnchors(value, anchors))
            gfx::log("setProp(%s): ogiltig anchor (förväntar kommalista av "
                     "left/top/right/bottom, eller none): %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "anchorLeft" || key == "anchorTop" ||
        key == "anchorRight" || key == "anchorBottom") {
        // SYSKONANKARET. Texten är "<namn>[,<gap>]" — samma
        // familj som `pos`/`anchor`: en strikt liten form, aldrig en
        // gissning. Ogiltigt värde loggas HÄR (setProp är en av
        // INMATNINGSvägarna) och ignoreras; `resolve()` självt faller
        // TYST tillbaka, för den körs 6+ gånger per frame.
        SiblingAnchor a;
        if (!parseSiblingAnchor(value, a)) {
            gfx::log("setProp(%s): ogiltig %s (förväntar id eller id,gap): "
                     "%s (ignoreras)", name.c_str(), key.c_str(),
                     value.c_str());
            return true;
        }
        if (key == "anchorLeft")        anchorLeft   = a;
        else if (key == "anchorTop")    anchorTop    = a;
        else if (key == "anchorRight")  anchorRight  = a;
        else                            anchorBottom = a;
        return true;
    }
    if (key == "right") {
        right = std::strtof(value.c_str(), nullptr);
        return true;
    }
    if (key == "bottom") {
        bottom = std::strtof(value.c_str(), nullptr);
        return true;
    }
    if (key == "tabIndex") {
        // Heltal; -1 (eller vad som helst negativt) = AUTO. Ingen räknare
        // och ingen utdelning — numret räknas ur DFS vid Tab-trycket.
        tabIndex = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
        return true;
    }
    if (key == "visible") {
        setVisible(parseBool(value));
        return true;
    }
    if (key == "enabled") {
        setEnabled(parseBool(value));
        return true;
    }
    // DRAG-AND-DROP — basens, för VARJE widget kan vara källa
    // eller mål. Textformen är bool som `visible`/`enabled`; `.glazeui`
    // bär settern (`lista.setAllowDrop(true);`), som readern normaliserar
    // till samma nyckel.
    if (key == "allowDrop") {
        setAllowDrop(parseBool(value));
        return true;
    }
    if (key == "allowDrag") {
        setAllowDrag(parseBool(value));
        return true;
    }
    // TOOLTIPEN — basens textfält. Textformen är strängen rakt
    // av; ett översättnings-id går via `setTextToken`, aldrig hit (regel 8:s
    // asymmetri — se setTextToken i widget.h).
    if (key == "toolTip") {
        setToolTip(value);
        return true;
    }
    // PEKARENS FORM — basens, som tooltipen. Värdet är
    // enumens namn med typens prefix eller utan (`gfx::PointerShape::Hand`,
    // `PointerShape::Hand`, `Hand`, och previewns gemena `hand`):
    // skiftlägesokänsligt på namnet, men ett ANNAT prefix är ett okänt värde.
    // Ett okänt värde är en KÄND nyckel — loggat, orört.
    if (key == "cursor") {
        static constexpr struct {
            const char*       namn;
            gfx::PointerShape form;
        } kFormer[] = {
            {"arrow", gfx::PointerShape::Arrow},
            {"ibeam", gfx::PointerShape::IBeam},
            {"hand", gfx::PointerShape::Hand},
            {"resizewe", gfx::PointerShape::ResizeWE},
            {"resizens", gfx::PointerShape::ResizeNS},
            {"notallowed", gfx::PointerShape::NotAllowed},
            {"crosshair", gfx::PointerShape::Crosshair},
            {"move", gfx::PointerShape::Move},
            {"closedhand", gfx::PointerShape::ClosedHand},
            {"hidden", gfx::PointerShape::Hidden},
            {"wait", gfx::PointerShape::Wait},
        };
        std::string sist = value;
        for (const char* prefix : {"gfx::PointerShape::", "PointerShape::"})   // lang_gate:rad enumprefix
            if (value.rfind(prefix, 0) == 0) {
                sist = value.substr(std::strlen(prefix));
                break;
            }
        for (char& ch : sist)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (const auto& f : kFormer)
            if (sist == f.namn) {
                setCursor(f.form);
                return true;
            }
        gfx::log("setProp(%s): okänd pekarform '%s' (ignoreras)",
                 name.c_str(), value.c_str());
        return true;
    }
    return false;
}

// PEKARENS FORM — bara lagringen; att VISA den är
// fönstrets (`AppWindow::updateCursor`), och omprövningen sker direkt.
void Widget::setCursor(gfx::PointerShape s)
{
    cursor_ = s;
    refreshPointer();
}

void Widget::resetCursor()
{
    cursor_.reset();
    refreshPointer();
}

void Widget::refreshPointer()
{
    if (AppWindow* f = liveOwner())
        f->refreshCursor();
}

// RULLNINGENS RIKTNING — bitparet för (enhet, axel):
// "satt" på den jämna biten, "vänd" på den udda. Bara musen och styrplattan
// har ett hjul.
namespace {
int hjulpar(InputDevice d, bool horizontal)
{
    return (d == InputDevice::Touchpad ? 2 : 0) + (horizontal ? 1 : 0);
}
} // namespace

void Widget::setWheelInvert(InputDevice d, bool vertical, bool horizontal)
{
    if (d == InputDevice::Keyboard) {
        gfx::log("%s::setWheelInvert: tangentbordet har inget hjul (ignoreras)",
                 name.c_str());
        return;
    }
    for (const bool sidled : {false, true}) {
        const int  i   = hjulpar(d, sidled) * 2;
        const bool vänd = sidled ? horizontal : vertical;
        wheelInvert_    = static_cast<std::uint8_t>(
            (wheelInvert_ & ~(3u << i)) | (1u << i) | ((vänd ? 1u : 0u) << (i + 1)));
    }
}

void Widget::clearWheelInvert(InputDevice d)
{
    if (d == InputDevice::Keyboard)
        return;
    for (const bool sidled : {false, true})
        wheelInvert_ = static_cast<std::uint8_t>(
            wheelInvert_ & ~(3u << (hjulpar(d, sidled) * 2)));
}

// Egen, förfädernas (till och med fönstrets rot — `parent()` slutar där) och
// sist appens. En popup är ett eget fönster: dess kedja slutar vid popupens
// rot, så den ärver från appen.
bool Widget::isWheelInverted(InputDevice d, bool horizontal) const
{
    if (d == InputDevice::Keyboard)
        return false;
    const int i = hjulpar(d, horizontal) * 2;
    for (const Widget* w = this; w; w = w->parent())
        if (w->wheelInvert_ & (1u << i))
            return (w->wheelInvert_ & (1u << (i + 1))) != 0;
    return detail::appWheelInverted(d, horizontal);   // skapar aldrig App
}

// STANDARDKNAPPEN — fönstret avgör om jag rör
// acceptknappen eller den fokuserade knappen (Widget är friend).
void Widget::refreshDefaultButton()
{
    if (AppWindow* f = liveOwner())
        f->defaultButtonMayChange(*this);
}

// TOOLTIPEN. Bara texten bor på widgeten; att VISA den är
// fönstrets sak (src/tooltip.cpp). Ingen invalidering — widgeten själv ser
// likadan ut — men en tooltip som just nu VISAS för den här texten stämmer
// inte längre, och fönstret stänger den.
void Widget::setToolTip(TrText t)
{
    if (toolTip_ == t)
        return;
    toolTip_ = std::move(t);
    if (AppWindow* f = liveOwner())
        f->toolTipChanged(*this);
}

// Basens mätning: fälten rakt av. `width`/`height` ÄR logiska px (det står
// vid deklarationen), och mätvägen talar samma enhet —
// så det finns ingen omräkning kvar att göra här. `c` är oanvänd och det
// är hela poängen: basen har inget att fråga kontexten om.
gfx::Sizef Widget::contentSize(const Ctx&) const
{
    return gfx::Sizef{width, height};
}

// AUTOSTORLEK UR BARNEN — kontraktet, motiveringen och det som
// förblir obestämt står vid deklarationen i widget.h. HÄR är det värt att se
// att funktionen är en INVERS av `resolve` och `carveSiblingDocks` nedan och
// delar deras urval: barn UTANFÖR dock-flödet räknas inte (de resolvas mot
// ett annat Ctx och hör inte till innehållslayouten), och ett DOLT DOCKAT barn
// räknas inte (det skär ingen remsa). Ett DOLT ODOCKAT barn får däremot sina
// KANTER men bidrar inte med något mått: `resolve` placerar
// ett syskon som ankrats mot det efter hålet, för `earlierSibling` frågar
// aldrig synligheten, och inversen ska stå på samma ställe.
//
// Talen är LOGISKA hela vägen: `measure()` svarar logiskt, `x`/`y`/`right`/
// `bottom` är logiska fält, och ingen `c.scale` finns i uttrycket. Det är den
// ENDA anledningen till att passen kan vara ren addition — i fysiska px hade
// varje barns offsets behövt skalas för sig.
namespace {

// KANTMODELLEN. En kant på en axel räknas FRÅN BÖRJAN (en
// koordinat från förälderns vänster-/överkant), FRÅN SLUTET (ett avstånd
// INÅT från förälderns höger-/nederkant), är OKÄND (den följer av något
// inversen inte räknar — ett dockat syskon, ett mål som självt är okänt) eller
// FRI (inget ankare binder den; den följer av den andra kanten och måttet).
enum class Ref : std::uint8_t { Fri, Början, Slut, Okänd };

struct Kant {
    Ref   ref = Ref::Fri;
    float v   = 0.0f;   // logiska px: koordinaten (Början) eller avståndet (Slut)
};

// Ett odockat barns AXEL efter härledningen: när- och fjärrkanten (vänster/
// över, höger/under), det mätta måttet, LUFTEN efter fjärrkanten och
// om barnet är MITT — ingen kant bunden.
struct Led {
    Kant  när, fjärr;
    float mått = 0.0f;
    float luft = 0.0f;
    bool  mitt = false;
};

// HÄRLEDNINGEN: de BUNDNA kanterna in, axeln ut — `resolve`s
// fyrgrensstruktur per axel. `luftFält` är `right`/`bottom`, och det räknas
// BARA när barnet är bundet i början och fritt i slutet: `resolve` läser inte
// fältet på en fri sida, så det säger ingenting om placeringen där — men det
// är avståndet barnet vill ha till det som står efter (WPF:s `Margin`). Ett
// barn bundet mot slutet bär ofta ett gammalt `x`/`y` från designern
// (`builder_window.cpp` skriver dem vid varje placering), och dem räknar vi
// därför inte.
Led härled(Kant när, Kant fjärr, float mått, float luftFält)
{
    Led l;
    l.mått = mått;
    if (när.ref == Ref::Fri && fjärr.ref == Ref::Fri) {
        // MITT — positionen beror på förälderns mått, men ytan måste rymma
        // barnet: bidraget är måttet, och kedjor räknar med [0, m] (en
        // approximation).
        l.mitt  = true;
        l.när   = {Ref::Början, 0.0f};
        l.fjärr = {Ref::Början, mått};
        return l;
    }
    // Ett OMVÄNT par (närkanten från slutet, fjärrkanten från början) har en
    // bredd som beror på förälderns — okänd, liksom båda kanterna.
    if (när.ref == Ref::Slut && fjärr.ref == Ref::Början) {
        l.när   = {Ref::Okänd, 0.0f};
        l.fjärr = {Ref::Okänd, 0.0f};
        return l;
    }
    // EN bunden kant: den andra är ± måttet, av samma sort.
    if (fjärr.ref == Ref::Fri) {
        if (när.ref == Ref::Början) {
            fjärr  = {Ref::Början, när.v + mått};
            l.luft = luftFält;
        } else if (när.ref == Ref::Slut) {
            fjärr = {Ref::Slut, när.v - mått};
        } else {
            fjärr = {Ref::Okänd, 0.0f};
        }
    } else if (när.ref == Ref::Fri) {
        if (fjärr.ref == Ref::Början)
            när = {Ref::Början, fjärr.v - mått};
        else if (fjärr.ref == Ref::Slut)
            när = {Ref::Slut, fjärr.v + mått};
        else
            när = {Ref::Okänd, 0.0f};
    }
    l.när   = när;
    l.fjärr = fjärr;
    return l;
}

// Närkanten ur ankarna: SYSKONANKARET vinner över förälderankaret på samma
// kant, precis som i `resolve`. `mål` är syskonets axel i passet — nullptr
// när syskonet inte finns där (dockat, utanför flödet), och kanten är då
// okänd. Mot ett mål från slutet räknas gapet INÅT: min vänsterkant ligger
// `gap` till höger om målets högerkant, alltså `gap` närmare förälderns slut.
Kant närkant(bool syskon, const Led* mål, float gap, bool förälder,
             float offset)
{
    if (syskon) {
        if (mål && mål->fjärr.ref == Ref::Början)
            return {Ref::Början, mål->fjärr.v + gap};
        if (mål && mål->fjärr.ref == Ref::Slut)
            return {Ref::Slut, mål->fjärr.v - gap};
        return {Ref::Okänd, 0.0f};
    }
    if (förälder)
        return {Ref::Början, offset};
    return {};
}

// Fjärrkanten, spegelvänt: min högerkant ligger `gap` till vänster om målets
// vänsterkant.
Kant fjärrkant(bool syskon, const Led* mål, float gap, bool förälder,
               float offset)
{
    if (syskon) {
        if (mål && mål->när.ref == Ref::Början)
            return {Ref::Början, mål->när.v - gap};
        if (mål && mål->när.ref == Ref::Slut)
            return {Ref::Slut, mål->när.v + gap};
        return {Ref::Okänd, 0.0f};
    }
    if (förälder)
        return {Ref::Slut, offset};
    return {};
}

// Bredden när BÅDA x-kanterna är bundna och kända — så kan höjden
// bero på den (`heightForWidth`). SPÄND: restbredden minus kanterna, mot
// förälderns högerkant ELLER ett syskon från slutet; ett FAST INTERVALL av
// samma sort: avståndet mellan kanterna. Annars ingen.
std::optional<float> kändBredd(Kant när, Kant fjärr, float restW)
{
    if (när.ref == Ref::Början && fjärr.ref == Ref::Slut)
        return std::max(0.0f, restW - när.v - fjärr.v);
    if (när.ref == Ref::Början && fjärr.ref == Ref::Början)
        return std::max(0.0f, fjärr.v - när.v);
    if (när.ref == Ref::Slut && fjärr.ref == Ref::Slut)
        return std::max(0.0f, när.v - fjärr.v);
    return std::nullopt;
}

// Barnets KLASS på axeln: båda kanterna från början, båda från slutet,
// eller SPÄND mellan dem. Ett barn med en okänd kant, ett omvänt par och MITT
// är ingen av de tre.
bool frånBörjan(const Led& l)
{
    return !l.mitt && l.när.ref == Ref::Början && l.fjärr.ref == Ref::Början;
}

bool frånSlutet(const Led& l)
{
    return !l.mitt && l.när.ref == Ref::Slut && l.fjärr.ref == Ref::Slut;
}

bool spänd(const Led& l)
{
    return l.när.ref == Ref::Början && l.fjärr.ref == Ref::Slut;
}

// STÅR DE ÖVER VARANDRA i höjdled? (a) det ena i flödet uppifrån och
// det andra i flödet nedifrån; (b) det enas fjärrkant och
// det andras närkant av samma sort och i ordning. Ett MITT-barn, ett omvänt
// eller okänt par och en spänd kant som inte är i ordning skiljs inte.
bool överVarandra(const Led& a, const Led& b)
{
    if (a.mitt || b.mitt)
        return false;
    const auto ovanför = [](const Led& ö, const Led& u) {
        if (frånBörjan(ö) && frånSlutet(u))
            return true;   // (a)
        if (ö.fjärr.ref == Ref::Början && u.när.ref == Ref::Början)
            return ö.fjärr.v <= u.när.v;   // (b) från början: f ≤ g
        if (ö.fjärr.ref == Ref::Slut && u.när.ref == Ref::Slut)
            return ö.fjärr.v >= u.när.v;   // (b) från slutet: d ≥ e
        return false;
    };
    return ovanför(a, b) || ovanför(b, a);
}

} // namespace

gfx::Sizef Widget::childrenExtent(const Ctx& c) const
{
    return extentOf(c, std::nullopt, {});
}

gfx::Sizef Widget::childrenExtent(const Ctx& c, float width) const
{
    return extentOf(c, width, {});
}

// KÄRNAN. Med en bredd tillkommer
// HÖJDEN FÖR BREDD: ett framåtpass ger varje dockad remsa den bredd
// dock-uppspelningen ger den (bredden minus Left/Right-remsorna FÖRE den), och
// de odockade bor i restbredden efter ALLA Left/Right-remsor.
gfx::Sizef Widget::extentOf(const Ctx& c, std::optional<float> width,
                            std::span<const Widget* const> skip) const
{
    gfx::Sizef need{0.0f, 0.0f};
    const auto skipped = [&](const Widget* ch) {
        return std::find(skip.begin(), skip.end(), ch) != skip.end();
    };
    // Höjden för en GIVEN bredd, när kroken svarar — annars måttet rakt av.
    const auto forWidth = [&](const Widget& ch, gfx::Sizef m,
                              std::optional<float> w) {
        if (w)
            if (const std::optional<float> h = ch.heightForWidth(c, *w))
                m.h = *h;
        return m;
    };

    // Remsans bredd PER BARNINDEX — samma urval som baklängespasset nedan, så
    // varje dockat barn där har sin post (ingen karta, ingen allokering per
    // barn i mätvägen).
    std::vector<float> stripW;
    float restW = 0.0f;   // de odockades
    if (width) {
        stripW.assign(children_.size(), 0.0f);
        float avail = std::max(0.0f, *width);
        for (std::size_t i = 0; i < children_.size(); ++i) {
            const Widget* ch = children_[i];
            if (!ch->visible_ || ch->dock == Dock::None ||
                !ch->inDockFlow() || skipped(ch))
                continue;
            stripW[i] = avail;
            if (ch->dock == Dock::Left || ch->dock == Dock::Right)
                avail = std::max(0.0f, avail - measure(*ch, c).w);
        }
        restW = avail;
    }

    // 1. ODOCKADE barn bor i "resten" — alltså i den yta som blir kvar
    // EFTER alla remsor.
    //
    // SYSKONANKARE MÅSTE FÖLJAS, annars är hjälparen ingen invers.
    // `b.anchorLeft = {"a", 8}` placerar b:s vänsterkant vid
    // a:s HÖGERKANT + 8 (`resolve`, grenen `sL`) — INTE vid `b.x`. Kedjan är
    // just den stiloberoende formen syskonankaret finns FÖR. Därför ett
    // FRAMÅTPASS med kanter: `earlierSibling` kan bara peka BAKÅT, så ett
    // barns ankarmål är alltid färdigräknat när vi når barnet.
    //
    // KANTERNA RÄKNAS ÅT BÅDA HÅLLEN: från förälderns
    // början ELLER från dess slut. En knapprad mot nederkanten har sin
    // närkant `bottom + höjd` från slutet — den behöver ingen fixpunkt, bara
    // att den står UNDER det som flödar uppifrån. Så bidrar den: ett barn i
    // flödet uppifrån med sin fjärrkant (plus luften), ett i flödet nedifrån
    // med sin närkant, ett SPÄNT med närkant + mätt mått + fjärrkant (måttet
    // är dess MINSTA — WPF:s desired size) och ett MITT med måttet.
    //
    // DOLDA barn följer med: deras kanter bär kedjan, för `resolve`
    // lämnar ett hål där de står, men bara de synliga bidrar nedan.
    struct Barn {
        const Widget* w;
        Led           x, y;
    };
    std::vector<Barn> barn;
    barn.reserve(children_.size());
    // Syskonankarets MÅL i passet — en linjär sökning, i UI-skala.
    const auto iPasset = [&barn](const Widget* s) -> const Barn* {
        for (const Barn& b : barn)
            if (b.w == s)
                return &b;
        return nullptr;
    };

    for (const Widget* ch : children_) {
        if (ch->dock != Dock::None || !ch->inDockFlow() || skipped(ch))
            continue;
        gfx::Sizef m = measure(*ch, c);

        // ---- X-axeln
        const Widget* sL = ch->earlierSibling(ch->anchorLeft.to);
        const Widget* sR = ch->earlierSibling(ch->anchorRight.to);
        const Barn*   mL = sL ? iPasset(sL) : nullptr;
        const Barn*   mR = sR ? iPasset(sR) : nullptr;
        const Kant    nx = närkant(sL != nullptr, mL ? &mL->x : nullptr,
                                   ch->anchorLeft.gap, ch->anchors.left, ch->x);
        const Kant    fx = fjärrkant(sR != nullptr, mR ? &mR->x : nullptr,
                                     ch->anchorRight.gap, ch->anchors.right,
                                     ch->right);
        // BÅDA x-kanterna kända med bredden GIVEN: barnets bredd är känd, så
        // dess höjd får bero på den (`resolve`s gren 2) — spänd mot
        // förälderns högerkant eller mot ett syskon från slutet, eller ett
        // fast intervall.
        if (width)
            if (const std::optional<float> pw = kändBredd(nx, fx, restW))
                m = forWidth(*ch, m, *pw);
        const Led lx = härled(nx, fx, m.w, ch->right);

        // ---- Y-axeln (spegelvänt)
        const Widget* sT = ch->earlierSibling(ch->anchorTop.to);
        const Widget* sB = ch->earlierSibling(ch->anchorBottom.to);
        const Barn*   mT = sT ? iPasset(sT) : nullptr;
        const Barn*   mB = sB ? iPasset(sB) : nullptr;
        const Kant    ny = närkant(sT != nullptr, mT ? &mT->y : nullptr,
                                   ch->anchorTop.gap, ch->anchors.top, ch->y);
        const Kant    fy = fjärrkant(sB != nullptr, mB ? &mB->y : nullptr,
                                     ch->anchorBottom.gap, ch->anchors.bottom,
                                     ch->bottom);
        const Led ly = härled(ny, fy, m.h, ch->bottom);

        barn.push_back(Barn{ch, lx, ly});
    }

    // BIDRAGEN, bara från de SYNLIGA. Höjden har två FLÖDEN:
    // allt mot nederkanten står UNDER allt som flödar uppifrån, så de läggs
    // ihop. I sidled läggs bara de par ihop som INTE står över varandra
    //: "Namn" uppe till vänster och "Hjälp" uppe till höger står i samma
    // rad, fälten uppifrån och OK nere till höger gör det inte.
    float uppifrån = 0.0f, nedifrån = 0.0f;
    std::vector<const Barn*> frånVänster, frånHöger;
    for (const Barn& b : barn) {
        if (!b.w->visible_)
            continue;
        if (b.x.mitt)
            need.w = std::max(need.w, b.x.mått);
        else if (spänd(b.x))
            need.w = std::max(need.w, b.x.när.v + b.x.mått + b.x.fjärr.v);
        else if (frånBörjan(b.x)) {
            need.w = std::max(need.w, b.x.fjärr.v + b.x.luft);
            frånVänster.push_back(&b);
        } else if (frånSlutet(b.x)) {
            need.w = std::max(need.w, b.x.när.v);
            frånHöger.push_back(&b);
        }

        if (b.y.mitt)
            need.h = std::max(need.h, b.y.mått);
        else if (spänd(b.y))
            need.h = std::max(need.h, b.y.när.v + b.y.mått + b.y.fjärr.v);
        else if (frånBörjan(b.y))
            uppifrån = std::max(uppifrån, b.y.fjärr.v + b.y.luft);
        else if (frånSlutet(b.y))
            nedifrån = std::max(nedifrån, b.y.när.v);
    }
    need.h = std::max(need.h, uppifrån + nedifrån);
    for (const Barn* p : frånVänster)
        for (const Barn* q : frånHöger)
            if (!överVarandra(p->y, q->y))
                need.w = std::max(need.w, p->x.fjärr.v + p->x.luft + q->x.när.v);

    // 2. DOCKADE barn BAKLÄNGES genom barnordningen. Docklayouten är
    // ordningsberoende — varje remsa skär den area som återstår efter de
    // TIDIGARE syskonen — så inversen måste gå åt andra hållet: sist
    // skuren remsa är den som ligger innerst, och den läggs på först.
    //
    // Varje remsa bär ett TVÄRKRAV (`max` på den andra axeln). Utan det
    // kollapsar kompositen: ett ensamt `Dock::Top 100×20` hade gett 0×20.
    for (std::size_t i = children_.size(); i-- > 0;) {
        const Widget* ch = children_[i];
        if (!ch->visible_ || ch->dock == Dock::None || !ch->inDockFlow() ||
            skipped(ch))
            continue;
        gfx::Sizef m = measure(*ch, c);
        // Med bredden given: Top/Bottom och Fill tar höjden för sin
        // remsbredd (`dockedSize`s fråga; Left/Right får höjden av ytan).
        if (width && ch->dock != Dock::Left && ch->dock != Dock::Right)
            m = forWidth(*ch, m, stripW[i]);
        switch (ch->dock) {
        case Dock::Top:
        case Dock::Bottom:
            need.h += m.h;
            need.w = std::max(need.w, m.w);
            break;
        case Dock::Left:
        case Dock::Right:
            need.w += m.w;
            need.h = std::max(need.h, m.h);
            break;
        default:
            // FILL — VID SIN POSITION i baklängespasset, aldrig samlat i
            // botten. `carveDock` säger att Fill tar resten UTAN att
            // förbruka den, så ett Fill FÖRE en remsa får hela den
            // då-aktuella arean och remsan efter överlappar det. Samlas de
            // i botten antas de ligga sist och måttet blir för stort.
            need.w = std::max(need.w, m.w);
            need.h = std::max(need.h, m.h);
            break;
        }
    }
    return need;
}

// Fönster-kopplingen (owner_ och dess livstecken) propageras genom hela
// delträdet — ett träd kan byggas fristående och adopteras in i ett fönster
// efteråt. Varje gång stegas ANKNYTNINGEN, också när fönstret är
// detsamma: en widget som lossats och satts in igen är i en ny kontext
// (`HandlerGuard`).
void Widget::setOwnerForTree(AppWindow* owner)
{
    owner_     = owner;
    ownerLife_ = windowLife(owner);
    ++attachGen_;
    for (Widget* ch : children_)
        ch->setOwnerForTree(owner);
}

// `languageChanged()` på mig och hela delträdet — de tre anropsställena
// (`App::languageChangedAll`, fönstrets visning, adoptionen ovan) går hit.
// Kroken får bara röra widgetens EGET tillstånd (kontraktet i widget.h),
// så trädet ändras inte under vandringen.
void Widget::languageChangedTree()
{
    languageChanged();
    for (Widget* ch : children_)
        ch->languageChangedTree();
}

// Adoptera ett ÄGT (del)träd: barnet läggs sist (= överst i z-ordning)
// och får fönster-kopplingen rekursivt. Ägandet hamnar i ownedChildren_.
Widget* Widget::adopt(std::unique_ptr<Widget> w)
{
    Widget* raw = w.get();
    ownedChildren_.push_back(std::move(w));
    return adopt(*raw);
}

// Adoptera LÅNAT: samma parent/owner/z-ordning som ägda barn,
// men trädet raderar aldrig widgeten — någon annan (typiskt fönster-
// klassen, som värde-medlem) äger den. Dubbeladoption = no-op + logg.
Widget* Widget::adopt(Widget& w)
{
    if (w.parent_) {
        gfx::log("Widget::adopt(%s): redan adopterad (ignoreras)",
                 w.name.c_str());
        return &w;
    }
    w.parent_ = this;
    w.setOwnerForTree(liveOwner());
    children_.push_back(&w);
    // Containern får veta EFTER länkningen — flikkontrollen döljer
    // en sida som inte är den valda. Före invalideringen, så att den ytan
    // redan har sin slutliga synlighet när den ritas om.
    childAdded(w);
    AppWindow* const f = liveOwner();
    if (f)
        invalidate();   // ny widget i ett levande fönster — rita om ytan
    // …och kommer ACCEPTKNAPPEN in i fönstret blir den eller en fokuserad
    // knapp standard — också i en
    // annan panel än den här, så fönstret ritar om dem.
    if (f)
        w.refreshDefaultButton();
    // Ett delträd som kommer IN i ett öppet fönster kan ha missat ett
    // språkbyte medan det stod utanför trädet (en flik som lossats och
    // satts tillbaka) — se `languageChanged()`. Ett fönster som inte är
    // öppet får hela sitt träd när det visas, så det behövs inte här.
    if (f && f->isOpen())
        w.languageChangedTree();
    return &w;
}

// ------------------------------------------------------- BORTTAGNINGEN
//
// Kontraktet, de fyra valideringskontrollerna och skälen står vid
// deklarationerna i widget.h. Här bor mekaniken, och den är delad i TVÅ
// funktioner med flit: `validateRelease` avgör OM operationen får ske och
// muterar INGENTING, `detach` gör hela mutationen. En nekad operation kan
// därför omöjligt lämna trädet halvvägs.

// Frågas REKURSIVT över hela målträdet, inte bara roten — en Dropdown kan
// sitta godtyckligt djupt i det som ska lossas. STATISK BASMEDLEM och inte
// en fri funktion: `hasOpenPopup()` är `protected`, precis samma skäl som
// gjorde `measure` och `carveSiblingDocks` till basmedlemmar.
bool Widget::subtreeHasOpenPopup(const Widget& w)
{
    if (w.hasOpenPopup())
        return true;
    for (const Widget* ch : w.children())
        if (subtreeHasOpenPopup(*ch))
            return true;
    return false;
}

// De fyra kontrollerna, i bindande ordning. `ägdKrävs` skiljer
// `releaseOwned` (kräver `ownedChildren_`) från `unadopt` (tar båda).
// `op` går bara in i loggraden.
bool Widget::validateRelease(const Widget& w, bool ägdKrävs,
                             const char* op) const
{
    // 1. DIREKT BARN?
    if (std::find(children_.begin(), children_.end(), &w) == children_.end()) {
        gfx::log("Widget::%s(%s): inte ett direkt barn till %s (nekas)", op,
                 w.name.c_str(), name.c_str());
        return false;
    }

    // 2. RÄTT ÄGANDEKATEGORI? `children_` blandar lånade och ägda — bara
    // `ownedChildren_` bär ägandet. Ett LÅNAT barn kan inte lämnas ut som
    // `unique_ptr`, och att lossa det ändå och returnera tomt vore den
    // farligaste formen: tom retur betyder normalt misslyckande.
    if (ägdKrävs && !ownsChild(w)) {
        gfx::log("Widget::%s(%s): barnet är LÅNAT, inte ägt — ägandet finns "
                 "inte att ge tillbaka (nekas, trädet är orört)", op,
                 w.name.c_str());
        return false;
    }

    // 3. STRUKTURELLT BARN? Kompositens egen vakt mot dinglande pekare.
    if (!canReleaseChild(w)) {
        gfx::log("Widget::%s(%s): %s nekar — barnet är dess egen inredning",
                 op, w.name.c_str(), name.c_str());
        return false;
    }
    // …och FÖNSTRETS inbyggda är samma kategori, bara en nivå upp:
    // `topBar_`/`menuBar`/`statusBar` läses ovillkorligt av
    // `chromeInsetH()`. Frågan går via ägaren eftersom fönstrets rot är en
    // vanlig `Panel` och inte en egen typ som kunde overrida ovan.
    if (const AppWindow* f = liveOwner(); f && f->isBuiltinMember(w)) {
        gfx::log("Widget::%s(%s): fönstrets INBYGGDA medlem (topBar/menuBar/"
                 "statusBar) kan inte tas bort (nekas)", op, w.name.c_str());
        return false;
    }

    // 4. ÖPPEN POPUP eller VÄRD FÖR EN MENY i delträdet? Nekandet ÄR hela
    // hanteringen: stängning är UPPSKJUTEN till pump-svansen, så anroparen
    // stänger först och låter pumpen gå ett varv.
    if (subtreeHasOpenPopup(w)) {
        gfx::log("Widget::%s(%s): delträdet har en ÖPPEN popup — stäng den "
                 "och låt pumpen gå ett varv först (nekas)", op,
                 w.name.c_str());
        return false;
    }
    if (menuPresentationHostedIn(w)) {
        gfx::log("Widget::%s(%s): delträdet är VÄRD för en öppen meny — "
                 "stäng den och låt pumpen gå ett varv först (nekas)", op,
                 w.name.c_str());
        return false;
    }
    return true;
}

bool Widget::ownsChild(const Widget& w) const
{
    for (const std::unique_ptr<Widget>& u : ownedChildren_)
        if (u.get() == &w)
            return true;
    return false;
}

// MUTATIONEN, och ORDNINGEN ÄR BINDANDE: fönsterstädningen körs FÖRE
// trädmutationen. `AppWindow::setFocus(nullptr)` anropar `ctxFor(*old)`,
// alltså en vandring uppåt genom förälderkedjan för att släcka careten —
// kedjan måste vara intakt när det sker.
void Widget::detach(Widget& w)
{
    AppWindow* const f = liveOwner();
    if (f)
        f->detachTree(w);

    children_.erase(std::remove(children_.begin(), children_.end(), &w),
                    children_.end());
    w.parent_ = nullptr;
    w.setOwnerForTree(nullptr);   // EFTER fönsterstädningen
    // Containern får veta — barnet är ute ur trädet men LEVER
    // fortfarande: ett ägt barn förstörs av `unadopt` först när vi är klara.
    childRemoved(w);
    if (f) {
        invalidate();   // ytan barnet låg på måste ritas om
        // PEKAREN: stod den över barnet
        // står den nu över något annat — EFTER mutationen, så att
        // hit-testen inte hittar barnet igen.
        f->refreshCursor();
    }
}

bool Widget::unadopt(Widget& w)
{
    if (!validateRelease(w, false, "unadopt"))
        return false;

    detach(w);

    // SYMMETRIN till `adopt`: ett ÄGT barn förstörs (det var `adopt`s
    // unique_ptr som tog ägandet), ett LÅNAT släpps bara — ägaren är
    // typiskt en värde-medlem i fönsterklassen och lever vidare.
    const auto it = std::find_if(ownedChildren_.begin(), ownedChildren_.end(),
                                 [&w](const std::unique_ptr<Widget>& u) {
                                     return u.get() == &w;
                                 });
    if (it != ownedChildren_.end())
        ownedChildren_.erase(it);   // destruerar delträdet
    return true;
}

// FLYTTEN — kontraktet står vid deklarationen i widget.h.
// Valideringen är `unadopt`s fyra kontroller plus målets, och ALLT prövas
// innan något muteras; sedan `detach` (fönsterstädningen i det gamla
// fönstret, sedan trädet) och en vanlig `adopt` i målet i samma
// ägandekategori.
bool Widget::transferChild(Widget& w, Widget& to)
{
    if (!validateRelease(w, false, "transferChild"))
        return false;
    bool cykel = false;
    for (const Widget* p = &to; p; p = p->parent_)
        cykel = cykel || p == &w;
    if (&to == this || cykel) {
        gfx::log("Widget::transferChild(%s): målet %s är %s (nekas, trädet "
                 "är orört)",
                 w.name.c_str(), to.name.c_str(),
                 &to == this ? "redan föräldern"
                             : "barnet självt eller i dess delträd — en cykel");
        return false;
    }
    const bool ägd = ownsChild(w);
    detach(w);
    if (!ägd) {
        to.adopt(w);
        return true;
    }
    const auto it = std::find_if(ownedChildren_.begin(), ownedChildren_.end(),
                                 [&w](const std::unique_ptr<Widget>& u) {
                                     return u.get() == &w;
                                 });
    std::unique_ptr<Widget> barn = std::move(*it);
    ownedChildren_.erase(it);
    to.adopt(std::move(barn));
    return true;
}

// RIVNINGSVÄGEN — kontraktet står vid deklarationen i widget.h.
std::unique_ptr<Widget> Widget::severChild(Widget* w, bool alive)
{
    children_.erase(std::remove(children_.begin(), children_.end(), w),
                    children_.end());
    std::unique_ptr<Widget> ut;
    const auto it = std::find_if(ownedChildren_.begin(), ownedChildren_.end(),
                                 [w](const std::unique_ptr<Widget>& u) {
                                     return u.get() == w;
                                 });
    if (it != ownedChildren_.end()) {
        ut = std::move(*it);
        ownedChildren_.erase(it);
    }
    if (alive) {
        w->parent_ = nullptr;
        w->owner_  = nullptr;
        w->ownerLife_.reset();
        ++w->attachGen_;
    }
    return ut;
}

std::unique_ptr<Widget> Widget::releaseOwned(Widget& w)
{
    if (!validateRelease(w, true, "releaseOwned"))
        return nullptr;

    detach(w);

    const auto it = std::find_if(ownedChildren_.begin(), ownedChildren_.end(),
                                 [&w](const std::unique_ptr<Widget>& u) {
                                     return u.get() == &w;
                                 });
    std::unique_ptr<Widget> ut = std::move(*it);
    ownedChildren_.erase(it);
    return ut;   // fristående träd: parent_ == nullptr, owner_ == nullptr
}

// Per-widget-damage: markera widgeten dirty och väck fönstret. Fristående
// widgets (GlazeBuilders preview, owner_ == nullptr) sätter bara flaggan —
// harmlöst, de deltar inte i någon damage-beräkning. Stämpeln stegas FÖRE
// ägarvakten: den är widgetens egen och gäller också utan fönster.
void Widget::invalidate()
{
    ++invalidations_;
    dirty_ = true;
    if (AppWindow* f = liveOwner())
        f->needsRedraw_ = true;
}

// DAMAGE-STÄMPELN — kontraktet står vid deklarationen i
// widget.h. Fönstrets del läses här och inte i headern, som inte får
// inkludera app_window.h (cirkulärt).
std::uint64_t Widget::damageStamp() const
{
    const AppWindow* f = liveOwner();
    return invalidations_ + (f ? f->fullInvalidations_ : 0);
}

// En synlighetsändring kan flytta fönstrets INNEHÅLLSYTA: dockade barn
// skär remsor, och ett dolt barn skär ingen. Statusraden är fallet som
// gör det viktigt — `statusBar.show()` gör innehållsytan 24 logiska px
// lägre, och en app som bett om en INNEHÅLLSSTORLEK (setContentSize)
// ska då få fönstret omräknat i stället för att tappa 24 px.
//
// Fönstret avgör själv om det spelar roll: utan begärd innehållsstorlek
// är anropet en ren no-op, och när chromet är oförändrat blir den
// resulterande klientstorleken identisk och `setSize` görs aldrig.
// Fristående widgets (GlazeBuilders preview) har ingen ägare = no-op.
void Widget::notifyContentInsets()
{
    if (AppWindow* f = liveOwner())
        f->contentInsetsChanged();
}

// SYNLIGHETEN PÅ SKÄRMEN — kontraktet står vid `shownChanged`
// i widget.h. Kroken får bara röra widgetens EGET tillstånd, så trädet
// ändras inte under vandringen.
void Widget::notifyShownChanged()
{
    shownChanged();
    for (Widget* ch : children_)
        ch->notifyShownChanged();
}

// En synlighetsändring flyttar SYSKONEN när widgeten deltar i förälderns
// flöde: en remsa vid en kant skär deras yta (`Dock::Fill` tar bara det
// som blir över och flyttar ingen), och en förälder som PLACERAR själv
// (`Bar`, `Splitter`, `DockHost`) räknar om alla barn. Syskonen är inte
// dirty, så per-widget-damage hade ritat om bara den här widgetens yta och
// lämnat syskonens gamla pixlar kvar. Föräldern ritas i stället om — dess
// yta omsluter barnen — och är föräldern fönstrets rot, som inte ingår i
// damage-vandringen, hela fönstret. Fristående widgets: no-op.
void Widget::relayoutSiblings()
{
    AppWindow* const f = liveOwner();
    if (!f || !parent_)
        return;
    // FÖRÄLDERNS EGNA MÅTT — kroken
    // före omritningsvillkoren nedan: ett barn som inte flyttar sina syskon
    // kan ändå ändra förälderns mått. Kontraktet står vid
    // `childLayoutChanged`.
    parent_->childLayoutChanged(*this);
    // CHROME-RÖTTERNA står UTANFÖR flödet men flyttar ändå
    // andra: namnlistens motvikt, menyradens rubriker och namnlistens
    // hårlinje läser programikonens plats, och programikonens plats läser
    // namnlistens synlighet. Hela fönstret ritas om.
    if (parent_ == &f->root_ && f->isChromeRoot(*this)) {
        f->invalidate();
        return;
    }
    const bool remsa = dock != Dock::None && dock != Dock::Fill;
    if (!remsa && !parent_->placesChildren())
        return;
    if (parent_ == &f->root_)
        f->invalidate();
    else
        parent_->invalidate();
}

// Finkornig damage: en godtycklig yta i stället för hela widgeten —
// caret-blinket invalidaterar t.ex. bara caret-recten. Recten hamnar i
// fönstrets extra-damage-lista och ritas om med klipp som vanliga
// damage-rects. Fristående widgets = no-op (som invalidate).
void Widget::invalidateRect(const gfx::Rectf& physRect)
{
    if (AppWindow* f = liveOwner()) {
        f->extraDamage_.push_back(physRect);
        f->needsRedraw_ = true;
    }
}

// Caret-blinkets omstart (synlig fas, ny period) — vidare till ägar-
// fönstret som äger timern. No-op utan ägare/fokus.
void Widget::restartCaretBlink()
{
    if (AppWindow* f = liveOwner())
        f->restartCaretTimer(this);
}

// LIVSTECKNET — lat: allokeras först när någon frågar, och dör med
// widgeten (medlemmen förstörs i `~Widget`), så varje utdelad `weak_ptr`
// går ut samtidigt.
std::weak_ptr<char> Widget::lifeToken()
{
    if (!liv_)
        liv_ = std::make_shared<char>(0);
    return liv_;
}

// ETT EVENT I PUMP-SVANSEN — kontraktet står i widget.h. Vakten tas
// NU och prövas FÖRE `fire` (widgeten kan ha dött, lossats eller förlorat
// sitt fönster sedan posten) och IGEN efter den: en handler får riva
// widgeten eller fönstret, och då rörs inte ens fönstret. En rivning har i så
// fall redan begärt sin egen omritning (`detachTree` skördar ytan).
void Widget::postEvent(std::function<void()> fire)
{
    AppWindow* const f = liveOwner();
    if (!f || !fire)
        return;
    const HandlerGuard   vakt(*this);
    std::function<void()> händelse = [this, vakt, fire = std::move(fire)] {
        if (!vakt.ok())
            return;
        fire();
        if (!vakt.ok())
            return;
        if (AppWindow* nu = liveOwner())
            nu->invalidate();
    };
    // Under `AppWindow::commitEdits()` hamnar händelsen i fönstrets
    // commit-kö och levereras innan den återvänder (eller i pumpens svans) —
    // en handler i kedjan som flyttade fokus får inte göra en commit
    // asynkron (kontraktet är synkront). Kontraktet i app_window.h.
    if (f->commitDepth_ > 0)
        f->commitQueue_.push_back(std::move(händelse));
    else
        glaze::app().post(std::move(händelse));
}

void Widget::noteHandlerRun()
{
    if (AppWindow* f = liveOwner())
        ++f->handlerRuns_;
}

// VAKTEN — kontraktet står i widget.h.
Widget::HandlerGuard::HandlerGuard(Widget& w)
    : widget_(&w), life_(w.lifeToken()), window_(windowLife(w.liveOwner())),
      hadWindow_(w.liveOwner() != nullptr), attachment_(w.attachGen_)
{
}

bool Widget::HandlerGuard::ok() const
{
    if (life_.expired())
        return false;   // död — läs inte ens anknytningen
    if (hadWindow_ && window_.expired())
        return false;   // fönstret dog; `owner_` dinglar
    return widget_->attachGen_ == attachment_;
}

std::weak_ptr<char> Widget::windowLife(AppWindow* w)
{
    return w ? w->lifeToken() : std::weak_ptr<char>{};
}

// Capture & grab routas till ägarfönstret (som äger bokföringen) — alla
// no-op-säkra för fristående widgets (GlazeBuilders preview).

void Widget::setPointerCapture()
{
    if (AppWindow* f = liveOwner())
        f->setPointerCapture(this);
}

void Widget::releasePointerCapture()
{
    if (AppWindow* f = liveOwner())
        f->releasePointerCapture(this);
}

bool Widget::takePointerCapture()
{
    AppWindow* f = liveOwner();
    return f && f->takePointerCapture(this);
}

// Capturen bor i FÖNSTRET (`capturedWidget_`) — en sanning, som
// `middlePressed`: fokusförlusten och OS:ets capture-förlust släcker svaret
// utan att någon behöver nolla en kopia här.
bool Widget::hasPointerCapture() const
{
    const AppWindow* f = liveOwner();
    return f && f->capturedWidget_ == this;
}

// APPENS MUSHANDLER — basens args: knappen, pekaren i
// widgetens LOGISKA px och fönstrets modifierare. Kontraktet står i widget.h.
bool Widget::fireMouse(const Handler<MouseArgs>& h, const Ctx& c,
                       std::optional<gfx::Pointf> pos, gfx::MouseButton b)
{
    MouseArgs e;
    e.button = b;
    if (pos) {
        const gfx::Rectf box = resolve(c);
        e.local = Point{Draw::localX(c, box, pos->x), Draw::localY(c, box, pos->y)};
    }
    if (const AppWindow* f = liveOwner())
        e.mods = f->modifierKeys();
    h(this, e);
    return e.handled;
}

// En handler som väntade sig en ÄTTLING till eventets args fick en annan typ
// — den körs inte, och det sägs EN gång per handler (`Handler`).
void detail::logHandlerArgs(const char* väntad, const char* fick)
{
    gfx::log("Handler: väntade sig args av typen %s men fick %s — handlern "
             "körs inte (sitter den på rätt sorts widget?)",
             väntad, fick);
}

void Widget::grabInput()
{
    if (AppWindow* f = liveOwner())
        f->pushGrab(this);
}

void Widget::releaseInput()
{
    if (AppWindow* f = liveOwner())
        f->removeGrab(this);
}

void Widget::focus()
{
    if (AppWindow* f = liveOwner())
        f->setFocus(this);   // privat i AppWindow — Widget är friend
}

// Mittentrycket bor i FÖNSTRET (`middleWidget_`) — en sanning, så
// fokusförlust och borttagning släcker svaret utan att någon behöver nolla
// en kopia här.
bool Widget::middlePressed() const
{
    const AppWindow* f = liveOwner();
    return f && f->middleWidget_ == this;
}

// Högertrycket likaså (`rightWidget_`) — headern får inte
// inkludera app_window.h, så svaret läses här.
bool Widget::rightPressed() const
{
    const AppWindow* f = liveOwner();
    return f && f->rightWidget_ == this;
}

// Hjulets två sorter — kontraktet står vid `mouseWheel` i widget.h.
float Widget::wheelScrollPx(const Ctx&, float rows, float stepPx) const
{
    const AppWindow* f       = liveOwner();
    const bool       precise = f && f->isPreciseWheel();
    return rows * (precise ? gfx::kPrecisePxPerRow : stepPx);
}

float Widget::wheelNotches(const Ctx& c, float rows) const
{
    const AppWindow* f       = liveOwner();
    const bool       precise = f && f->isPreciseWheel();
    return precise ? rows * gfx::kPrecisePxPerRow / c.style->wheelStep : rows;
}

Ctx Widget::behaviorCtx() const
{
    // Förutsätter ägarfönster (dokumenterat) — anropas ur beteendekod som
    // redan vet att widgeten sitter i ett träd.
    return liveOwner()->ctxFor(*this);
}

// STORLEKEN OCH PUNKTEN I FÖNSTRET — kontraktet
// står i widget.h. Samma `ctxFor` + `resolve` som ritningen och krokarna,
// men utan `behaviorCtx`s precondition: ett stängt fönster (eller ett utan
// font — `makeCtx` läser den) ger ett tomt svar. Roten resolvar till 0 × 0;
// dess yta är fönstrets innehållsyta, `c.area`.
std::optional<gfx::Rectf> Widget::windowBox(Ctx& c) const
{
    AppWindow* const f = liveOwner();
    if (!f || !f->isOpen() || !f->font_)
        return std::nullopt;
    c = f->ctxFor(*this);
    return this == &f->root_ ? c.area : resolve(c);
}

gfx::Sizef Widget::actualSize() const
{
    Ctx c{};
    const std::optional<gfx::Rectf> box = windowBox(c);
    if (!box)
        return gfx::Sizef{};
    return gfx::Sizef{box->w / c.scale, box->h / c.scale};
}

std::optional<gfx::Pointf> Widget::toWindow(Point local) const
{
    Ctx c{};
    const std::optional<gfx::Rectf> box = windowBox(c);
    if (!box)
        return std::nullopt;
    const gfx::Rectf p = Draw::project(
        c, *box, gfx::Rectf{static_cast<float>(local.x),
                            static_cast<float>(local.y), 0.0f, 0.0f});
    return gfx::Pointf{p.x, p.y};
}

std::optional<Point> Widget::fromWindow(gfx::Pointf window) const
{
    Ctx c{};
    const std::optional<gfx::Rectf> box = windowBox(c);
    if (!box)
        return std::nullopt;
    // `fireMouse`s formel — så `toWindow(e.local)` är trycket.
    return Point{Draw::localX(c, *box, window.x),
                 Draw::localY(c, *box, window.y)};
}

// Ytan som faktiskt ritas: resolve() + stilens overdraw-marginal (skugga,
// fokusring — logiska px × scale). Damage-beräkningen och "skär widgeten
// damage-recten?"-testet använder den här, aldrig resolve() rakt av.
gfx::Rectf Widget::paintBounds(const Ctx& c) const
{
    const gfx::Rectf r = resolve(c);
    const float      m = c.style->overdraw * c.scale;
    return gfx::Rectf{r.x - m, r.y - m, r.w + 2.0f * m, r.h + 2.0f * m};
}

namespace {

// Skär en dockad remsa ur area (fysiska px) och krymp den för nästa
// syskon — WinForms-semantiken: Left/Right/Top/Bottom tar en
// remsa i sin kant, Fill tar resten UTAN att förbruka den (flera Fill
// delar ytan; z-ordningen avgör vad som syns).
gfx::Rectf carveDock(gfx::Rectf& area, Dock d, const gfx::Sizef& sz)
{
    gfx::Rectf r = area;
    switch (d) {
    case Dock::Left:
        r.w = sz.w;
        area.x += sz.w;
        area.w -= sz.w;
        break;
    case Dock::Right:
        r.x = area.x + area.w - sz.w;
        r.w = sz.w;
        area.w -= sz.w;
        break;
    case Dock::Top:
        r.h = sz.h;
        area.y += sz.h;
        area.h -= sz.h;
        break;
    case Dock::Bottom:
        r.y = area.y + area.h - sz.h;
        r.h = sz.h;
        area.h -= sz.h;
        break;
    default:   // Fill — resten
        break;
    }
    if (area.w < 0.0f) area.w = 0.0f;
    if (area.h < 0.0f) area.h = 0.0f;
    return r;
}

// ENHETSBRYGGAN. `contentSize()` svarar i LOGISKA px;
// `c.area` och allt `resolve()` returnerar är FYSISKA. Det är
// HÄR, på konsumtionssidan, skalningen sker — och den står i en egen
// funktion så att de tre anropsställena nedan inte kan glida isär.
gfx::Sizef phys(const gfx::Sizef& logical, const Ctx& c)
{
    return gfx::Sizef{logical.w * c.scale, logical.h * c.scale};
}

// SYSKONANKARNAS MINNE. Resolve är tillståndslös och löser
// varje syskonankare REKURSIVT — och en widget ankrad både åt vänster
// (etiketten) och uppåt (raden ovanför) förgrenar sig i två riktningar per
// nivå: ett formulär med ~30 rader blir exponentiellt och fryser fönstret vid
// första omlayout. Minnet gäller BARA under det yttersta `resolve`-anropet
// (`ResolveScope`), så tillståndslösheten står kvar utåt: inget lever mellan
// två anrop, och ingenting kan ändras medan ett pågår (resolve är const).
// Varje syskon löses därmed högst EN gång per yttersta anrop — linjärt.
// Nyckeln är widgeten OCH ctx:t (adressen, ytan, skalan, stilen): en
// förälder som placerar sina barn kan resolva med ett eget Ctx. UI-tråden.
struct ResolveMemoEntry {
    const Ctx*   ctx;
    gfx::Rectf   area;
    float        scale;
    const Style* style;
    gfx::Rectf   rect;
};
int                                                       g_resolveDepth = 0;
std::unordered_map<const Widget*, ResolveMemoEntry>       g_resolveMemo;

struct ResolveScope {
    ResolveScope() { ++g_resolveDepth; }
    ~ResolveScope()
    {
        if (--g_resolveDepth == 0)
            g_resolveMemo.clear();
    }
    ResolveScope(const ResolveScope&)            = delete;
    ResolveScope& operator=(const ResolveScope&) = delete;
};

gfx::Rectf resolveSibling(const Widget& w, const Ctx& c)
{
    if (const auto it = g_resolveMemo.find(&w); it != g_resolveMemo.end()) {
        const ResolveMemoEntry& e = it->second;
        if (e.ctx == &c && e.area.x == c.area.x && e.area.y == c.area.y &&
            e.area.w == c.area.w && e.area.h == c.area.h &&
            e.scale == c.scale && e.style == c.style)
            return e.rect;
    }
    const gfx::Rectf r = w.resolve(c);
    g_resolveMemo[&w]  = ResolveMemoEntry{&c, c.area, c.scale, c.style, r};
    return r;
}

} // namespace

// EN DOCKAD REMSAS MÅTT — kontraktet står i widget.h. Utanför
// det anonyma namespacet (en medlem), men det läser `phys` därifrån: samma
// enhetsbrygga som allt annat i resolve. Bara Top/Bottom frågar kroken —
// för dem är bredden TVÄRaxeln och given av ytan, så höjden får bero på den
// utan att dockkontraktet bryts (skäraxeln är höjden, och den beror inte på
// `area.h`). Left/Right får höjden av ytan och har inget att fråga om.
gfx::Sizef Widget::dockedSize(const Widget& w, const gfx::Rectf& area,
                              const Ctx& c)
{
    gfx::Sizef s = phys(w.contentSize(c), c);
    if (w.dock == Dock::Top || w.dock == Dock::Bottom)
        if (const std::optional<float> h =
                w.heightForWidth(c, area.w / c.scale))
            s.h = *h * c.scale;
    return s;
}

// SYSKON-CARVINGEN. Ytan ett ICKE-dockat barn
// får: `c.area` minus alla SYNLIGA dockade syskons remsor.
//
// Kontraktet står i widget.h; det som är värt att se HÄR är att den
// återanvänder `carveDock` ovan rakt av — inklusive Fill-grenen (som inte
// förbrukar något) och negativ-klampningen. Det finns alltså exakt EN
// beskrivning av vad en remsa gör, och gren 1 nedan läser samma — också
// remsans MÅTT, `dockedSize`, med den yta som återstår när remsan skärs.
//
// `except` FÅR vara null (GlazeBuilder räknar koordinater innan noden
// finns). Widgets utanför dock-flödet (`TopBar`) hoppas över: de resolvas
// mot ett annat Ctx och hör inte till innehållslayouten.
gfx::Rectf Widget::carveSiblingDocks(const Widget& parent,
                                     const Widget* except, const Ctx& c)
{
    gfx::Rectf area = c.area;
    for (const Widget* sib : parent.children_) {
        if (sib == except || sib->dock == Dock::None || !sib->visible_ ||
            !sib->inDockFlow())
            continue;
        carveDock(area, sib->dock, dockedSize(*sib, area, c));
    }
    return area;
}

// SYSKONUPPSLAGET. Vandringen stannar vid `this`, precis
// som dock-carvingen ovan gör — och det är hela cykelskyddet: efter mig
// finns inget att hitta, så en framåtreferens (A→B där B ligger senare),
// en självreferens och ett okänt namn ger ALLA samma svar. Rekursionsdjupet
// är därmed högst mitt eget syskonindex.
// TVÅFASIGT med flit: kandidaten hålls tills `this` FAKTISKT hittats i
// listan. Den enkla formen (returnera direkt vid namnträff) vilar på att
// `this` finns bland förälderns barn — sant i dag, eftersom `adopt` sätter
// `parent_` och `children_` i samma andetag och det inte finns något
// borttags-API — men den vilar UTAN att kontrollera det, och då är
// terminerings"beviset" ett antagande. Bryts invarianten någon gång skulle
// den formen kunna svara med ett SENARE syskon, och två sådana kan peka på
// varandra: oändlig rekursion. Här kostar samma säkerhet en pekare.
const Widget* Widget::earlierSibling(const std::string& sname) const
{
    if (sname.empty() || !parent_)
        return nullptr;
    const Widget* träff = nullptr;
    for (const Widget* sib : parent_->children_) {
        if (sib == this)
            return träff;          // ENDA vägen ut med ett svar
        if (!träff && sib->name == sname)
            träff = sib;
    }
    return nullptr;                // `this` fanns inte — svara aldrig
}

gfx::Rectf Widget::resolve(const Ctx& c) const
{
    const ResolveScope scope;   // syskonankarnas minne

    // 0. En förälder som placerar sina barn själv (Bar-flexen) vinner över
    // allt — barnets dock/anchors ignoreras. Algoritmen bor i containern.
    // Frågan är PER BARN (`placesChild`): ritytan placerar bara
    // sina förankrade.
    if (parent_ && parent_->placesChild(*this))
        return parent_->placeChild(*this, c);

    // 1. DOCK vinner över allt: min remsa fås genom att spela upp
    // skärningen för de dockade syskonen FÖRE mig i barnordningen —
    // stateless (ingen cachad layout), O(n²) i syskon, försumbart i
    // UI-skala. Dolda syskon skär inget (de ritas/hit-testas inte heller).
    // Remsans mått är `dockedSize` — samma fråga som `carveSiblingDocks`
    // ställer, med den yta som återstår (en Top-etikett bryter mot bredden
    // efter Left-remsorna före den).
    if (dock != Dock::None) {
        gfx::Rectf area = c.area;
        if (parent_) {
            for (const Widget* sib : parent_->children_) {
                if (sib == this)
                    break;
                if (sib->dock != Dock::None && sib->visible_)
                    carveDock(area, sib->dock, dockedSize(*sib, area, c));
            }
        }
        return carveDock(area, dock, dockedSize(*this, area, c));
    }

    // MÄTNINGEN ÄR LOGISK, LAYOUTEN FYSISK — `phys` är bryggan (se
    // hjälparen ovan). `sz` är fysisk från och med här och möter `c.area`
    // i alla grenar nedan. Den står EFTER dock-grenen, som mäter genom
    // `dockedSize` — så varje remsa mäts exakt en gång per resolve.
    const gfx::Sizef sz = phys(contentSize(c), c);

    float px, py;
    float pw = sz.w, ph = sz.h;

    // DEN SKURNA AREAN: ett icke-dockat barn
    // bor i "resten" precis som `Dock::Fill`, alltså i förälderns area
    // minus alla synliga dockade syskons remsor. Skulle gren 2 läsa `c.area`
    // OSKUREN på alla sex ställena nedan, tar en Top-dockad menyrad plats i
    // fönstrets storleksräkning men INTE i syskonens layout.
    //
    // `a` ersätter `c.area` — men BARA här. Syskonankarna nedan resolvar
    // målet med SAMMA `c`, vilket är korrekt per konstruktion: skärningen
    // sker inuti varje widgets egen resolve, så målet räknar fram en
    // identisk `a` själv.
    const gfx::Rectf a = (parent_ && inDockFlow())
                             ? carveSiblingDocks(*parent_, this, c)
                             : c.area;

    // 2. KANTBINDNING per axel. En kant är BUNDEN
    // om den har ett SYSKONANKARE eller ett FÖRÄLDERANKARE; syskonet
    // vinner på den kant där båda finns. Fyrgrensstrukturen per axel
    // ger sträckningen gratis:
    //
    //     två bundna  = sträckning (bredd = högerkant − vänsterkant)
    //     en bunden   = position + mätt storlek
    //     ingen       = centrerad + offset
    //
    // Talen blir desamma för rena förälderankare: `R − L` med
    // `L = area.x + x·s` och `R = area.x + area.w − right·s` är samma sak
    // som `area.w − (x + right)·s`.
    //
    // REKURSIONEN är avsiktlig och stateless-kontraktet står orört: målet
    // resolvas med SAMMA Ctx, vilket är korrekt per konstruktion eftersom
    // syskon delar förälderns childArea (`drawTree` ger alla syskon samma
    // `cc`). Kostnaden är samma komplexitetsklass som dock-carvingens
    // syskonvandling ovan, och termineringen är gratis: `earlierSibling`
    // kan bara peka BAKÅT. Varje syskon löses högst EN gång per yttersta
    // anrop (`resolveSibling` — utan minnet vore det exponentiellt).
    const Widget* sL = earlierSibling(anchorLeft.to);
    const Widget* sR = earlierSibling(anchorRight.to);
    const Widget* sT = earlierSibling(anchorTop.to);
    const Widget* sB = earlierSibling(anchorBottom.to);

    float L = 0.0f, R = 0.0f;
    bool  bL = false, bR = false;
    if (sL) {
        const gfx::Rectf t = resolveSibling(*sL, c);   // min vänsterkant mot dess högra
        L  = t.x + t.w + anchorLeft.gap * c.scale;
        bL = true;
    } else if (anchors.left) {
        L  = a.x + x * c.scale;
        bL = true;
    }
    if (sR) {
        const gfx::Rectf t = resolveSibling(*sR, c);   // min högerkant mot dess vänstra
        R  = t.x - anchorRight.gap * c.scale;
        bR = true;
    } else if (anchors.right) {
        R  = a.x + a.w - right * c.scale;
        bR = true;
    }

    if (bL && bR) {
        px = L;
        pw = std::max(0.0f, R - L);
    } else if (bL) {
        px = L;
    } else if (bR) {
        px = R - sz.w;
    } else {
        // Inget ankare = centrerad + offset.
        px = a.x + (a.w - sz.w) / 2.0f + x * c.scale;
    }

    float T = 0.0f, B = 0.0f;
    bool  bT = false, bB = false;
    if (sT) {
        const gfx::Rectf t = resolveSibling(*sT, c);
        T  = t.y + t.h + anchorTop.gap * c.scale;
        bT = true;
    } else if (anchors.top) {
        T  = a.y + y * c.scale;
        bT = true;
    }
    if (sB) {
        const gfx::Rectf t = resolveSibling(*sB, c);
        B  = t.y - anchorBottom.gap * c.scale;
        bB = true;
    } else if (anchors.bottom) {
        B  = a.y + a.h - bottom * c.scale;
        bB = true;
    }

    // HÖJD FÖR BREDD: en widget som är STRÄCKT vågrätt men inte
    // lodrätt har nu fått sin bredd av layouten, och kroken får svara med
    // höjden för just den (en radbruten etikett: radernas antal). Svarar den
    // gäller höjden i alla tre lodräta grenar — som `ph` OCH i positionen, så
    // en bottenbunden etikett växer uppåt från sin kant.
    float h = sz.h;
    if (bL && bR && !(bT && bB))
        if (const std::optional<float> hw = heightForWidth(c, pw / c.scale))
            h = *hw * c.scale;

    if (bT && bB) {
        py = T;
        ph = std::max(0.0f, B - T);
    } else if (bT) {
        py = T;
        ph = h;
    } else if (bB) {
        py = B - h;
        ph = h;
    } else {
        py = a.y + (a.h - h) / 2.0f + y * c.scale;
        ph = h;
    }
    return gfx::Rectf{px, py, pw, ph};
}

} // namespace glaze
