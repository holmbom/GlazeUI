// PropertyGrid — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. Cellgeometrin
// bor HÄR i LOGISKA px (rowBounds/nameBounds/valueBounds) — EN sanning som
// ritningen läser rakt av och som cellträffen/editor-placeringen
// projicerar till fysiska px (Draw::project). Basen Panel ger klipp,
// hjulscroll, overlay-listerna och innehållsytan; contentExtent överrids
// så radhöjden (data, inte barn) styr scrollintervallet.

#include "glaze/widgets/property_grid.h"

#include <algorithm>
#include <cstdlib>

#include <gfx/platform.h>

#include "glaze/draw.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/textinput.h"

namespace glaze {

// ------------------------------------------------------------- editorn
//
// En riktig `TextInput` med två tillägg och inget annat — datagridens
// `TextEditor`-form. En tangent går till den FOKUSERADE widgeten och
// bubblar aldrig vidare, så utan subklassen hade Enter och Escape aldrig nått
// gridden. Enter SPARAR och Escape AVBRYTER — båda konsumeras: de har gjort
// sitt jobb, och en Enter som därefter nådde menyernas acceleratorer hade gjort
// två saker. Med Ctrl/Cmd/Alt är tangenten någon annans genväg och går till
// fältet. Fokusförlusten sparar (postat). Allt annat — caret, markering,
// klippbord — ärvs orört.
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
    // rad råkar ha samma värde.
    void börja(const std::string& text)
    {
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

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.endEdit(true, true);
    }

private:
    PropertyGrid& owner_;
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

GLAZE_REGISTER_WIDGET("propertygrid", PropertyGrid)

PropertyGrid::PropertyGrid()
{
    setClip(true);        // rader och editorer klipps mot innehållsytan
    showScrollbars(true); // överflöd ska SYNAS

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
}

void PropertyGrid::setRows(std::vector<Row> rows)
{
    ++rowsGen_;      // ett postat event beskriver de gamla raderna
    hideEditors();   // AVBRYTER — redigeringen gällde de gamla raderna
    rows_    = std::move(rows);
    rowGen_.assign(rows_.size(), 0u);
    changed_ = -1;
    invalidate();
}

// STYCKEFORMEN — kontraktet och skälet står i headern.
// Ingen `hideEditors()`: att lägga till en rad SIST rör inte den rad som
// redigeras, och att stänga editorn hade varit en tillståndsförlust som
// inte hör till operationen.
void PropertyGrid::addRow(std::string name, std::string value, bool readOnly,
                          std::string tag)
{
    Row r;
    r.name     = std::move(name);
    r.value    = std::move(value);
    r.readOnly = readOnly;
    r.tag      = std::move(tag);
    rows_.push_back(std::move(r));
    rowGen_.push_back(0u);
    invalidate();
}

void PropertyGrid::setRow(int index, std::string name, std::string value,
                          bool readOnly, std::string tag)
{
    if (index < 0 || std::size_t(index) >= rows_.size()) {
        gfx::log("PropertyGrid(%s): setRow(%d) utanför intervallet (%zu "
                 "rader) — ignoreras", this->name.c_str(), index,
                 rows_.size());
        return;
    }
    // Raden ERSÄTTS helt: `choices` nollas med, precis som `tag` gör när
    // svansargumentet utelämnas. Tilldelning, inte uppdatering.
    Row r;
    r.name     = std::move(name);
    r.value    = std::move(value);
    r.readOnly = readOnly;
    r.tag      = std::move(tag);
    // Skriver vi under en ÖPPEN editor pekar den på en rad som inte finns
    // längre i samma form — stäng den UTAN att spara, som `setRows` gör. Och
    // en KÖAD sparning av raden (fokusförlustens postade events) beskriver
    // den gamla raden: radens generation räknas upp, så den slopas.
    if (active_ == index)
        hideEditors();
    rows_[std::size_t(index)] = std::move(r);
    ++rowGen_[std::size_t(index)];
    invalidate();
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
// Definierad här och inte där: `TextInput`/`Dropdown` är framåtdeklarerade
// i headern, och en konvertering till basen kräver en komplett typ.
bool PropertyGrid::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(editor_) &&
           &child != static_cast<const Widget*>(picker_);
}

gfx::Sizef PropertyGrid::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // AXELREGELN: som Panel — griden är en
        // Panel-ättling och dimensioneras av appen, inte av stilen. Ingen
        // auto-storlek.
        return gfx::Sizef{width, height};
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

gfx::Rectf PropertyGrid::rowBounds(const Ctx& c, int row) const
{
    const gfx::Rectf in   = innerBounds(c);
    const float      rowH = c.style->gridRowH;
    return gfx::Rectf{in.x, in.y - scrollY() + float(row) * rowH, in.w, rowH};
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

    switch (c.ui) {
    // GLAZE — samma kolumner som Slate (`panel` / `appWorkspace` + kontur),
    // men den AKTIVA RADENS namncell fylls med `accentSurface` och namnet
    // skrivs i `onAccent`. Griden ÄR redan
    // Glaze-receptet — försänkt värdekolumn, upphöjd namnkolumn, bestämd
    // kant — och det enda som saknades var att den valda raden faktiskt
    // SYNS. Samma par som Dropdown-listans hovrade rad; de är samma sak
    // sedd två gånger och ska se lika ut.
    //
    // Först skulle raden bli en LAMINERAD `buttonHover`-platta. Det föll på
    // mätningen: `buttonHover` mot panelen är 2.01:1 rått och **1.53:1
    // laminerad** i ljust läge — en markering man inte ser är ingen
    // markering. `accentSurface` mäter 5.67:1 / 3.45:1 mot panelen och bär
    // dessutom sin text (`onAccent`, 5.67:1 / 4.81:1 — grindat par).
    // Lamineringen är borta av familjens generella skäl: den kräver en
    // KONTUR för att inte tvätta bort sin egen separation, och en cell i en
    // tabell har ingen.
    //
    // Kolumnerna lamineras heller inte. De är ytor man läser i, inte kroppar
    // man påverkar; en laminerad kolumn hade sett ut som en bred knapp.
    //
    // De fyra tillstånden (princip 7) — de bor per RAD:
    //   vila       namnkolumn `panel`, värdekolumn `appWorkspace`,
    //              gridlinjer `border`
    //   hover      — (raden markeras inte av musen; ETT aktivt val räcker)
    //   tryck      aktiv rad: namncellen `accentSurface`, namnet `onAccent`
    //   fokus      ägs av den hostade editorn
    //   inaktiv    skrivskyddat värde `textDim`
    case UI::Glaze:
        // Ytterytan/ramen rundas EN gång; kolumnplattan klipps mot samma
        // gemensamma innerform. Att runda kolumnerna var för sig hade
        // rundat den INRE skarven mellan namn- och värdekolumnen — fel
        // geometri, inte bara fel utseende.
        Draw::rect(c, in, rIn, col.appWorkspace);
        Draw::clip(c, in, rIn);
        Draw::rect(c, {in.x, in.y, nw, in.h}, 0.0f, col.panel);
        Draw::endClip(c);
        Draw::frame(c, b, m.panelRadius, Pen{col.border, m.borderWidth});

        Draw::clip(c, in, rIn);
        for (int i = 0; i < int(rows_.size()); ++i) {
            const gfx::Rectf row = rowBounds(c, i);
            if (row.y + row.h < in.y)
                continue;
            if (row.y > in.y + in.h)
                break;
            const Row& rd = rows_[std::size_t(i)];

            const bool act = i == active_;
            if (act)
                Draw::rect(c, {row.x, row.y, nw, row.h}, 0.0f,
                           col.accentSurface);
            Draw::text(c, {row.x + m.gridPadX, row.y, nw, row.h}, rd.name,
                       act ? col.onAccent : col.text);
            Draw::text(c, {row.x + nw + m.gridPadX, row.y, row.w, row.h},
                       rd.value, rd.readOnly ? col.textDim : col.text);
            Draw::line(c, row.x, row.y + row.h, row.x + row.w, row.y + row.h,
                       Pen{col.border, 0.0f});
        }
        Draw::line(c, in.x + nw, in.y, in.x + nw, in.y + in.h,
                   Pen{col.border, 0.0f});
        Draw::endClip(c);
        break;

    // MINIMALISTIC — INGA KOLUMNFYLLNINGAR.
    // "Ytor separeras med en LINJE, inte med en fyllning" gäller griden
    // hårdast av alla: den är ju bara rader och kolumner, och en tabell som
    // bygger sin struktur på två bakgrundsfärger är precis vad familjen
    // valde bort. Kvar står radlinjerna, kolumnskiljaren och ramen.
    //
    // Den aktiva raden markeras med ett accent-STRECK i vänsterkanten
    // (`rowMarkerW`) i stället för med en fylld namncell. Regeln "fyllning
    // betyder tillstånd" gäller KONTROLLER — en rad i en lista är ingen
    // kontroll, och att fylla den hade gjort griden till ett fält av
    // plattor. Samma markör som Dropdown-listans valda rad; de två är samma
    // sak sedd två gånger och delar därför mått.
    //
    // De fyra tillstånden (princip 7) — per RAD:
    //   vila       ingen fyllning, radlinjer `border`
    //   hover      —
    //   tryck      aktiv rad: `accent`-markör i vänsterkanten, namnet i
    //              `accent`
    //   fokus      ägs av den hostade editorn
    //   inaktiv    skrivskyddat värde `textDim`
    case UI::Minimalistic:
        // YTAN FYLLS — se panel.cpp. Rutnätet är en container
        // av samma slag som Panel och ska ha samma bakgrund; utan den låg
        // radernas markering och kolumnlinje direkt på fönstret.
        Draw::rect(c, b, m.panelRadius, col.panel);
        Draw::frame(c, b, m.panelRadius, Pen{col.border, m.borderWidth});

        Draw::clip(c, in, rIn);
        for (int i = 0; i < int(rows_.size()); ++i) {
            const gfx::Rectf row = rowBounds(c, i);
            if (row.y + row.h < in.y)
                continue;
            if (row.y > in.y + in.h)
                break;
            const Row& rd  = rows_[std::size_t(i)];
            const bool act = i == active_;

            if (act)
                Draw::rect(c, {row.x, row.y, m.rowMarkerW, row.h}, 0.0f,
                           col.accent);
            Draw::text(c, {row.x + m.gridPadX, row.y, nw, row.h}, rd.name,
                       act ? col.accent : col.text);
            Draw::text(c, {row.x + nw + m.gridPadX, row.y, row.w, row.h},
                       rd.value, rd.readOnly ? col.textDim : col.text);
            Draw::line(c, row.x, row.y + row.h, row.x + row.w, row.y + row.h,
                       Pen{col.border, 0.0f});
        }
        Draw::line(c, in.x + nw, in.y, in.x + nw, in.y + in.h,
                   Pen{col.border, 0.0f});
        Draw::endClip(c);
        break;

    // SLATE — det medvetna UNDANTAGET i taxonomin:
    // griden tillhör FLERA klasser samtidigt. Den ÄR en yta (ram + två
    // kolumnbakgrunder), den bär ett SPÅR (värdekolumnen på `appWorkspace`),
    // en PLATTA (den aktiva radens namncell) och två hostade FÄLT
    // (TextInput/Dropdown). Taxonomin beskriver alltså dess DELAR, inte
    // widgeten. Dess overlay-remsor är Panels.
    //
    // Radierna kommer ur `Style::panelRadius`. Editorerna placeras
    // rakt över värdecellen och hade läckt ut i ett rundat hörn. De är
    // BARN, så trädvandringens rundade klipp täcker dem; radernas
    // egen ritning klipps mot samma innerform (`rIn`) av griden själv.
    //
    // De fyra tillstånden (princip 7) — de bor per RAD, inte på widgeten:
    //   vila       namnkolumn `panel`, värdekolumn `appWorkspace`,
    //              gridlinjer `border`
    //   hover      — (raden markeras inte av musen; ETT aktivt val räcker)
    //   tryck      aktiv rad: namncellen `buttonHover`
    //   fokus      ägs av den hostade editorn, inte av griden
    //   inaktiv    skrivskyddat värde `textDim`
    case UI::Slate:
    default:
        // Ytan: namnkolumn på panel-nivån, värdekolumn på den försänkta
        // arbetsytan; ram runt hela (panel-familjens kant). Rundningen:
        // se Glaze-grenen ovan — ytterytan EN gång, kolumnplattan klippt.
        Draw::rect(c, in, rIn, col.appWorkspace);
        Draw::clip(c, in, rIn);
        Draw::rect(c, {in.x, in.y, nw, in.h}, 0.0f, col.panel);
        Draw::endClip(c);
        Draw::frame(c, b, m.panelRadius, Pen{col.border, m.borderWidth});

        Draw::clip(c, in, rIn);
        for (int i = 0; i < int(rows_.size()); ++i) {
            const gfx::Rectf row = rowBounds(c, i);
            if (row.y + row.h < in.y)
                continue;                    // ovanför synfältet
            if (row.y > in.y + in.h)
                break;                       // nedanför — resten också
            const Row& rd = rows_[std::size_t(i)];

            if (i == active_)                // aktiv rad markeras i namnet
                Draw::rect(c, {row.x, row.y, nw, row.h}, 0.0f,
                           col.buttonHover);
            Draw::text(c, {row.x + m.gridPadX, row.y, nw, row.h}, rd.name,
                       col.text);
            // Värdet ritas alltid — den aktiva cellens editor täcker det.
            Draw::text(c, {row.x + nw + m.gridPadX, row.y, row.w, row.h},
                       rd.value, rd.readOnly ? col.textDim : col.text);
            // Radlinjen (subtil gridlinje).
            Draw::line(c, row.x, row.y + row.h, row.x + row.w, row.y + row.h,
                       Pen{col.border, 0.0f});
        }
        Draw::line(c, in.x + nw, in.y, in.x + nw, in.y + in.h,
                   Pen{col.border, 0.0f});   // kolumnskiljaren
        Draw::endClip(c);
        break;
    }
}

gfx::Sizef PropertyGrid::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt) — radhöjden är logisk, som allt annat.
    const gfx::Sizef base  = Panel::contentExtent(c);
    const float      rowsH = float(rows_.size()) * c.style->gridRowH * c.scale;
    return gfx::Sizef{base.w, std::max(base.h, rowsH)};
}

void PropertyGrid::mouseDown(const Ctx& c, float mx, float my)
{
    // Remsorna ligger överst — tumdrag/rännklick går till Panel.
    if (inScrollStrip(c, mx, my)) {
        Panel::mouseDown(c, mx, my);
        return;
    }
    Panel::mouseDown(c, mx, my);   // nollar drag-läget

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
    // EFTER sparningen. Klick på en redigerbar värdecell öppnar editorn;
    // allt annat stänger.
    const gfx::Rectf r = resolve(c);
    for (int i = 0; i < int(rows_.size()); ++i) {
        if (!Draw::project(c, r, rowBounds(c, i)).contains(mx, my))
            continue;
        const Row& rd = rows_[std::size_t(i)];
        if (!rd.readOnly &&
            Draw::project(c, r, valueBounds(c, i)).contains(mx, my))
            openEditor(c, i);
        else
            hideEditors();
        return;
    }
    hideEditors();
}

void PropertyGrid::openEditor(const Ctx& c, int row)
{
    const Row& r = rows_[std::size_t(row)];
    active_ = row;

    // Cellen i LOGISKA px relativt childArea — barnen resolvar mot den
    // (scrollad), så editorn följer scrollen utan extra kod. Cellens
    // logiska rect är widget-lokal; childArea är widgetens innerBounds
    // minus scroll, alltså skiljer bara panelramen och scrolloffseten.
    const gfx::Rectf cell = valueBounds(c, row);
    const gfx::Rectf in   = innerBounds(c);
    const float x = cell.x - in.x + scrollX();
    const float y = cell.y - in.y + scrollY();

    Widget* ed = r.choices.empty() ? static_cast<Widget*>(editor_)
                                   : static_cast<Widget*>(picker_);
    Widget* other = r.choices.empty() ? static_cast<Widget*>(picker_)
                                      : static_cast<Widget*>(editor_);
    other->hide();
    ed->x      = x;
    ed->y      = y;
    ed->width  = cell.w;
    ed->height = cell.h;

    if (r.choices.empty()) {
        editor_->börja(r.value);
    } else {
        picker_->setItems(r.choices);
        const auto it =
            std::find(r.choices.begin(), r.choices.end(), r.value);
        picker_->setSelectedIndex(
            it == r.choices.end() ? -1 : int(it - r.choices.begin()));
    }
    ed->show();
    ed->focus();
    if (r.choices.empty())
        editor_->selectAll();   // det man skriver ERSÄTTER värdet
    else
        picker_->open();   // klicket ÄR intentionen — öppna listan direkt
    invalidate();
}

void PropertyGrid::hideEditors()
{
    if (picker_)
        picker_->close();
    if (editor_)
        editor_->hide();
    if (picker_)
        picker_->hide();
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
// Bara fritexten avslutas här — vallistan sparar vid valet. En text som inte
// ändrade värdet sparar ingenting och fyrar inget. Svarar om gridden går att
// röra efteråt (en direkt avfyrning kör handlers som får riva den).
bool PropertyGrid::endEdit(bool spara, bool postad)
{
    if (active_ < 0 || !editor_->isVisible())
        return true;
    const int         r  = active_;
    const std::string ny = editor_->text();
    hideEditors();
    if (!spara || std::size_t(r) >= rows_.size() ||
        ny == rows_[std::size_t(r)].value)
        return true;
    rows_[std::size_t(r)].value = ny;
    changed_                    = r;
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

// `commitEdit` (widget.h) är Enter utan tangenten.
void PropertyGrid::commitEdit()
{
    endEdit(true, false);
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
