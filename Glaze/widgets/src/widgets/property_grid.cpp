// PropertyGrid — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. Cellgeometrin
// bor HÄR i LOGISKA px (rowBounds/valueBounds) — EN sanning som
// ritningen läser rakt av och som cellträffen/editor-placeringen
// projicerar till fysiska px (Draw::project). Basen Panel ger klipp,
// hjulscroll, overlay-listerna och innehållsytan; contentExtent överrids
// så radhöjden (data, inte barn) styr scrollintervallet. Radens slag
// (text, kryss, färg, tal, rubrik) bestämmer cellens innehåll (`drawCell`)
// och vilken editor som öppnas (`openEditor`); beslutens skäl står i headern.

#include "glaze/widgets/property_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include <gfx/platform.h>

#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/color_picker.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/textinput.h"

#include "../color_panel.h"
#include "../number_text.h"
#include "../prop_parse.h"

namespace glaze {

// ------------------------------------------------------------- editorn
//
// En riktig `TextInput` med tre tillägg och inget annat — datagridens
// `TextEditor`-form. En tangent går till den FOKUSERADE widgeten och
// bubblar aldrig vidare, så utan subklassen hade Enter och Escape aldrig nått
// gridden. Enter SPARAR och Escape AVBRYTER — båda konsumeras: de har gjort
// sitt jobb, och en Enter som därefter nådde menyernas acceleratorer hade gjort
// två saker. Med Ctrl/Cmd/Alt är tangenten någon annans genväg och går till
// fältet. Fokusförlusten sparar (postat). Ett TALFÄLT släpper bara in
// taltecken. Allt annat — caret, markering, klippbord — ärvs orört.
//
// Varför inte fältets eget `onCommit`: dess fokusförlust POSTAS, och mellan
// posten och pump-svansen kan gridden redan ha öppnat editorn på en annan
// rad — eventet hade då sparat den nya radens text i fel rad. Gridden måste
// veta direkt, i `focusLost`, att redigeringen tog slut.
class PropertyGrid::TextEditor final : public TextInput {
public:
    explicit TextEditor(PropertyGrid& owner) : owner_(owner) {}

    // En NY RAD börjar: värdet och en TOM ångra-historik (datagridens
    // `TextEditor::börja`). Editorn återanvänds,
    // och `setText`s likhetsvakt hade bevarat förra radens steg när nästa
    // rad råkar ha samma värde. `tal` = en talrad: bara taltecken tas emot.
    void börja(const std::string& text, bool tal)
    {
        numeric_ = tal;
        setText(text);
        resetHistory();
    }

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        const bool genväg =
            (mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModAlt)) != 0;
        if (!genväg && k == gfx::Key::Return) {
            owner_.endEdit(true, false);
            return true;
        }
        if (!genväg && k == gfx::Key::Escape) {
            owner_.endEdit(false, false);
            return true;
        }
        return TextInput::keyDown(k, mods);
    }

    // Ett tecken som inte ingår i ett tal sväljs redan vid tangenttrycket; en
    // inklistring prövas i stället av tolkningen när raden sparas.
    bool textInput(const char* utf8) override
    {
        if (!numeric_)
            return TextInput::textInput(utf8);
        std::string ok;
        for (const char* p = utf8; *p; ++p)
            if (numbertext::isNumberChar(*p))
                ok.push_back(*p);
        if (ok.empty())
            return true;
        return TextInput::textInput(ok.c_str());
    }

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.endEdit(true, true);
    }

private:
    PropertyGrid& owner_;
    bool          numeric_ = false;
};

// ------------------------------------------------------------- vallistan
//
// En riktig `Dropdown` med ett tillägg — datagridens `ChoiceEditor`-form
// En STÄNGD dropdown släpper
// Enter och Escape till fönstrets standard- och avbrytknapp, men i
// gridden är vallistan en REDIGERING: de två tangenterna avslutar den, öppen
// eller stängd, i ETT steg och konsumerade — annars klickade Enter efter ett
// Escape i listan dialogens OK mitt i en cell, och nästa Escape dess Avbryt.
// Enter i en ÖPPEN lista väljer först (valet SPARAR, som ett klick —
// `onChange` i konstruktorn); Escape väljer inget, och det redan sparade står
// kvar. Space och pilarna är dropdownens, och med Ctrl/Cmd/Alt är tangenten
// någon annans genväg.
class PropertyGrid::ChoiceEditor final : public Dropdown {
public:
    explicit ChoiceEditor(PropertyGrid& owner) : owner_(owner) {}

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        if ((mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModAlt)) != 0)
            return Dropdown::keyDown(k, mods);
        switch (k) {
        case gfx::Key::Escape:
            owner_.hideEditors();
            return true;
        case gfx::Key::Return:
            if (isOpen()) {
                // Valets `onChange` får riva eller flytta gridden — då rör vi
                // ingenting mer.
                const HandlerGuard vakt(*this);
                Dropdown::keyDown(k, mods);
                if (!vakt.ok())
                    return true;
            }
            owner_.hideEditors();
            return true;
        default:
            return Dropdown::keyDown(k, mods);
        }
    }

private:
    PropertyGrid& owner_;
};

// ------------------------------------------------------------- kryssrutan
//
// En fokuserbar, genomskinlig barn-widget över värdecellen: kryssrutan ritas
// av gridens egen cell, och editorn bär bara FOKUSEN — ringen runt rutan och
// tangenterna. Space växlar (och fyrar, som ett tryck), Enter och Escape
// stänger och konsumeras — samma regel som vallistan, så dialogens standard-
// och avbrytknapp aldrig klickas mitt i en cell. Ett tryck på editorn växlar
// likaså: den täcker värdecellen och tar trycket före gridden. Tangenterna
// är bara ens om redigeringen pågår: en dold editor som behållit fokus
// släpper dem vidare.
class PropertyGrid::CheckEditor final : public Widget {
public:
    explicit CheckEditor(PropertyGrid& owner) : owner_(owner) {}

    bool focusable() const override { return true; }

protected:
    void draw(const Ctx& c) override
    {
        if (!focused())
            return;
        const auto&      m   = *c.style;
        const gfx::Rectf box = PropertyGrid::checkBoxRect(c, c.bounds());
        Draw::frame(c, Draw::inset(box, -m.focusOffset),
                    m.checkRadius + m.focusOffset,
                    Pen{c.theme->colors.accent, m.focusWidth});
    }

    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        if (!isVisible() || (mods & (gfx::kModCtrl | gfx::kModCmd |
                                     gfx::kModAlt)) != 0)
            return false;
        switch (k) {
        case gfx::Key::Space:
            owner_.toggleActive();
            return true;
        case gfx::Key::Return:
        case gfx::Key::Escape:
            owner_.hideEditors();
            return true;
        default:
            return false;
        }
    }

    void mouseDown(const Ctx&, float, float) override
    {
        owner_.toggleActive();
    }

    void focusLost() override
    {
        if (isVisible())
            owner_.hideEditors();
    }

private:
    PropertyGrid& owner_;
};

// ------------------------------------------------------------- färgen
//
// En riktig `ColorPicker` över värdecellen, med popupen öppen direkt. Enter och
// Escape avslutar redigeringen när popupen är stängd (med öppen popup tar
// popupen tangenterna själv) och konsumeras; fokusförlusten stänger den.
// Gridden lyssnar bara på `onCommit`. En dold editor som behållit fokus
// öppnar ingen popup på Space.
class PropertyGrid::ColorEditor final : public ColorPicker {
public:
    explicit ColorEditor(PropertyGrid& owner) : owner_(owner) {}

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        if (!isVisible())
            return false;
        const bool genväg =
            (mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModAlt)) != 0;
        if (!genväg && !isOpen() &&
            (k == gfx::Key::Return || k == gfx::Key::Escape)) {
            owner_.hideEditors();
            return true;
        }
        return ColorPicker::keyDown(k, mods);
    }

    void focusLost() override
    {
        ColorPicker::focusLost();
        if (isVisible())
            owner_.hideEditors();
    }

private:
    PropertyGrid& owner_;
};

GLAZE_REGISTER_WIDGET("propertygrid", PropertyGrid)

PropertyGrid::PropertyGrid() : sep_(tr(str::DecimalSeparator))
{
    setClip(true);        // rader och editorer klipps mot innehållsytan

    // Fritexten committar vid Enter, klick och fokusförlust (`TextEditor`,
    // `endEdit`) — fältets `onChange` lämnas osatt: att skriva är inte att
    // spara.
    editor_ = add<TextEditor>(*this);
    editor_->hide();

    // Vallistan: ett VAL är en sparning — och därmed också en
    // commit. Programmatisk sättning av listan (`openEditor`) fyrar inte
    // dropdownens onChange, så ingen eko-loop. Enter och Escape avslutar den
    // (`ChoiceEditor`).
    picker_ = add<ChoiceEditor>(*this);
    picker_->hide();
    picker_->onChange = [this](Widget*, ChangeArgs&) {
        if (active_ < 0)
            return;
        rows_[std::size_t(active_)].value = picker_->selectedText();
        invalidate();   // raden ritas om med nya värdet
        fireChange(active_);
    };

    checkEd_ = add<CheckEditor>(*this);
    checkEd_->hide();

    // Färgen: bara commiten skrivs till raden — reglagens live-händelser rör
    // inte värdet (`commitColor`).
    colorEd_ = add<ColorEditor>(*this);
    colorEd_->hide();
    colorEd_->onCommit = [this](Widget*, ChangeArgs&) { commitColor(); };
}

// ------------------------------------------------------------- raderna

// En INAKTIV grid visas infälld: varje rubrik står stängd (radernas
// `expanded` rörs inte, så de öppnas igen som de var när gridden aktiveras),
// och bara rubrikerna — och raderna före den första — syns.
void PropertyGrid::rebuildVisible()
{
    pos_.assign(rows_.size(), -1);
    visibleCount_ = 0;
    bool öppen    = true;   // raderna före den första rubriken syns alltid
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        if (r.kind == Kind::Header) {
            pos_[i] = visibleCount_++;
            öppen   = r.expanded && isEnabled();
        } else if (öppen) {
            pos_[i] = visibleCount_++;
        }
    }
}

// AKTIVERINGEN BYTTE: en pågående redigering stängs utan att spara (en
// inaktiv grid redigeras inte), och de synliga raderna räknas om — infällt
// som inaktiv, som raderna säger som aktiv.
void PropertyGrid::enabledChanged()
{
    hideEditors();
    rebuildVisible();
    invalidate();
}

void PropertyGrid::setRows(std::vector<Row> rows)
{
    ++rowsGen_;      // ett postat event beskriver de gamla raderna
    hideEditors();   // AVBRYTER — redigeringen gällde de gamla raderna
    rows_ = std::move(rows);
    rowGen_.assign(rows_.size(), 0u);
    pending_.assign(rows_.size(), std::nullopt);
    changed_ = -1;
    rebuildVisible();
    invalidate();
}

// STYCKEFORMEN — kontraktet och skälet står i headern.
// Ingen `hideEditors()`: att lägga till en rad SIST rör inte den rad som
// redigeras, och att stänga editorn hade varit en tillståndsförlust som
// inte hör till operationen.
void PropertyGrid::addRow(Row row)
{
    rows_.push_back(std::move(row));
    rowGen_.push_back(0u);
    pending_.push_back(std::nullopt);
    rebuildVisible();
    invalidate();
}

void PropertyGrid::setRow(int index, Row row)
{
    if (!valid(index)) {
        gfx::log("PropertyGrid(%s): setRow(%d) utanför intervallet (%zu "
                 "rader) — ignoreras", this->name.c_str(), index,
                 rows_.size());
        return;
    }
    // Skriver vi under en ÖPPEN editor pekar den på en rad som inte finns
    // längre i samma form — stäng den UTAN att spara, som `setRows` gör. Och
    // en KÖAD sparning av raden (fokusförlustens postade events) beskriver
    // den gamla raden: radens generation räknas upp, så den slopas.
    if (active_ == index)
        hideEditors();
    rows_[std::size_t(index)] = std::move(row);   // tilldelning, inte uppdatering
    ++rowGen_[std::size_t(index)];
    pending_[std::size_t(index)].reset();
    rebuildVisible();
    invalidate();
}

void PropertyGrid::setRowEnabled(int row, bool on)
{
    if (!valid(row)) {
        gfx::log("PropertyGrid(%s): setRowEnabled(%d) utanför intervallet (%zu "
                 "rader) — ignoreras", this->name.c_str(), row, rows_.size());
        return;
    }
    Row& r = rows_[std::size_t(row)];
    if (r.enabled == on)
        return;
    r.enabled = on;
    if (!on) {
        // Raden som redigeras stängs utan att spara, och en köad sparning av
        // den beskriver en rad som inte längre går att ändra.
        if (active_ == row)
            hideEditors();
        ++rowGen_[std::size_t(row)];
    }
    invalidate();
}

void PropertyGrid::setExpanded(int row, bool on)
{
    if (!valid(row)) {
        gfx::log("PropertyGrid(%s): setExpanded(%d) utanför intervallet (%zu "
                 "rader) — ignoreras", this->name.c_str(), row, rows_.size());
        return;
    }
    Row& r = rows_[std::size_t(row)];
    if (r.kind != Kind::Header) {
        gfx::log("PropertyGrid(%s): setExpanded(%d) på en rad som inte är en "
                 "rubrik — ignoreras", this->name.c_str(), row);
        return;
    }
    if (r.expanded == on)
        return;
    r.expanded = on;
    rebuildVisible();
    // Raden som redigeras kan ha blivit dold: då stängs redigeringen UTAN att
    // spara — den rad användaren skrev i syns inte längre.
    if (active_ >= 0 && pos_[std::size_t(active_)] < 0)
        hideEditors();
    invalidate();
}

// LÄSVÄGARNA — ur `value`, aldrig ur editorernas tillstånd.
bool PropertyGrid::isChecked(int row) const
{
    return valid(row) && rows_[std::size_t(row)].kind == Kind::Check &&
           propparse::parseBool(rows_[std::size_t(row)].value);
}

std::optional<gfx::Color> PropertyGrid::colorValue(int row) const
{
    if (!valid(row) || rows_[std::size_t(row)].kind != Kind::Color)
        return std::nullopt;
    const Row& r = rows_[std::size_t(row)];
    gfx::Color c{};
    if (!propparse::parseHexColor(numbertext::trim(r.value), c, true))
        return std::nullopt;
    if (!r.allowAlpha)
        c = colorpanel::opak(c);   // färgväljarens läsregel
    return c;
}

std::optional<double> PropertyGrid::numberValue(int row) const
{
    if (!valid(row) || rows_[std::size_t(row)].kind != Kind::Number)
        return std::nullopt;
    const Row& r = rows_[std::size_t(row)];
    double     v = 0.0;
    if (!numbertext::parseUser(r.value, v))
        return std::nullopt;
    return numbertext::effective(v, r.min, r.max, r.decimals);
}

bool PropertyGrid::hasError(int row) const
{
    if (!valid(row))
        return false;
    switch (rows_[std::size_t(row)].kind) {
    case Kind::Number:
        return pending_[std::size_t(row)].has_value() || !numberValue(row);
    case Kind::Color:
        return !colorValue(row);
    default:
        return false;
    }
}

std::string PropertyGrid::numberDisplay(int row) const
{
    if (pending_[std::size_t(row)])
        return *pending_[std::size_t(row)];
    const Row& r = rows_[std::size_t(row)];
    if (const std::optional<double> v = numberValue(row))
        return numbertext::format(*v, r.decimals, sep_.str());
    return r.value;
}

bool PropertyGrid::setProp(const std::string& key, const std::string& value)
{
    if (key == "nameWidth") {
        setNameWidth(static_cast<float>(std::atof(value.c_str())));
        return true;
    }
    return Panel::setProp(key, value);   // basen SIST — konventionen
}

// STRUKTURELLA BARN — kontraktet och skälet står i headern.
// Definierad här och inte där: editorerna är framåtdeklarerade
// i headern, och en konvertering till basen kräver en komplett typ.
bool PropertyGrid::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(editor_) &&
           &child != static_cast<const Widget*>(picker_) &&
           &child != static_cast<const Widget*>(checkEd_) &&
           &child != static_cast<const Widget*>(colorEd_);
}

// TOOLTIPEN PER RAD — felets text vinner, sedan radens egen, sedan gridens.
TrText PropertyGrid::toolTipAt(const Ctx& c, float x, float y) const
{
    const int i = rowAt(c, x, y);
    if (i >= 0) {
        if (hasError(i))
            return tr(str::PropertyGridInvalid);
        if (!rows_[std::size_t(i)].toolTip.empty())
            return rows_[std::size_t(i)].toolTip;
    }
    return Widget::toolTipAt(c, x, y);
}

gfx::Sizef PropertyGrid::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner. Bredden är appens (en placeringsyta,
        // som Panel); AUTO-höjden är de synliga raderna × radhöjden plus ramens
        // indrag — oberoende av bredden, så en dockad Top-remsa mäts utan
        // `c.area`. Varje ändring av raderna, fällningen eller ramen
        // invaliderar, annars står mätcachen kvar på gamla höjden.
        gfx::Sizef s{width, height};
        if (s.h <= 0.0f)
            s.h = float(visibleCount_) * c.style->gridRowH +
                  (isBorder() ? 2.0f * c.style->panelInset : 0.0f);
        return s;
    }
    }
}

// ------------------------------------------------------- cellgeometrin
// EN sanning i widget-lokala LOGISKA px: ritningen läser dem direkt,
// cellträffen och editor-placeringen projicerar dem. Alla scroll-
// förskjutna (samma rymd som ritningen).

float PropertyGrid::nameW(const Ctx& c) const
{
    return nameWidth_ > 0.0f ? nameWidth_ : c.style->gridNameW;
}

float PropertyGrid::scrollOffset(const Ctx& c, const gfx::Rectf& in) const
{
    const float spelrum =
        float(visibleCount_) * c.style->gridRowH - in.h;
    return std::clamp(scrollY(), 0.0f, std::max(0.0f, spelrum));
}

gfx::Rectf PropertyGrid::rowBounds(const Ctx& c, int row) const
{
    if (row < 0 || std::size_t(row) >= pos_.size() || pos_[std::size_t(row)] < 0)
        return gfx::Rectf{};
    const gfx::Rectf in   = innerBounds(c);
    const float      rowH = c.style->gridRowH;
    return gfx::Rectf{in.x,
                      in.y - scrollOffset(c, in) +
                          float(pos_[std::size_t(row)]) * rowH,
                      in.w, rowH};
}

gfx::Rectf PropertyGrid::valueBounds(const Ctx& c, int row) const
{
    const gfx::Rectf full = rowBounds(c, row);
    const float      nw   = nameW(c);
    // Stanna före overlay-listens remsa — editorn får inte täcka den
    // (barn vinner hit-testen över remsan, dokumenterad panel-regel).
    return gfx::Rectf{full.x + nw, full.y,
                      std::max(0.0f, full.w - nw - c.style->scrollThick),
                      full.h};
}

gfx::Rectf PropertyGrid::checkBoxRect(const Ctx& c, const gfx::Rectf& cell)
{
    const float s = c.style->checkSize;
    return gfx::Rectf{cell.x + c.style->gridPadX, cell.y + (cell.h - s) / 2.0f,
                      s, s};
}

int PropertyGrid::rowAt(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf r = resolve(c);
    for (int i = 0; i < int(rows_.size()); ++i) {
        if (pos_[std::size_t(i)] < 0)
            continue;
        if (Draw::project(c, r, rowBounds(c, i)).contains(mx, my))
            return i;
    }
    return -1;
}

// ------------------------------------------------------------ ritningen

// RADEN — en rubrikrad är en fylld remsa i `panelHeader` (alla tre UI) med
// vinkeln i expanderkolumnen och namnet; en annan rad är namn + värdecell, med
// den aktiva radens markering per UI. Kolumnskiljaren skärs bara för raderna
// med två kolumner: en rubrikrad är en enda yta.
void PropertyGrid::drawRow(const Ctx& c, int i, const gfx::Rectf& r) const
{
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    const Row&  rd  = rows_[std::size_t(i)];
    const float nw  = nameW(c);
    const bool  på  = isEnabled() && rd.enabled;   // gridden och raden

    if (rd.kind == Kind::Header) {
        Draw::rect(c, r, 0.0f, col.panelHeader);
        const bool  ut    = rd.expanded && isEnabled();   // inaktiv = infälld
        const float halvW = ut ? m.chevronW : m.chevronH;
        const float halvH = ut ? m.chevronH : m.chevronW;
        const gfx::Rectf e{r.x, r.y, m.treeExpanderW, r.h};
        Draw::chevron(c,
                      {e.x + e.w / 2.0f - halvW, e.y + e.h / 2.0f - halvH,
                       2.0f * halvW, 2.0f * halvH},
                      ut ? Dir::Down : Dir::Right, Pen{col.textDim, 0.0f});
        Draw::text(c, {r.x + m.treeExpanderW + m.gridPadX, r.y, r.w, r.h},
                   rd.name, på ? col.text : col.textDim);
    } else {
        gfx::Color namn = på ? col.text : col.textDim;
        if (i == active_) {
            switch (c.ui) {
            // GLAZE — den AKTIVA RADENS namncell fylls med `accentSurface` och
            // namnet skrivs i `onAccent`, samma par som Dropdown-listans
            // hovrade rad. Först skulle raden bli en laminerad
            // `buttonHover`-platta; den mäter 1.53:1 mot panelen i ljust läge
            // — en markering man inte ser är ingen markering. `accentSurface`
            // mäter 5.67:1 / 3.45:1 och bär sin text (`onAccent`, grindat par).
            case UI::Glaze:
                Draw::rect(c, {r.x, r.y, nw, r.h}, 0.0f, col.accentSurface);
                namn = col.onAccent;
                break;
            // MINIMALISTIC — ett accent-STRECK i vänsterkanten (`rowMarkerW`)
            // och namnet i `accent`: fyllning betyder tillstånd hos
            // KONTROLLER, och en rad i en lista är ingen kontroll. Samma
            // markör som Dropdown-listans valda rad.
            case UI::Minimalistic:
                Draw::rect(c, {r.x, r.y, m.rowMarkerW, r.h}, 0.0f, col.accent);
                namn = col.accent;
                break;
            // SLATE — namncellen i `buttonHover`.
            case UI::Slate:
            default:
                Draw::rect(c, {r.x, r.y, nw, r.h}, 0.0f, col.buttonHover);
                break;
            }
        }
        Draw::text(c, {r.x + m.gridPadX, r.y, nw, r.h}, rd.name, namn);
        drawCell(c, i, r);
        Draw::line(c, r.x + nw, r.y, r.x + nw, r.y + r.h,
                   Pen{col.border, 0.0f});   // kolumnskiljaren
    }
    Draw::line(c, r.x, r.y + r.h, r.x + r.w, r.y + r.h,
               Pen{col.border, 0.0f});   // radlinjen
}

// VÄRDECELLENS innehåll per slag. Ett fel fyller cellen med `errorSurface` och
// skriver texten i `onError` (det grindade paret; som linje räcker inte
// `errorSurface` mot mörka ytor). En skrivskyddad eller inaktiv rad — eller
// en inaktiv grid — skriver sitt värde i `textDim`; en skrivskyddad kryssruta
// behåller full kontrast, för den läses som ett värde och inte som en
// avstängd kontroll. Rutan är listornas kryssruta: kontur med `checkRadius`
// och bock med `checkStroke`.
void PropertyGrid::drawCell(const Ctx& c, int i, const gfx::Rectf& r) const
{
    const auto& m    = *c.style;
    const auto& col  = c.theme->colors;
    const Row&  rd   = rows_[std::size_t(i)];
    const float nw   = nameW(c);
    const bool  på   = isEnabled() && rd.enabled;   // gridden och raden
    const gfx::Rectf cell{r.x + nw, r.y, r.w - nw, r.h};
    const gfx::Color fg = rd.readOnly || !på ? col.textDim : col.text;
    const bool fel = hasError(i);
    if (fel)
        Draw::rect(c, cell, 0.0f, col.errorSurface);
    const gfx::Color txt = fel ? col.onError : fg;

    switch (rd.kind) {
    case Kind::Check: {
        const gfx::Rectf box = checkBoxRect(c, cell);
        Draw::frame(c, box, m.checkRadius,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        if (isChecked(i))
            Draw::check(c, box, Pen{på ? col.text : col.textDim, m.checkStroke});
        break;
    }
    case Kind::Color: {
        const std::optional<gfx::Color> färg = colorValue(i);
        if (!färg) {
            Draw::text(c, {cell.x + m.gridPadX, r.y, cell.w, r.h}, rd.value,
                       txt);
            break;
        }
        // Färgrutan: inaktiv tonas den med ikonernas mått, och med alfa ritas
        // den över ett rutmönster så att genomskinligheten syns.
        const gfx::Rectf box = checkBoxRect(c, cell);
        gfx::Color       fyll = *färg;
        if (!på)
            fyll.a = static_cast<std::uint8_t>(
                std::lround(float(fyll.a) * (1.0f - m.iconFade)));
        if (rd.allowAlpha)
            Draw::checker(c, box, m.colorSwatchRadius, m.colorCheckerCell,
                          col.panel, col.appWorkspace);
        Draw::rect(c, box, m.colorSwatchRadius, fyll,
                   Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        Draw::text(c, {box.x + box.w + m.checkGap, r.y, cell.w, r.h},
                   propparse::hexColorText(*färg, rd.allowAlpha), txt);
        break;
    }
    case Kind::Number: {
        // Prefixet vänster, suffixet högerställt, talet emellan — samma tre
        // ytor som editorn placeras mellan (`openEditor`).
        const float x0 = cell.x + m.gridPadX;
        const float pw = rd.prefix.empty() ? 0.0f : Draw::measure(c, rd.prefix).w;
        const float sw = rd.suffix.empty() ? 0.0f : Draw::measure(c, rd.suffix).w;
        if (!rd.prefix.empty())
            Draw::text(c, {x0, r.y, pw, r.h}, rd.prefix, txt);
        Draw::text(c,
                   {x0 + pw, r.y,
                    std::max(0.0f, cell.w - 2.0f * m.gridPadX - pw - sw), r.h},
                   numberDisplay(i), txt);
        if (!rd.suffix.empty())
            Draw::text(c, {cell.x, r.y, cell.w - m.gridPadX, r.h}, rd.suffix,
                       txt, Align::Right);
        break;
    }
    case Kind::Header:
        break;
    case Kind::Text:
    default:
        // Värdet ritas alltid — den aktiva cellens editor täcker det.
        Draw::text(c, {r.x + nw + m.gridPadX, r.y, r.w, r.h}, rd.value, fg);
        break;
    }
}

void PropertyGrid::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const gfx::Rectf in  = innerBounds(c);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const float      nw  = nameW(c);
    // Innerformens radie (logiska px) — Panels EN sanning, koncentrisk mot
    // ytterytan. Raderna är DATA och ritas här i draw(), så trädvandringens
    // barnklipp (som täcker de hostade editorerna) hjälper dem inte: de
    // måste klippas mot samma form av griden själv.
    const float      rIn = innerRadius(c);

    // YTAN per UI. GLAZE och SLATE: namnkolumn på panel-nivån, värdekolumn på
    // den försänkta arbetsytan; ytterytan rundas EN gång och kolumnplattan
    // klipps mot samma gemensamma innerform (att runda kolumnerna var för sig
    // hade rundat den INRE skarven — fel geometri, inte bara fel utseende), och
    // ramen är panel-familjens kant. Kolumnerna lamineras inte: de är ytor man
    // läser i, inte kroppar man påverkar. MINIMALISTIC: inga kolumnfyllningar —
    // "ytor separeras med en LINJE, inte med en fyllning" gäller griden hårdast,
    // som bara är rader och kolumner — men ytan fylls i panelens färg, annars
    // låg radernas markering direkt på fönstret.
    // INAKTIV: värdekolumnen på `fieldReadOnly`, som ett inaktivt fält — annars
    // såg en inaktiv grid aktiv ut (texten dämpas i `drawRow`/`drawCell`).
    switch (c.ui) {
    case UI::Minimalistic:
        Draw::rect(c, b, m.panelRadius, col.panel);
        Draw::frame(c, b, m.panelRadius, Pen{col.border, m.borderWidth});
        break;
    case UI::Glaze:
    case UI::Slate:
    default:
        Draw::rect(c, in, rIn, isEnabled() ? col.appWorkspace : col.fieldReadOnly);
        Draw::clip(c, in, rIn);
        Draw::rect(c, {in.x, in.y, nw, in.h}, 0.0f, col.panel);
        Draw::endClip(c);
        Draw::frame(c, b, m.panelRadius, Pen{col.border, m.borderWidth});
        break;
    }

    // De fyra tillstånden (princip 7) — de bor per RAD, inte på widgeten:
    //   vila       gridlinjer `border`
    //   hover      — (raden markeras inte av musen; ETT aktivt val räcker)
    //   tryck      aktiv rad: markeringen per UI (`drawRow`)
    //   fokus      ägs av den hostade editorn, inte av griden
    //   inaktiv    skrivskyddat värde och inaktiv rad `textDim`
    Draw::clip(c, in, rIn);
    const float rowH = m.gridRowH;
    for (int i = 0; i < int(rows_.size()); ++i) {
        if (pos_[std::size_t(i)] < 0)
            continue;
        const gfx::Rectf row = rowBounds(c, i);
        if (row.y + row.h < in.y)
            continue;                    // ovanför synfältet
        if (row.y > in.y + in.h)
            break;                       // nedanför — resten också
        drawRow(c, i, row);
    }
    // Kolumnskiljaren under den sista raden, ner till kanten.
    const float slut = in.y - scrollOffset(c, in) + float(visibleCount_) * rowH;
    if (slut < in.y + in.h)
        Draw::line(c, in.x + nw, std::max(slut, in.y), in.x + nw, in.y + in.h,
                   Pen{col.border, 0.0f});
    Draw::endClip(c);
}

gfx::Sizef PropertyGrid::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt) — radhöjden är logisk, som allt annat.
    const gfx::Sizef base  = Panel::contentExtent(c);
    const float      rowsH =
        float(visibleCount_) * c.style->gridRowH * c.scale;
    return gfx::Sizef{base.w, std::max(base.h, rowsH)};
}

// ------------------------------------------------------------ beteendet

// ORDNINGEN i ett tryck: remsan → en öppen färgpopup → den pågående
// redigeringen → rubrikraden → en rad som inte går att ändra → kryssrutan →
// färgen → text, vallista och tal. Tillståndet ändras alltid FÖRE editorn
// öppnas och händelserna fyras SIST.
void PropertyGrid::mouseDown(const Ctx& c, float mx, float my)
{
    // Remsorna ligger överst — tumdrag/rännklick går till Panel.
    if (inScrollStrip(c, mx, my)) {
        Panel::mouseDown(c, mx, my);
        return;
    }
    Panel::mouseDown(c, mx, my);   // nollar drag-läget

    // ETT TRYCK MEDAN FÄRGPOPUPEN ÄR ÖPPEN STÄNGER BARA POPUPEN: dess commit
    // postas, och en ny editor på en annan färgrad hade hunnit sätta sin färg
    // i den gemensamma väljaren före den.
    if (colorEd_->isVisible() && colorEd_->isOpen()) {
        hideEditors();
        return;
    }

    // ETT KLICK I GRIDDEN SPARAR FÖRST — datagridens ordning: den
    // pågående redigeringen avslutas och dess events fyras DIREKT (vi står i
    // händelseflödet, och gridden tar inte fokus, så ingen fokusförlust gör
    // det åt oss). En handler som räknar om och bygger om raderna är därmed
    // KLAR innan nästa rad öppnas — det var den ordningen som gjorde att en
    // omräkning i handlern rev nästa redigering. Rev eller flyttade den
    // gridden är klicket slut här: `resolve` och `openEditor` hade
    // rört en widget som inte längre finns, eller sitter någon annanstans.
    if (!endEdit(true, false))
        return;

    // Cellträff via SAMMA geometri som ritningen, bara projicerad till
    // musens fysiska rymd (aldrig dubblerade mått) — mot raderna som de är
    // EFTER sparningen.
    const int i = rowAt(c, mx, my);
    if (i < 0) {
        hideEditors();
        return;
    }
    const Row& rd = rows_[std::size_t(i)];
    if (rd.kind == Kind::Header) {
        hideEditors();
        if (rd.enabled)
            setExpanded(i, !rd.expanded);
        return;
    }
    const gfx::Rectf r = resolve(c);
    if (rd.readOnly || !rd.enabled ||
        !Draw::project(c, r, valueBounds(c, i)).contains(mx, my)) {
        hideEditors();
        return;
    }
    if (rd.kind == Kind::Check) {
        rows_[std::size_t(i)].value = isChecked(i) ? "false" : "true";
        changed_                    = i;
        openEditor(c, i);   // fokuserbäraren på cellen
        fireChange(i);      // händelserna sist
        return;
    }
    openEditor(c, i);
}

// Raden redigeras i en av fyra editorer; de andra stängs först. Editorns
// plats är radens plats bland de synliga och namnkolumnens bredd, i LOGISKA px
// relativt childArea — barnen resolvar mot den (scrollad), så editorn följer
// scrollen utan extra kod.
void PropertyGrid::openEditor(const Ctx& c, int row)
{
    const Row& r = rows_[std::size_t(row)];
    hideEditors();
    active_ = row;

    const gfx::Rectf cell = valueBounds(c, row);
    float            x    = nameW(c);
    const float      y    = float(pos_[std::size_t(row)]) * c.style->gridRowH;
    float            w    = cell.w;

    Widget* ed = nullptr;
    switch (r.kind) {
    case Kind::Check:
        ed = checkEd_;
        break;
    case Kind::Color:
        ed = colorEd_;
        break;
    case Kind::Number: {
        // Fältet står MELLAN prefixet och suffixet, som ritningen ställer dem.
        const float pad = c.style->gridPadX;
        const float pw  = r.prefix.empty() ? 0.0f
                                           : pad + Draw::measure(c, r.prefix).w;
        const float sw  = r.suffix.empty() ? 0.0f
                                           : pad + Draw::measure(c, r.suffix).w;
        x += pw;
        w = std::max(0.0f, cell.w - pw - sw);
        ed = editor_;
        break;
    }
    case Kind::Header:
        active_ = -1;   // en rubrik redigeras aldrig
        return;
    case Kind::Text:
    default:
        ed = r.choices.empty() ? static_cast<Widget*>(editor_)
                               : static_cast<Widget*>(picker_);
        break;
    }
    ed->x      = x;
    ed->y      = y;
    ed->width  = w;
    ed->height = cell.h;

    switch (r.kind) {
    case Kind::Check:
        break;
    case Kind::Color: {
        colorEd_->setAllowAlpha(r.allowAlpha);
        colorEd_->setColor(colorValue(row).value_or(gfx::Color{}));
        colorRow_     = row;
        colorRowsGen_ = rowsGen_;
        colorRowGen_  = rowGen_[std::size_t(row)];
        break;
    }
    case Kind::Number:
        editor_->börja(numberDisplay(row), true);
        break;
    default:
        if (r.choices.empty()) {
            editor_->börja(r.value, false);
        } else {
            picker_->setItems(r.choices);
            const auto it =
                std::find(r.choices.begin(), r.choices.end(), r.value);
            picker_->setSelectedIndex(
                it == r.choices.end() ? -1 : int(it - r.choices.begin()));
        }
        break;
    }
    ed->show();
    ed->focus();
    if (r.kind == Kind::Color)
        colorEd_->open();   // klicket ÄR intentionen — öppna popupen direkt
    else if (r.kind == Kind::Number ||
             (r.kind == Kind::Text && r.choices.empty()))
        editor_->selectAll();   // det man skriver ERSÄTTER värdet
    else if (r.kind == Kind::Text)
        picker_->open();   // öppna listan direkt
    invalidate();
}

void PropertyGrid::hideEditors()
{
    if (picker_)
        picker_->close();
    if (colorEd_)
        colorEd_->close();
    if (editor_)
        editor_->hide();
    if (picker_)
        picker_->hide();
    if (checkEd_)
        checkEd_->hide();
    if (colorEd_)
        colorEd_->hide();
    active_ = -1;
    invalidate();
}

// AVSLUTET — datagridens `endEdit`, och ORDNINGEN är bärande:
//   1. raden och texten läses, och editorerna stängs FÖRST (`active_` = −1),
//      så en andra väg in — editorns `focusLost` när en handler flyttar
//      fokus, ett nytt klick — ser att ingen redigering pågår;
//   2. värdet skrivs och eventen fyras SIST, när allt är städat: en handler
//      får göra vad som helst, även bygga om raderna. Ur en `focusLost`
//      POSTAS de i stället (`postEvent`), och slopas om raderna bytts.
// Bara fritexten och talet avslutas här — vallistan sparar vid valet, kryssen
// vid trycket och färgen vid sin commit. En text som inte ändrade värdet
// sparar ingenting och fyrar inget; ett tal jämförs efter klampning och
// avrundning, så "12,50" mot det sparade "12.5" är samma tal. En otolkbar
// talrad sparar ingenting heller — den skrivna texten blir radens FEL tills
// den ersätts; Escape tar bort felet. Svarar om gridden går att röra efteråt
// (en direkt avfyrning kör handlers som får riva den).
bool PropertyGrid::endEdit(bool spara, bool postad)
{
    if (active_ < 0 || !editor_->isVisible())
        return true;
    const int         r  = active_;
    const std::string ny = editor_->text();
    hideEditors();
    if (std::size_t(r) >= rows_.size())
        return true;
    Row& rd = rows_[std::size_t(r)];
    if (!spara) {
        if (pending_[std::size_t(r)]) {
            pending_[std::size_t(r)].reset();
            invalidate();
        }
        return true;
    }
    if (rd.kind == Kind::Number) {
        double v = 0.0;
        if (!numbertext::parseUser(ny, v)) {
            pending_[std::size_t(r)] = ny;
            invalidate();
            return true;
        }
        const double eff = numbertext::effective(v, rd.min, rd.max, rd.decimals);
        const std::optional<double> förut = numberValue(r);
        pending_[std::size_t(r)].reset();
        invalidate();
        if (förut && *förut == eff)
            return true;
        rd.value = numbertext::format(eff, rd.decimals, ".");
    } else {
        if (ny == rd.value)
            return true;
        rd.value = ny;
    }
    changed_ = r;
    invalidate();
    if (!postad)
        return fireChange(r);
    // KÖAD: slopas om raderna bytts (`setRows`) eller just raden ersatts
    // (`setRow`) innan svansen — då beskriver (r) inte sparningen.
    const unsigned gen    = rowsGen_;
    const unsigned radGen = rowGen_[std::size_t(r)];
    postEvent([this, gen, radGen, r] {
        if (gen != rowsGen_ || std::size_t(r) >= rowGen_.size() ||
            radGen != rowGen_[std::size_t(r)])
            return;
        fireChange(r);
    });
    return true;
}

// KRYSSRUTANS växling i en öppen redigering — trycket på editorn och Space.
// Värdet skrivs FÖRE eventen, och svaret är `fireChange`s.
bool PropertyGrid::toggleActive()
{
    const int r = active_;
    if (!valid(r) || rows_[std::size_t(r)].kind != Kind::Check)
        return true;
    rows_[std::size_t(r)].value = isChecked(r) ? "false" : "true";
    changed_                    = r;
    invalidate();
    return fireChange(r);
}

// FÄRGVÄLJARENS commit → raden. Generationsvakten: en postad commit (hexfältet,
// stängningen) gäller bara om raderna och just den raden är desamma som när
// popupen öppnades. En effektivt oförändrad färg (`#3366ff` mot `#3366FF`)
// skriver och fyrar ingenting.
void PropertyGrid::commitColor()
{
    const int r = colorRow_;
    if (!valid(r) || colorRowsGen_ != rowsGen_ ||
        colorRowGen_ != rowGen_[std::size_t(r)])
        return;
    Row&             rd = rows_[std::size_t(r)];
    const gfx::Color nu = colorEd_->color();
    const std::optional<gfx::Color> förut = colorValue(r);
    if (förut && *förut == nu)
        return;
    rd.value = propparse::hexColorText(nu, rd.allowAlpha);
    changed_ = r;
    invalidate();
    fireChange(r);
}

// `commitEdit` (widget.h) är Enter utan tangenten.
void PropertyGrid::commitEdit()
{
    endEdit(true, false);
    if (colorEd_->isVisible())
        colorEd_->commitEdit();
}

// `onChange` och sedan `onCommit` — en sparning är en avslutad handling. Raden
// sätts här (ett postat event kan komma efter en senare sparning) och IGEN
// före `onCommit`: en onChange-handler som byggt om raderna har nollat den,
// och `onCommit` ska läsa den sparade raden (kontraktet vid `changedRow`).
bool PropertyGrid::fireChange(int row)
{
    changed_ = row;
    if (!fireEvent(onChange))
        return false;
    changed_ = row;
    return fireEvent(onCommit);
}

} // namespace glaze
