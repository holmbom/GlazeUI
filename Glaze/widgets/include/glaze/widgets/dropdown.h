#pragma once
#include <memory>
#include <vector>

#include "glaze/canvas/geometry.h"   // Length — textstorleken
#include "glaze/draw.h"     // Align — fältets justering nedan
#include "glaze/widget.h"

// Dropdown — vallista à la WinForms ComboBox (DropDownList-läget: värdet
// väljs, skrivs inte). MÖNSTER: widgeten ritar SIG SJÄLV — draw()/
// contentSize() bor i src/dropdown.cpp med switch (c.ui), och där
// ritas BÅDE den stängda kontrollen och popup-listans rader (hård regel
// 3). Allt BETEENDE (öppna/stänga, tangenter, val) bor där också.
//
// Listan är ett RIKTIGT popup-fönster (Chrome::None + setPopup) som ägs
// lat av widgeten — den kan sticka ut utanför ägarfönstret och vänder
// uppåt när skärmens arbetsyta tar slut nedåt. Placeringen går via
// popup-kontraktet (AppWindow::setPopupAnchor — flip/klamp bor i
// backenden; Wayland saknar skärmpositioner helt). Aldrig för hand.
// Raderna är DATA (ritas av en `glaze::ListBox`-subklass i popup-fönstrets
// rot — samma widget appar använder fristående), inte
// widgets. Klick utanför/fokustapp stängs av App:s popup-semantik
// (closePopups); widgeten nollar sitt öppet-läge i popupens onClose.
//
// Värden läses ur SENDER (regel 6): dd.selectedIndex/selectedText(), inte
// ur args. onChange fyras vid varje ANVÄNDAR-ändring av valet (klick på
// rad, pil-upp/ner i stängt läge, Enter på markerad rad) — programmatisk
// sättning avfyrar ALDRIG något.
//
// TANGENTERNA: STÄNGD öppnar Space, och pilarna byter valet direkt; Enter
// är inte den stängda listans utan bubblar
// till fönstrets standardknapp. ÖPPEN committar Enter/Space markeringen och
// Escape stänger — före fönstrets avbrytknapp.
//
// TILLSTÅNDSMODELLEN: dropdownen
// äger det COMMITTADE valet, popup-listan det PROVISORISKA. Det finns
// alltså EN väg som fyrar `onChange` — `commit()` — och popupen går aldrig
// genom ListBoxens egen användarväg. Dubbelfyrning är omöjlig by design.

namespace glaze {

class Dropdown : public Widget {
public:
    // BÅDA out-of-line (dropdown.cpp): även en defaultad inline-ctor
    // kräver medlemmarnas destruktorer (exception-städningen) — och
    // ListWindow är ofullständig här.
    Dropdown();
    ~Dropdown() override;


    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    void setItems(std::vector<std::string> v)
    {
        if (items_ == v)
            return;
        items_ = std::move(v);
        // STÄNG en öppen popup: listan har en EGEN kopia av raderna
        // (den är en `ListBox`), och en popup som visar den
        // GAMLA listan medan fältet visar den nya är värre än en stängd.
        // Stängning är det säkra svaret.
        close();
        invalidate();
    }
    const std::vector<std::string>& items() const { return items_; }

    // ------------------------------------------ STYCKEFORMEN
    //
    // KOLLEKTIONSVOKABULÄREN är GEMENSAM för alla listbärande komponenter
    // (regel 8): `addX` · `setX(index, …)` · `setXs(lista)`.
    // IDENTISKA namn och identisk semantik som `ListBox` — en form att
    // lära sig, inte tre. `PropertyGrid` bär samma vokabulär som
    // `addRow`/`setRow`/`setRows`.
    //
    // Båda STÄNGER en öppen popup av exakt samma skäl som `setItems`:
    // popup-listan har en EGEN kopia av raderna, och en popup som visar
    // den GAMLA listan medan fältet visar den nya är värre än en stängd.
    // `setItem` utanför intervallet ⇒ logg + no-op, aldrig tyst tillväxt.
    void addItem(std::string text);
    void setItem(int index, std::string text);

    // Programmatisk sättning avfyrar ALDRIG onChange.
    void setSelectedIndex(int i)
    {
        if (selectedIndex_ == i)
            return;
        selectedIndex_ = i;
        invalidate();
    }
    int selectedIndex() const { return selectedIndex_; }

    std::string selectedText() const
    {
        return (selectedIndex_ >= 0 &&
                std::size_t(selectedIndex_) < items_.size())
                   ? items_[std::size_t(selectedIndex_)]
                   : std::string();
    }

    // JUSTERINGEN av fältets text — samma fält och samma namn som
    // på `Label` och `TextInput` (regel 6). Den behövs av datagridens
    // val-kolumn: en cell i en centrerad eller högerjusterad kolumn ska
    // inte hoppa till vänster när den börjar redigeras. Gäller det STÄNGDA
    // fältet — raderna i popupen är en lista och står som listor står.
    void setAlign(Align a)
    {
        if (align_ == a)
            return;
        align_ = a;
        invalidate();
    }
    Align align() const { return align_; }

    // TEXTSTORLEKEN — `TextInput::setTextSize`s form och regel:
    // `glaze::px(…)` eller `glaze::mm(…)`, 0 = stilens. Fältets text och
    // AUTO-höjd följer, och POPUPENS rader likaså (`ListBox::setTextSize` —
    // listan och fältet har samma storlek). Ett byte stänger en öppen popup,
    // som `setItems` (den kan inte växa efter att den visats).
    void   setTextSize(Length size);
    Length textSize() const { return textSize_; }

    // ANVÄNDAREN valde ett annat värde — ur listan, eller med pilarna på en
    // stängd dropdown. `onCommit` fyras direkt efter för samma val:
    // ett val ur en lista har inget halvfärdigt läge.
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;

    bool focusable() const override { return true; }

    // setProp-egenskaper: items ("a;b;c"), selectedIndex, align.
    bool setProp(const std::string& key, const std::string& value) override;

    bool isOpen() const { return open_; }
    void open();                        // no-op utan items/ägarfönster
    void close();

    // Det PROVISORISKA valet i en ÖPPEN lista — markören användaren flyttar
    // med mus eller pil innan hen committar. **-1 när listan är stängd**:
    // då finns inget provisoriskt val alls. Värdet ÄGS av popup-listan
    // och lagras aldrig här: ett eget hover-fält vore en dubblering, för
    // markören skulle byta tillståndskälla beroende på inmatningsenhet.
    int provisionalRow() const;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;
    void       mouseDown(const Ctx& c, float mx, float my) override;
    void       focusLost() override { close(); }

    // BORTTAGNINGENS DENY-CHECK: en dropdown med
    // ÖPPEN popup får inte lossas ur trädet. Popupen är ett eget
    // OS-fönster vars stängning är UPPSKJUTEN till pump-svansen, så en
    // synkron rivning härifrån hade dragit in hela fönsterlivscykeln i ett
    // widget. Anroparen stänger först och låter pumpen gå ett varv.
    bool       hasOpenPopup() const override { return isOpen(); }

private:
    class ListWindow;                   // popup-fönstret (dropdown.cpp)
    friend class DropdownRows;          // interna listwidgeten (dropdown.cpp)

    void openAt(const Ctx& c);
    void commit(int row);               // sätt + onChange + close
    void setProvisional(int row);       // markören i popupen + ensureVisible
    void popupClosed();                 // ur popupens onClose

    // Fältets textyta: luften till vänster, och lika mycket luft till
    // vänster om pilen — det en justerad text ställer sig inom.
    gfx::Rectf textRect(const Ctx& c, const gfx::Rectf& chev) const;
    // Ctx:t för fältets TEXT i dess storlek — bara för texten.
    Ctx textCtx(const Ctx& c) const;

    std::unique_ptr<ListWindow> list_;  // lat — skapas vid första open
    std::vector<std::string> items_;
    int    selectedIndex_ = -1;         // -1 = inget valt
    bool   open_  = false;
    Align  align_ = Align::Left;
    Length textSize_;                   // 0 = stilens
};

} // namespace glaze
