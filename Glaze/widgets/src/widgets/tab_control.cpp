// TabControl — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna — inga tal och
// inga färger i den här filen. Geometrin bor i de privata `*Rect`-metoderna
// och `tabRects`, som ritningen, placeringen och musvägen alla läser: det man
// ser, det sidan placeras mot och det man kan trycka på kan därför inte glida
// isär. Sidorna (`TabPage`) ritar inte sin yta — den är flikkontrollens.
//
// SKRIVNA BESLUT (de långa motiveringarna står i tab_control.h):
//   sidorna är riktiga barn och fliktiteln deras `text` · flikkontrollen
//   placerar dem och äger deras synlighet · valet lagras som det sätts och
//   läses klampat · valet följer sidan vid en borttagning · stängknappen
//   fyrar bara ett event — appen tar bort sidan · ingen autostorlek ·
//   sidornas kollektionsvokabulär (`addPage`/`setPage`/`setPages`)
//   är trädets egna operationer under ett annat namn.

#include "glaze/widgets/tab_control.h"

#include <algorithm>
#include <cstdlib>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../menu_query.h"
#include "../prop_parse.h"

namespace glaze {

namespace {

// Håller widgeten, eller någon i dess delträd, tangentfokus?
bool hållerFokus(const Widget& w)
{
    if (w.focused())
        return true;
    for (const Widget* b : w.children())
        if (hållerFokus(*b))
            return true;
    return false;
}

// Ligger `w` någonstans under `förfader`?
bool liggerUnder(const Widget& w, const Widget& förfader)
{
    for (const Widget* p = w.parent(); p; p = p->parent())
        if (p == &förfader)
            return true;
    return false;
}

} // namespace

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("tabcontrol", TabControl)

// ---------------------------------------------------------------- sidorna

int TabControl::pageCount() const
{
    int n = 0;
    for (const Widget* b : children())
        if (dynamic_cast<const TabPage*>(b))
            ++n;
    return n;
}

TabPage* TabControl::page(int index) const
{
    if (index < 0)
        return nullptr;
    int i = 0;
    for (Widget* b : children())
        if (auto* p = dynamic_cast<TabPage*>(b)) {
            if (i == index)
                return p;
            ++i;
        }
    return nullptr;
}

int TabControl::pageIndex(const Widget& w) const
{
    int i = 0;
    for (const Widget* b : children())
        if (dynamic_cast<const TabPage*>(b)) {
            if (b == &w)
                return i;
            ++i;
        }
    return -1;
}

// ------------------------------------------- KOLLEKTIONSVOKABULÄREN
// Trädets egna operationer under kollektionens namn — `add<TabPage>` och
// `unadopt` — så valet och synligheten går samma väg som vid en `adopt`
// (`childAdded`/`childRemoved`).

TabPage& TabControl::addPage(TrText text)
{
    return *add<TabPage>(std::move(text));
}

void TabControl::setPage(int index, TrText text)
{
    TabPage* p = page(index);
    if (!p) {
        gfx::log("TabControl(%s): setPage(%d) utanför intervallet (%d sidor) "
                 "— ignoreras", name.c_str(), index, pageCount());
        return;
    }
    p->setText(std::move(text));   // invaliderar flikkontrollen (tab_page.h)
}

// ATOMISK: varje sida prövas FÖRE mutationen med borttagningens egna två
// frågor om popuper (widget.h — de andra två, direkt barn och flikkontrollens
// egen vakt, gäller per konstruktion: sidorna är barnen och flikkontrollen
// nekar inga). En sida som inte får lämna gör att ingenting händer.
void TabControl::setPages(std::vector<TrText> texts)
{
    std::vector<TabPage*> gamla;
    for (Widget* b : children())
        if (auto* p = dynamic_cast<TabPage*>(b))
            gamla.push_back(p);
    for (TabPage* p : gamla)
        if (subtreeHasOpenPopup(*p) || menuPresentationHostedIn(*p)) {
            gfx::log("TabControl(%s): setPages — sidan '%s' har en öppen popup "
                     "eller meny; stäng den och låt pumpen gå ett varv först "
                     "(ingenting ändrat)", name.c_str(), p->name.c_str());
            return;
        }
    // VALET är flikkontrollens eget tal och står kvar (det lagrade, inte det
    // klampade — `setSelectedIndex(1)` före `setPages` gäller efteråt, så
    // satsordningen inte avgör). Borttagningarna flyttar det; det skrivs
    // tillbaka när de nya sidorna finns.
    const int val = selectedIndex_;
    for (TabPage* p : gamla)
        unadopt(*p);   // ägd förstörs, lånad släpps
    for (TrText& t : texts)
        add<TabPage>(std::move(t));
    selectedIndex_ = val;
    syncPages(false);
    invalidate();
}

// ------------------------------------------------------------------ valet

int TabControl::selectedIndex() const
{
    const int n = pageCount();
    return n == 0 ? -1 : std::clamp(selectedIndex_, 0, n - 1);
}

TabPage* TabControl::selectedPage() const
{
    return page(selectedIndex());
}

// Talet LAGRAS som det är — också bortom sidorna, så att en
// `setSelectedIndex(1);` som står före trädbyggets `adopt` gäller när den
// andra sidan kommer. Programmatiskt: inget event.
void TabControl::setSelectedIndex(int index)
{
    if (index < 0) {
        gfx::log("TabControl(%s): setSelectedIndex(%d) — negativt index, "
                 "ignoreras", name.c_str(), index);
        return;
    }
    if (index == selectedIndex_)
        return;
    selectedIndex_ = index;
    syncPages(false);
    invalidate();
}

void TabControl::setClosable(bool on)
{
    if (closable_ == on)
        return;
    closable_ = on;
    invalidate();   // flikarna blir bredare eller smalare
}

bool TabControl::setProp(const std::string& key, const std::string& value)
{
    if (key == "selectedIndex") {
        setSelectedIndex(std::atoi(value.c_str()));   // oparsbart ⇒ 0
        return true;
    }
    if (key == "closable") {
        setClosable(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// SYNLIGHETEN — den valda sidan syns och alla andra barn göms. FOKUS i ett
// barn som göms flyttar FÖRST, så det aldrig står kvar i en widget som inte
// syns: till den nya sidans första tabbstopp när användaren bytte inifrån
// sidan (`intoPage`), annars till flikkontrollen. Tabbordningen frågas
// medan den nya sidan redan syns och den gamla ännu inte är gömd — den nya
// sidans kontroller står då i listan, och fokus flyttar exakt en gång.
void TabControl::syncPages(bool intoPage)
{
    TabPage* vald = selectedPage();
    if (vald)
        vald->show();
    bool fokusGöms = false;
    for (const Widget* b : children())
        if (b != vald && hållerFokus(*b))
            fokusGöms = true;
    if (fokusGöms) {
        Widget* mål = this;
        if (intoPage && vald && appWindow())
            for (Widget* w : appWindow()->focusOrder())
                if (liggerUnder(*w, *vald)) {
                    mål = w;
                    break;
                }
        mål->focus();
    }
    for (Widget* b : children())
        if (b != vald)
            b->hide();
    shown_ = vald;
}

// Ett barn som inte är en sida blir ingen flik (headern). Det GÖMS — annars
// hade ett tabbstopp utan yta stått i fokusordningen.
void TabControl::childAdded(Widget& child)
{
    if (!dynamic_cast<TabPage*>(&child))
        gfx::log("TabControl(%s): '%s' är ingen TabPage — blir ingen flik och "
                 "döljs", name.c_str(), child.name.c_str());
    glömMusen();
    syncPages(false);
}

// VALET FÖLJER SIDAN (headern): var den borttagna INTE den visade flyttar
// indexet till den visade sidans nya plats; var den det står indexet kvar,
// och den som glider in på platsen — eller den sista — visas. Barnet lämnar
// flikkontrollen SYNLIGT, så att det beter sig som vilken widget som helst
// där det hamnar härnäst. Ingen användarkod: trädet är mitt i en mutation.
void TabControl::childRemoved(Widget& child)
{
    child.show();
    if (&child != shown_ && shown_)
        selectedIndex_ = pageIndex(*shown_);
    glömMusen();
    syncPages(false);
}

// Musens flik-index gällde den GAMLA raden: efter en ny eller borttagen sida
// hade en tänd flik eller en tryckt stängknapp pekat på fel flik tills nästa
// rörelse. Nästa `mouseMove` tänder rätt igen.
void TabControl::glömMusen()
{
    hoverTab_   = -1;
    hoverClose_ = -1;
    pressClose_ = -1;
    pressStill_ = false;
}

// ---------------------------------------------------------------- geometrin

gfx::Rectf TabControl::stripRect(const Ctx& c, const gfx::Rectf& b) const
{
    return gfx::Rectf{b.x, b.y, b.w, std::min(c.style->tabHeaderH, b.h)};
}

gfx::Rectf TabControl::frameRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float h = std::min(c.style->tabHeaderH, b.h);
    return gfx::Rectf{b.x, b.y + h, b.w, b.h - h};
}

// Sidytan = visningsytan minus ramens indrag — samma indrag som en ramad
// Panel gör, och aldrig negativ.
gfx::Rectf TabControl::pageRect(const Ctx& c, const gfx::Rectf& b) const
{
    const gfx::Rectf r = Draw::inset(frameRect(c, b), c.style->panelInset);
    return gfx::Rectf{r.x, r.y, std::max(0.0f, r.w), std::max(0.0f, r.h)};
}

// FLIKARNA. En flik är så bred som rubriken plus luften (och stängknappens
// kvadrat), aldrig smalare än `tabMinW`. Ryms de inte krymper de LIKA LÅNGT
// — de längsta först, tills alla får plats eller når `tabMinW` (samma tak
// för alla, Chromes fördelning) — och räcker inte ens det förskjuts raden så
// att den VALDA fliken slutar vid radens högerkant. Tillståndslöst: allt
// räknas ur rubrikerna, valet och ytan vid varje fråga.
std::vector<gfx::Rectf> TabControl::tabRects(const Ctx& c,
                                             const gfx::Rectf& b) const
{
    const auto&      m = *c.style;
    const gfx::Rectf s = stripRect(c, b);
    const int        n = pageCount();
    std::vector<gfx::Rectf> ut;
    if (n == 0)
        return ut;

    // Stängknappens kvadrat ERSÄTTER högerluften, och minsta bredden växer
    // lika mycket — `tabMinW` är rubrikens flik, så en stängbar flik som
    // krympt till botten har lika mycket rum för text som en vanlig.
    const float knapp = closable_ ? s.h - m.tabPadX : 0.0f;
    const float minst = m.tabMinW + knapp;
    std::vector<float> bredd;
    bredd.reserve(std::size_t(n));
    for (int i = 0; i < n; ++i) {
        const float text = Draw::measure(c, page(i)->text()).w;
        bredd.push_back(std::max(minst, 2.0f * m.tabPadX + text + knapp));
    }

    // TAKET — vattenfyllning över de sorterade bredderna: den minsta bredd
    // `tak` där summan av min(bredd, tak) ryms.
    const float rum   = s.w - float(n) * m.tabGap;
    float       summa = 0.0f;
    for (float w : bredd)
        summa += w;
    if (summa > rum) {
        std::vector<float> sorterad = bredd;
        std::sort(sorterad.begin(), sorterad.end());
        float tak   = minst;
        float under = 0.0f;   // summan av de bredder som redan ryms
        for (std::size_t k = 0; k < sorterad.size(); ++k) {
            const float kvar = float(sorterad.size() - k);
            if (under + kvar * sorterad[k] >= rum) {
                tak = std::max(minst, (rum - under) / kvar);
                break;
            }
            under += sorterad[k];
        }
        for (float& w : bredd)
            w = std::min(w, tak);
    }

    float x = s.x + m.tabGap;
    for (int i = 0; i < n; ++i) {
        ut.push_back(gfx::Rectf{x, s.y, bredd[std::size_t(i)], s.h});
        x += bredd[std::size_t(i)] + m.tabGap;
    }

    // Den VALDA fliken syns alltid hel.
    const int sel = selectedIndex();
    const float höger = ut[std::size_t(sel)].x + ut[std::size_t(sel)].w;
    const float skjut = std::max(0.0f, höger - (s.x + s.w));
    for (gfx::Rectf& r : ut)
        r.x -= skjut;
    return ut;
}

// Stängknappen är en kvadrat lika hög som raden längst till höger i fliken,
// och plattan är den minus `tabCloseInset` (dockningsrubrikens form).
gfx::Rectf TabControl::closeRect(const Ctx& c, const gfx::Rectf& t) const
{
    return Draw::inset(gfx::Rectf{t.x + t.w - t.h, t.y, t.h, t.h},
                       c.style->tabCloseInset);
}

gfx::Rectf TabControl::titleRect(const Ctx& c, const gfx::Rectf& t) const
{
    const float pad   = c.style->tabPadX;
    const float höger = closable_ ? t.h : pad;
    return gfx::Rectf{t.x + pad, t.y, std::max(0.0f, t.w - pad - höger), t.h};
}

int TabControl::tabAt(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const float      lx = Draw::localX(c, r, mx);
    const float      ly = Draw::localY(c, r, my);
    if (!stripRect(c, b).contains(lx, ly))
        return -1;   // också en flik som förskjutits ut ur raden
    const std::vector<gfx::Rectf> tabs = tabRects(c, b);
    for (std::size_t i = 0; i < tabs.size(); ++i)
        if (tabs[i].contains(lx, ly))
            return int(i);
    return -1;
}

int TabControl::closeAt(const Ctx& c, float mx, float my) const
{
    if (!closable_)
        return -1;
    const int t = tabAt(c, mx, my);
    if (t < 0)
        return -1;
    const gfx::Rectf r = resolve(c);
    const gfx::Rectf k =
        closeRect(c, tabRects(c, Draw::bounds(c, r))[std::size_t(t)]);
    return k.contains(Draw::localX(c, r, mx), Draw::localY(c, r, my)) ? t : -1;
}

// ---------------------------------------------------------------- placeringen
//
// Bar-formen: `c.area` är flikkontrollens childArea — dess EGEN rekt, fysisk
// — och den valda sidan får sidytan projicerad ur den logiska geometrin. De
// gömda barnen får en tom rekt i hörnet.
gfx::Rectf TabControl::placeChild(const Widget& child, const Ctx& c) const
{
    if (&child != shown_)
        return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
    return Draw::project(c, c.area, pageRect(c, Draw::bounds(c, c.area)));
}

// Barnen klipps mot SIDYTAN — en sida som är större än ytan får aldrig rita
// över flikraden eller ramen.
gfx::Rectf TabControl::clipRect(const Ctx& c) const
{
    const gfx::Rectf r = resolve(c);
    return Draw::project(c, r, pageRect(c, Draw::bounds(c, r)));
}

gfx::Sizef TabControl::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // BESLUT: INGEN autostorlek — flikkontrollen är en
        // PLACERINGSYTA som Panel och delaren, och dimensioneras av layouten
        // (oftast `Dock::Fill` eller dubbelankare). Ur sidorna går den inte
        // att mäta heller: bara den valda syns, och en flikkontroll som
        // bytte storlek med fliken hade flyttat allt runt omkring.
        return gfx::Sizef{width, height};
    }
}

// ---------------------------------------------------------------- ritningen
//
// Det som är gemensamt: visningsytan fylls med `panel` och ramas in (sidan
// ritar ingen egen yta), rubrikerna kortas med ellips, och fokusringen ligger
// INNANFÖR den valda fliken — utanför hade den täckt grannarna. Stängknappen
// (`setClosable`) syns på den valda fliken och den pekaren står över, och
// följer verktygsknappens recept i varje familj, som dockningsrubrikens
// knappar gör.
//
//   SLATE         fliken är en FLIK: den valda är sidans yta som sticker
//                 upp — `panel` med hårlinjen `border` runt de övre hörnen,
//                 och ramens överkant är öppen under den. De andra är
//                 rubriker utan platta.
//   GLAZE         samma form med familjens BESTÄMDA kant (`buttonBorder`)
//                 runt flik och yta, och den tända fliken är en tänd
//                 Glaze-platta: `buttonHover`, laminerad, med kontur.
//   MINIMALISTIC  ingen flikform alls — flikarna är rubriker i en rad, den
//                 valda bär radens accentstreck (`rowMarkerW`) i underkanten
//                 och texten i `accent`, menyradens recept. Ytan är `panel`
//                 med en hel hårlinje runt om.
//
//   vila     rubriker `text`, ingen platta på de ovalda
//   hover    den ovalda fliken får `buttonHover` (Glaze laminerad, med kontur)
//   vald     se ovan
//   fokus    ring i `accent` innanför den valda fliken
//   inaktiv  all text `textDim`, ingen hover, kanterna i `border` och
//            Minimalistics streck i `textDim`
void TabControl::draw(const Ctx& c)
{
    const gfx::Rectf              b    = c.bounds();
    const gfx::Rectf              s    = stripRect(c, b);
    const gfx::Rectf              f    = frameRect(c, b);
    const auto&                   m    = *c.style;
    const auto&                   col  = c.theme->colors;
    const bool                    på   = isEnabled();
    const int                     sel  = selectedIndex();
    const std::vector<gfx::Rectf> tabs = tabRects(c, b);
    const int                     n    = int(tabs.size());
    const gfx::Rectf              vald = sel >= 0 ? tabs[std::size_t(sel)]
                                                  : gfx::Rectf{};
    // Ramens lucka är den valda flikens INSIDA — mellan dess sidlinjer, så
    // att de möter ramens överkant i SLUTNA hörn (en lucka över hela fliken
    // lämnar en pixel öppen i varje hörn). En flik som når ramens
    // hörn kapas av primitiven: dess sidlinje fortsätter rakt ned i ramens.
    const float linje  = Draw::penWidth(c, Pen{col.border, m.borderWidth});
    const float luckaX = vald.x + linje;
    const float luckaW = sel >= 0 ? vald.w - 2.0f * linje : 0.0f;

    auto tänd = [&](int i) {
        return på && hovered() && i == hoverTab_ && i != sel;
    };
    // FLIKFORMEN: rundade övre hörn och en ÖPPEN nederkant — en rundad rekt
    // som når en hel flikhöjd nedanför fliken, klippt vid flikens underkant,
    // så bågarna och nederkanten hamnar utanför klippet (kapselns teknik).
    // Klippet sträcker sig en flik-storlek åt sidorna och uppåt: måttet är
    // flikens eget, inget tal.
    auto flikKlipp = [&](const gfx::Rectf& t) {
        Draw::clip(c, {t.x - t.w, t.y - t.h, 3.0f * t.w, 2.0f * t.h});
    };
    auto flik = [&](const gfx::Rectf& t, gfx::Color fyll) {
        flikKlipp(t);
        Draw::rect(c, {t.x, t.y, t.w, 2.0f * t.h}, m.tabRadius, fyll);
        Draw::endClip(c);
    };
    auto flikRam = [&](const gfx::Rectf& t, gfx::Color kant) {
        flikKlipp(t);
        Draw::frame(c, {t.x, t.y, t.w, 2.0f * t.h}, m.tabRadius,
                    Pen{kant, m.borderWidth});
        Draw::endClip(c);
    };
    auto rubrik = [&](int i, gfx::Color fg) {
        Draw::textElided(c, titleRect(c, tabs[std::size_t(i)]),
                         page(i)->text(), fg, Align::Center);
    };
    // Stängknappen — plattan när den är tänd eller tryckt, därefter tecknet.
    auto stäng = [&](int i) {
        if (!closable_ || (i != sel && !(på && hovered() && i == hoverTab_)))
            return;
        const gfx::Rectf k     = closeRect(c, tabs[std::size_t(i)]);
        const bool       nere  = på && pressed() && i == pressClose_ && pressStill_;
        const bool       het   = på && hovered() && i == hoverClose_;
        const gfx::Color platta = nere ? col.buttonPress : col.buttonHover;
        if (nere || het) {
            switch (c.ui) {
            case UI::Glaze:
                Draw::rect(c, k, m.cornerRadius, platta);
                if (het && !nere)
                    Draw::laminate(c, k, m.cornerRadius, col.sheen,
                                   m.sheenLower, col.sheenHigh, m.sheenUpper);
                Draw::frame(c, k, m.cornerRadius,
                            Pen{col.buttonBorder, m.borderWidth});
                break;
            case UI::Minimalistic:
                Draw::rect(c, k, m.cornerRadius, platta,
                           Pen{nere ? col.accent : col.buttonBorder,
                               m.borderWidth});
                break;
            case UI::Slate:
            default:
                Draw::rect(c, k, m.cornerRadius, platta);
                break;
            }
        }
        const gfx::Color tecken = !på ? col.border : het ? col.text : col.textDim;
        const float      g      = m.tabCloseGlyph;
        Draw::cross(c, {k.x + (k.w - g) / 2.0f, k.y + (k.h - g) / 2.0f, g, g},
                    Pen{tecken, m.borderWidth});
    };

    switch (c.ui) {
    case UI::Minimalistic: {
        Draw::rect(c, f, 0.0f, col.panel,
                   Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        Draw::clip(c, s);
        for (int i = 0; i < n; ++i) {
            const gfx::Rectf& t = tabs[std::size_t(i)];
            if (tänd(i))
                Draw::rect(c, t, 0.0f, col.buttonHover);
            if (i == sel)
                Draw::rect(c, {t.x, t.y + t.h - m.rowMarkerW, t.w, m.rowMarkerW},
                           0.0f, på ? col.accent : col.textDim);
            rubrik(i, !på ? col.textDim : i == sel ? col.accent : col.text);
            stäng(i);
        }
        Draw::endClip(c);
        break;
    }

    case UI::Glaze: {
        const gfx::Color kant = på ? col.buttonBorder : col.border;
        Draw::rect(c, f, 0.0f, col.panel);
        Draw::frameGap(c, f, 0.0f, Pen{kant, m.borderWidth}, luckaX, luckaW);
        Draw::clip(c, s);
        for (int i = 0; i < n; ++i) {
            const gfx::Rectf& t = tabs[std::size_t(i)];
            if (i == sel) {
                flik(t, col.panel);
                flikRam(t, kant);
            } else if (tänd(i)) {
                flik(t, col.buttonHover);
                Draw::laminate(c, t, m.tabRadius, col.sheen, m.sheenLower,
                               col.sheenHigh, m.sheenUpper);
                flikRam(t, kant);
            }
            rubrik(i, på ? col.text : col.textDim);
            stäng(i);
        }
        Draw::endClip(c);
        break;
    }

    case UI::Slate:
    default:
        Draw::rect(c, f, 0.0f, col.panel);
        Draw::frameGap(c, f, 0.0f, Pen{col.border, m.borderWidth}, luckaX,
                       luckaW);
        Draw::clip(c, s);
        for (int i = 0; i < n; ++i) {
            const gfx::Rectf& t = tabs[std::size_t(i)];
            if (i == sel) {
                flik(t, col.panel);
                flikRam(t, col.border);
            } else if (tänd(i)) {
                flik(t, col.buttonHover);
            }
            rubrik(i, på ? col.text : col.textDim);
            stäng(i);
        }
        Draw::endClip(c);
        break;
    }

    if (focused() && på && sel >= 0)
        Draw::frame(c, Draw::inset(vald, m.focusOffset),
                    std::max(0.0f, m.tabRadius - m.focusOffset),
                    Pen{col.accent, m.focusWidth});
}

// ---------------------------------------------------------------- beteendet

// Nästa eller föregående flik RUNT OM — Ctrl+Tab går från den sista till den
// första. Med en enda sida är det samma sida.
int TabControl::cycled(int dir) const
{
    const int n = pageCount();
    return n == 0 ? -1 : ((selectedIndex() + dir) % n + n) % n;
}

// ANVÄNDARVÄGEN. Ordningen är DataGridens: tillståndet först (valet,
// synligheten, fokus), omritningen, och handlern SIST — en handler får göra
// vad som helst, även ta bort sidan den just fick.
bool TabControl::selectFromUser(int index, bool intoPage)
{
    if (index < 0 || index >= pageCount() || index == selectedIndex())
        return false;
    selectedIndex_ = index;
    syncPages(intoPage);
    invalidate();
    if (onChange) {
        ChangeArgs e;
        onChange(this, e);   // den nya fliken läses ur sender
    }
    return true;
}

// FLIKKONTROLLEN HAR FOKUS: pilarna stannar i kanten, Ctrl-varianterna går
// runt om (WinForms). Alt är menyernas och appens.
bool TabControl::keyDown(gfx::Key k, std::uint32_t mods)
{
    const int n = pageCount();
    if (n == 0 || (mods & gfx::kModAlt))
        return false;
    const bool ctrl  = (mods & gfx::kModCtrl) != 0;
    const bool shift = (mods & gfx::kModShift) != 0;
    const int  sel   = selectedIndex();
    int        mål   = -1;
    switch (k) {
    case gfx::Key::Tab:
        if (!ctrl)
            return false;   // en vanlig Tab är fokusvandringens
        mål = cycled(shift ? -1 : 1);
        break;
    case gfx::Key::PageDown:
    case gfx::Key::PageUp:
        if (!ctrl)
            return false;
        mål = cycled(k == gfx::Key::PageDown ? 1 : -1);
        break;
    case gfx::Key::Left:  mål = std::max(sel - 1, 0); break;
    case gfx::Key::Right: mål = std::min(sel + 1, n - 1); break;
    case gfx::Key::Home:  mål = 0; break;
    case gfx::Key::End:   mål = n - 1; break;
    default:
        return false;
    }
    selectFromUser(mål, false);
    return true;   // också i kanten — tangenten är flikkontrollens
}

// FOKUS STÅR I SIDAN (en tangent den fokuserade widgeten tackade nej till):
// bara Ctrl-varianterna, och fokus följer med till den nya sidan.
bool TabControl::descendantKeyDown(gfx::Key k, std::uint32_t mods)
{
    if (pageCount() == 0 || (mods & gfx::kModAlt) || !(mods & gfx::kModCtrl))
        return false;
    const bool shift = (mods & gfx::kModShift) != 0;
    int        mål   = -1;
    if (k == gfx::Key::Tab)
        mål = cycled(shift ? -1 : 1);
    else if (k == gfx::Key::PageDown)
        mål = cycled(1);
    else if (k == gfx::Key::PageUp)
        mål = cycled(-1);
    else
        return false;
    selectFromUser(mål, true);
    return true;
}

// TRYCKET — på en stängknapp börjar det en knapptryckning (fliken väljs
// inte), på en flik väljer det fliken direkt (Windows flikar väljer vid
// trycket, inte vid släppet). AppWindow har redan gett flikkontrollen fokus.
void TabControl::mouseDown(const Ctx& c, float mx, float my)
{
    pressClose_ = closeAt(c, mx, my);
    pressStill_ = pressClose_ >= 0;
    if (pressClose_ >= 0) {
        invalidate();
        return;
    }
    const int t = tabAt(c, mx, my);
    if (t >= 0)
        selectFromUser(t, false);
}

void TabControl::mouseDrag(const Ctx& c, float mx, float my)
{
    if (pressClose_ < 0)
        return;
    const bool kvar = closeAt(c, mx, my) == pressClose_;
    if (kvar == pressStill_)
        return;   // regel 1: ingen ändring, ingen omritning
    pressStill_ = kvar;
    invalidate();
}

// SLÄPPET — stängknappens klick, om trycket släpps över SAMMA knapp (dra bort
// för att ångra). Fliken tas inte bort här: appen gör det i handlern.
bool TabControl::mouseUp(const Ctx& c, float mx, float my)
{
    const int tryckt = pressClose_;
    pressClose_      = -1;
    pressStill_      = false;
    if (tryckt < 0)
        return false;
    invalidate();
    if (!isEnabled() || closeAt(c, mx, my) != tryckt || !onTabClose)
        return false;
    TabCloseArgs e;
    e.index = tryckt;
    onTabClose(this, e);
    return true;   // en handler körde — AppWindow ritar om allt
}

// Hovern tänder en flik och en stängknapp, och ritar om bara flikraden (regel
// 1 — en rörelse som inte byter flik ritar ingenting).
void TabControl::mouseMove(const Ctx& c, float mx, float my)
{
    const int t = tabAt(c, mx, my);
    const int k = closeAt(c, mx, my);
    if (t == hoverTab_ && k == hoverClose_)
        return;
    hoverTab_   = t;
    hoverClose_ = k;
    const gfx::Rectf r = resolve(c);
    invalidateRect(Draw::project(c, r, stripRect(c, Draw::bounds(c, r))));
}

} // namespace glaze
