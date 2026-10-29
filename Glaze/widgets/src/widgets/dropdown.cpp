// Dropdown — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. Både den
// stängda kontrollen och popup-listans rader ritas HÄR, tillsammans med
// beteendet: öppna/stänga, tangenter, val — och popup-maskineriet. Listan
// är ett riktigt fönster (Chrome::None + popup-attributet) som ägs lat:
// AppWindow-OBJEKTET återanvänds mellan öppningar (OS-fönstret återskapas
// per öppning — popup-dismissal går via App::closePopups → close).
// Placeringen går via POPUP-KONTRAKTET (setPopupAnchor):
// ankarrect i ägarens klient-fysiska px; flip/klamp
// och all skärmrymds-aritmetik bor i BACKENDEN (Wayland saknar
// skärmpositioner helt).

#include "glaze/widgets/dropdown.h"

#include "glaze/widgets/listbox.h"

#include <algorithm>
#include <cmath>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../font_set.h"
#include "../prop_parse.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("dropdown", Dropdown)

// ------------------------------------------------------- interna klasser

// Popup-listans rader — en SUBKLASS till `glaze::ListBox`: radrecept,
// hit-test och scroll bor i ListBox och är delat.
//
// TILLSTÅNDSMODELLEN (hela poängen är att dropdownen inte kräver
// "tre överrider"):
//
//   Dropdown::selectedIndex_        det COMMITTADE valet — hela widgetens
//                                   livstid
//   popup-listans selectedIndex_    det PROVISORISKA valet — lever bara
//                                   medan popupen gör det
//   Dropdown har inget hover-index: det hade varit en dubblering — markören
//                                   hade bytt tillståndskälla beroende på
//                                   inmatningsenhet, och `commit(hover_)`
//                                   hade kunnat committa ett annat index än
//                                   popupen ritar starkt.
//
// Att popupen aldrig går genom ListBoxens ANVÄNDARVÄG (`selectFromUser`)
// är det som gör dubbelfyrning omöjlig BY DESIGN: musen sätter det
// provisoriska valet (programmatisk setter, noll event) och ber sedan
// ÄGAREN committa — Dropdowns `onChange` fyras alltså exakt en gång, från
// exakt ett ställe.
class DropdownRows final : public ListBox {
public:
    explicit DropdownRows(Dropdown& d) : d_(&d)
    {
        // INGEN RAM-INSET: popupen ÄR fönstret, och raderna ska ligga kant
        // i kant som de alltid gjort. `setBorder(false)` gör `innerBounds`
        // identisk med ytan; ramen ritas ändå av ListBox, ovanpå raderna.
        setBorder(false);
        // INGA OVERLAY-REMSOR: popupen scrollar med en OSYNLIG offset,
        // och en synlig tumme hade varit ett oredovisat utseendedelta.
        // Kapningen vid `dropMaxRows` finns kvar och gör listan kort.
        showScrollbars(false);
    }

    // ÄGAREN är ingen subklass och når inte ListBoxens skyddade popup-yta —
    // därför de här raderna, och inga fler. Radhöjden är listans
    // EGEN, i dropdownens textstorlek: popupens höjd räknas ur den.
    void  setProvisional(int row) { setProvisionalIndex(row); }
    int   provisional() const { return provisionalIndex(); }
    float rowHeight(const Ctx& c) const { return rowH(c, 0); }

protected:
    // Popupen ligger på ett EGET OS-fönster och har ingenting bakom sig att
    // lysa igenom — därför fyller även Minimalistic sin yta här.
    bool ownsSurface() const override { return true; }

    // INGET HÖGERTRYCKSVAL: listans högertryck
    // väljer genom `selectFromUser`, och popupens val är PROVISORISKT — det
    // får aldrig gå den vägen (headern ovan, listbox.h).
    bool rightDown(const Ctx&, float, float) override { return false; }

    // Dock::Fill styr storleken — måttet används aldrig, och popupens höjd
    // räknas ur radhöjden (`rowHeight`) en helt annan väg.
    //
    // `{0,0}` ÄR ENHETSNEUTRALT, och det är hela skälet att den här raden
    // får stå utan en dynamisk grind: `measure_gate` når bara REGISTRERADE
    // typer och den här klassen är TU-lokal. KONTRAKTET är därför att den
    // FÖRBLIR `{0,0}` — ger någon den ett verkligt mått måste den ändringen
    // ta med sig en egen grind. Radhöjdstesterna i `dropdown_gate` mäter
    // POPUPGEOMETRI och bevisar ingenting om den här funktionen.
    gfx::Sizef contentSize(const Ctx&) const override
    {
        return gfx::Sizef{0.0f, 0.0f};
    }

    // REMSA-FÖRST i båda krokarna. Remsorna är avslagna här, så
    // kroken är i praktiken bara Panels drag-nollning — men mönstret står
    // kvar: slås remsorna någon gång på får popupen tumdrag utan att en rad
    // committas under tummen.
    void mouseMove(const Ctx& c, float mx, float my) override
    {
        if (handleStripFirst(c, mx, my, false))
            return;
        const int row = rowAtY(c, my);
        if (row >= 0)
            setProvisionalIndex(row);   // PROGRAMMATISK — noll event
    }

    void mouseDown(const Ctx& c, float mx, float my) override
    {
        if (handleStripFirst(c, mx, my, true))
            return;
        const int row = rowAtY(c, my);
        if (row >= 0) {
            // MARKÖREN FÖRST, därefter commit. Ett klick UTAN föregående
            // mouseMove (touch, syntetiska events, vissa capture-sekvenser)
            // hade annars committat rad 7 medan popupen fortfarande ritade
            // rad 3 starkt — stängningen är uppskjuten till pump-svansen, så
            // det fönstret är synligt. Invarianten är att det
            // provisoriska valet ÄR det som ritas starkt; commit läser det.
            setProvisionalIndex(row);
            d_->commit(provisionalIndex());   // ÄGARENS onChange, en gång
        }
    }

    bool mouseWheel(const Ctx& c, float x, float y, float wx, float wy) override
    {
        ListBox::mouseWheel(c, x, y, wx, wy);
        return true;   // konsumera ALLTID — popupen bubblar aldrig
    }

    // Tangenterna ägs av DROPDOWNEN (den har fokus, inte popupen) — se
    // Dropdown::keyDown. Att ärva ListBoxens tabell hade gett en ANDRA väg
    // in i samma tillstånd, med `selectFromUser` och ett `onChange` på fel
    // widget.
    bool keyDown(gfx::Key, std::uint32_t) override { return false; }

private:
    Dropdown* d_;
};

// Popup-fönstret. Chrome::None (ramlöst — ingen drag/resize), popup-
// attributet ger noActivate + stäng-vid-klick-utanför/fokustapp via
// App::closePopups. Ägs av Dropdownen; visas ägt av Dropdownens fönster
// (kaskad-stängs med det).
class Dropdown::ListWindow final : public AppWindow {
public:
    explicit ListWindow(Dropdown& d)
        : AppWindow("", 100, 40, gfx::Chrome::None)
    {
        setPopup(true);
        rows_       = add<DropdownRows>(d);
        rows_->dock = Dock::Fill;
        onClose     = [&d](Widget*, CloseArgs&) { d.popupClosed(); };
    }

    DropdownRows* rows() { return rows_; }

private:
    DropdownRows* rows_ = nullptr;
};

// ------------------------------------------------------------- Dropdown

Dropdown::Dropdown()  = default;   // ListWindow komplett här — se headern
Dropdown::~Dropdown() = default;

// STYCKEFORMEN — kontraktet och skälet står i headern.
// Definierade HÄR och inte inline: `close()` rör `ListWindow`, som är
// ofullständig i headern.
void Dropdown::addItem(std::string text)
{
    items_.push_back(std::move(text));
    close();          // popupen bär en EGEN kopia — se setItems
    invalidate();
}

void Dropdown::setItem(int index, std::string text)
{
    if (index < 0 || std::size_t(index) >= items_.size()) {
        gfx::log("Dropdown(%s): setItem(%d) utanför intervallet (%zu rader) "
                 "— ignoreras", name.c_str(), index, items_.size());
        return;
    }
    if (items_[std::size_t(index)] == text)
        return;                       // hård regel 1: invalidera vid ÄNDRING
    items_[std::size_t(index)] = std::move(text);
    close();
    invalidate();
}

// TEXTSTORLEKEN — regeln i `textSizeValue`. En öppen popup har
// sin storlek och sina rader från öppningen, så den stängs (setItems).
void Dropdown::setTextSize(Length size)
{
    const std::optional<Length> s = textSizeValue(size, "Dropdown", name);   // lang_gate:rad typnamnet i loggraden
    if (!s || (s->value == textSize_.value && s->unit == textSize_.unit))
        return;
    textSize_ = *s;
    close();
    invalidate();
}

Ctx Dropdown::textCtx(const Ctx& c) const
{
    return sizedText(c, textSize_);
}

bool Dropdown::setProp(const std::string& key, const std::string& value)
{
    if (key == "items") {
        // ';'-separerad lista — builderns/gui_loaderns textform.
        std::vector<std::string> nya;
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto sep = value.find(';', start);
            if (sep == std::string::npos) {
                if (start < value.size())
                    nya.push_back(value.substr(start));
                break;
            }
            nya.push_back(value.substr(start, sep - start));
            start = sep + 1;
        }
        setItems(std::move(nya));
        return true;
    }
    if (key == "selectedIndex") {
        // Fältet heter `selectedIndex` överallt (`kSetterFields`, modellen,
        // egenskapspanelen), så `typDd.setSelectedIndex(1);` i en `.glazeui`
        // utförs av previewn. Något alias `selected` finns inte: regel 4
        // gäller vad BUILDERN bevarar, inte vilka runtime-nycklar som finns.
        setSelectedIndex(std::atoi(value.c_str()));   // oparsbart ⇒ 0
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
    return Widget::setProp(key, value);
}

// Textytan mellan vänsterluften och pilen, med fältets luft också på pilens
// sida — det en centrerad eller högerjusterad text ställer sig inom. En
// vänsterjusterad text börjar där den alltid börjat; bredden rör den inte.
gfx::Rectf Dropdown::textRect(const Ctx& c, const gfx::Rectf& chev) const
{
    const gfx::Rectf b  = c.bounds();
    const float      x0 = b.x + c.style->fieldPadX;
    return gfx::Rectf{x0, b.y,
                      std::max(0.0f, chev.x - c.style->fieldPadX - x0), b.h};
}

gfx::Sizef Dropdown::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // Samma recept som TextInput — bredden ur layouten med fallback,
        // autohöjden ur fonten. Medvetet EGNA tal: de två fälten får
        // divergera i en framtida design.
        //
        // `Draw::lineHeight` och INTE `c.font->height()`:
        // fonten talar FYSISKA px, mätvägen LOGISKA. Draw är bryggan.
        // Radhöjden är TEXTSTORLEKENS, luften fältets.
        const auto& m = *c.style;
        return gfx::Sizef{width > 0.0f ? width : m.fieldDefaultW,
                          height > 0.0f ? height
                                        : Draw::lineHeight(textCtx(c)) +
                                              2.0f * m.fieldPadY};
    }
    }
}

void Dropdown::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const Ctx        t   = textCtx(c);   // fältets text i dess storlek

    // Chevronens ruta: högerkanten in en padding, v-centrerad.
    const gfx::Rectf chev{b.x + b.w - m.fieldPadX - 2.0f * m.chevronW,
                          b.y + b.h / 2.0f - m.chevronH,
                          2.0f * m.chevronW, 2.0f * m.chevronH};

    switch (c.ui) {
    // GLAZE — ett FÄLT, och fält LAMINERAS INTE —
    // exakt samma recept och samma skäl som TextInput. Konturen är den
    // BESTÄMDA `buttonBorder`, och chevronen står i `textDim` som en lugn
    // markör: den är inte en handling utan en upplysning om att listan finns.
    //
    // De fyra tillstånden (princip 7):
    //   vila       yta `panel`, kontur `buttonBorder`, chevron `textDim`
    //   hover      konturen går till `accent`
    //   tryck      konturen står kvar i `accent` så länge listan är ÖPPEN —
    //              inte bara medan knappen hålls
    //   fokus      fristående ring utanför kanten
    //   inaktiv    kontur `border`, text och chevron dimmade
    case UI::Glaze:
        Draw::rect(c, b, m.cornerRadius, col.panel,
                   Pen{!isEnabled()             ? col.border
                       : isOpen() || hovered()  ? col.accent
                                                : col.buttonBorder,
                       m.borderWidth});

        Draw::clip(c, {b.x + m.fieldPadX, b.y, chev.x - b.x - m.fieldPadX,
                       b.h});
        Draw::text(t, textRect(c, chev), selectedText(), textColor(c),
                   align_);
        Draw::endClip(c);

        Draw::chevron(c, chev, isOpen() ? Dir::Up : Dir::Down,
                      Pen{isEnabled() ? col.textDim : col.border, 0.0f});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        m.cornerRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;

    // MINIMALISTIC — "Outline": BARA en kant, ingen `panel`-fyllning
    // — samma beslut som TextInput, och de
    // två måste tas tillsammans: två fält bredvid varandra där bara det ena
    // har yta läser som att det andra är avstängt.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ingen fyllning, kant `buttonBorder`, chevron hårlinje
    //   hover      kanten går till `accent`
    //   tryck      kanten står kvar i `accent` medan listan är ÖPPEN
    //   fokus      fristående ring (halv tjocklek)
    //   inaktiv    kant `border`, text och chevron dimmade
    case UI::Minimalistic:
        // FÄLTYTAN FYLLS I VILA — se button.cpp. Annars vore en stängd
        // lista osynlig utom sin kant och sin chevron. `panel` är samma yta som TextInput
        // använder: de två ÄR samma sorts fält och ska se lika ut.
        Draw::rect(c, b, m.cornerRadius,
                   isEnabled() ? col.panel : col.buttonDisabled);

        Draw::frame(c, b, m.cornerRadius,
                    Pen{!isEnabled()            ? col.border
                        : isOpen() || hovered() ? col.accent
                                                : col.buttonBorder,
                        m.borderWidth});

        Draw::clip(c, {b.x + m.fieldPadX, b.y, chev.x - b.x - m.fieldPadX,
                       b.h});
        Draw::text(t, textRect(c, chev), selectedText(), textColor(c),
                   align_);
        Draw::endClip(c);

        Draw::chevron(c, chev, isOpen() ? Dir::Up : Dir::Down,
                      Pen{isEnabled() ? col.textDim : col.border, 0.0f});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        m.cornerRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;

    // SLATE — ett FÄLT: skrivytan är `panel` och
    // KANTEN bär tillståndet. Ytan är inte försänkt — om fälten
    // ska bli försänkta (`appWorkspace`) är en öppen fråga, och den
    // gäller TextInput och Dropdown tillsammans.
    //
    // De fyra tillstånden (princip 7):
    //   vila       yta `panel`, hårlinje `border`
    //   hover      kanten `accent` (hotBorder)
    //   tryck      kanten `accent` medan listan är ÖPPEN — det är
    //              dropdownens enda "tryckta" läge och det varar tills
    //              popupen stängs, inte bara medan knappen hålls
    //   fokus      fristående ring utanför kanten
    //   inaktiv    dimmad text och chevron; kanten faller till `border`
    case UI::Slate:
    default:
        // Fältet: TextInput-receptet (panel-yta, tillståndet i kanten).
        // Öppen lista = kanten står kvar i accenten även när musen lämnat —
        // annars slocknade kontrollen så fort pekaren gick ner i listan.
        Draw::rect(c, b, m.cornerRadius, col.panel,
                   Pen{isOpen() && isEnabled() ? col.accent : hotBorder(c),
                       m.borderWidth});

        // Vald text, klippt mot fältet (chevron-zonen exkluderad).
        Draw::clip(c, {b.x + m.fieldPadX, b.y, chev.x - b.x - m.fieldPadX,
                       b.h});
        Draw::text(t, textRect(c, chev), selectedText(), textColor(c),
                   align_);
        Draw::endClip(c);

        // Chevronen: ∨ stängd, ∧ öppen — textDim som lugn accent.
        Draw::chevron(c, chev, isOpen() ? Dir::Up : Dir::Down,
                      Pen{isEnabled() ? col.textDim : col.border, 0.0f});

        if (focused() && isEnabled())
            Draw::frame(c, Draw::inset(b, -m.focusOffset),
                        m.cornerRadius + m.focusOffset,
                        Pen{col.accent, m.focusWidth});
        break;
    }
}

void Dropdown::mouseDown(const Ctx&, float, float)
{
    // Toggle. OBS ordningen vid klick-medan-öppen: App::closePopups har
    // redan köat stängningen (deferred) när trycket når oss — open_ är
    // fortfarande true, så vägen blir close() (idempotent), inte en
    // omedelbar återöppning. Det ÄR toggle-känslan.
    if (open_)
        close();
    else
        open();
    invalidate();
}

bool Dropdown::keyDown(gfx::Key k, std::uint32_t)
{
    const int n = int(items_.size());
    if (!open_) {
        // Stängd: pilarna byter val direkt (WinForms), SPACE öppnar. Enter
        // öppnar inte — den bubblar till
        // fönstrets standardknapp; öppen är den listans (nedan).
        if (k == gfx::Key::Up || k == gfx::Key::Down) {
            if (n == 0)
                return true;
            const int steg = k == gfx::Key::Down ? 1 : -1;
            const int nytt = std::clamp(selectedIndex_ + steg, 0, n - 1);
            if (nytt != selectedIndex_) {
                selectedIndex_ = nytt;
                invalidate();
                firePair(onChange, onCommit);   // samma val
            }
            return true;
        }
        if (k == gfx::Key::Space) {
            open();
            return true;
        }
        return false;
    }
    // Öppen: pilarna flyttar det PROVISORISKA valet i popup-listan (som
    // äger det), Enter/Space committar det, Escape stänger utan ändring.
    // Tangenterna bor HÄR och inte i popupen: fokus ligger på dropdownen,
    // och en andra tabell i listan hade gett en andra väg in i samma
    // tillstånd.
    const int prov = provisionalRow();
    switch (k) {
    case gfx::Key::Up:
        setProvisional(std::clamp(prov - 1, 0, n - 1));
        return true;
    case gfx::Key::Down:
        setProvisional(std::clamp(prov + 1, 0, n - 1));
        return true;
    case gfx::Key::Home:
        setProvisional(0);
        return true;
    case gfx::Key::End:
        setProvisional(n - 1);
        return true;
    case gfx::Key::Return:
    case gfx::Key::Space:
        if (prov >= 0)
            commit(prov);
        else
            close();
        return true;
    case gfx::Key::Escape:
        close();
        invalidate();
        return true;
    default:
        return false;
    }
}

void Dropdown::open()
{
    if (open_ || items_.empty() || !appWindow())
        return;
    openAt(behaviorCtx());
}

void Dropdown::openAt(const Ctx& c)
{
    const gfx::Rectf r   = resolve(c);
    gfx::Window&     win = appWindow()->window();
    if (!list_)
        list_ = std::make_unique<ListWindow>(*this);

    // Popup-kontraktet: ankarrectet
    // är kontrollens rect i ägarens KLIENT-FYSISKA px (det vi redan har i
    // r) och listans storlek sätts i LOGISKA px — all skärmrymds-aritmetik
    // (flip uppåt vid skärmkant, klamp i sidled) bor i BACKENDEN
    // (Wayland: xdg_positioner; D3D11/Metal/X11: clientToScreen/screenArea
    // internt).
    // Listan är kontrollens bredd; höjden = raderna, kapad vid stilens dropMaxRows
    // (resten hjulscrollas — radwidgetens offset).
    const int   rows = std::min(int(items_.size()), c.style->dropMaxRows);
    const float lw   = Draw::bounds(c, r).w;   // kontrollens bredd, logisk

    // Popup-listan har en EGEN kopia av raderna (den är en ListBox), och
    // det PROVISORISKA valet startar på det committade — satt med den
    // programmatiska vägen, så öppnandet aldrig fyrar onChange.
    // Raderna i dropdownens TEXTSTORLEK — och popupens höjd ur
    // listans egen radhöjd, så de aldrig blir oense.
    list_->rows()->setItems(items_);
    list_->rows()->setTextSize(textSize_);
    list_->rows()->setProvisional(selectedIndex_);
    list_->rows()->setScroll(0.0f, 0.0f);   // scrollen nollas per öppning
    list_->setPopupAnchor(r);
    list_->setClientSize(
        int(std::lround(lw)),
        int(std::lround(float(std::max(rows, 1)) *
                        list_->rows()->rowHeight(c))));
    open_  = true;
    list_->show(*appWindow());
    // Fail-safe: misslyckas backendens fönsterinit fastnar open_ sant utan
    // popup — close() blir no-op (listan är inte öppen ⇒ inget onClose ⇒
    // aldrig popupClosed) och dropdownen är permanent död. Städa direkt.
    if (!list_->isOpen()) {
        gfx::log("Dropdown: popup-fönstret kom aldrig upp — stänger av");
        popupClosed();
        return;
    }
    list_->rows()->ensureVisible(selectedIndex_);
    // Öppnades vi mitt i ett tryck (press-och-dra) håller backenden
    // mus-capture på ägarfönstret — alla mus-events routas dit tills
    // släppet och listan ser ingen hover. Släpp capturen och nolla vårt
    // tryck-utseende: släpps knappen över popupen når MouseButtonUp
    // aldrig ägarfönstret, och pressed_ skulle annars fastna.
    win.releaseMouseCapture();
    pressed_ = false;
    invalidate();
}

void Dropdown::close()
{
    if (open_ && list_)
        list_->close();   // uppskjuten (pump-svansen) → onClose → popupClosed
}

void Dropdown::commit(int row)
{
    if (row >= 0 && std::size_t(row) < items_.size() &&
        row != selectedIndex_) {
        selectedIndex_ = row;
        // onChange och onCommit för samma val. En handler som rev
        // eller flyttade listan lämnar ingenting att stänga här.
        if (!firePair(onChange, onCommit))
            return;
    }
    close();
    invalidate();
}

// DET PROVISORISKA VALET bor i popup-listan, inte här — Dropdown når det
// via sin pekare. Läsaren svarar -1 när listan är stängd: då FINNS inget
// provisoriskt val, och det är exakt vad -1 betyder.
int Dropdown::provisionalRow() const
{
    return open_ && list_ ? list_->rows()->provisional() : -1;
}

void Dropdown::setProvisional(int row)
{
    if (!open_ || !list_)
        return;
    DropdownRows* r = list_->rows();
    if (row == r->provisional())
        return;
    r->setProvisional(row);    // programmatisk setter — invaliderar, noll event
    r->ensureVisible(row);
}

void Dropdown::popupClosed()
{
    open_ = false;
    invalidate();   // chevron/tillstånd ritas om i ägarfönstret
}

} // namespace glaze
