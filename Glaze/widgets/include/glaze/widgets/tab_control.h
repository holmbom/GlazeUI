#pragma once
#include <vector>

#include "glaze/widget.h"
#include "glaze/widgets/tab_page.h"

// TabControl — FLIKARNA (WinForms TabControl,
// wxNotebook). En rad flikar överst och den VALDA sidans innehåll under.
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/tab_control.cpp med switch (c.ui) (hård regel 3).
//
// SIDORNA ÄR BARNEN — `TabPage`, i adopt-ordning, och fliktiteln är sidans
// `text`. WinForms form, och skälet är regel 8: ett kedjat
// `flikar.page(0).adopt(x)` går inte att emittera i grammatiken, medan
// `flikar.adopt(sida);` + `sida.adopt(x);` är trädbyggets egna satser. Ett
// barn som inte är en `TabPage` är ingen flik: det loggas, döljs och
// placeras inte (WinForms kastar ett undantag).
//
// SIDORNAS KOLLEKTIONSVOKABULÄR (WinForms `TabPages.Add(string)`), den DYNAMISKA vägen bredvid
// medlemssidorna:
//   `addPage(text)`     en ny ÄGD sida sist, och referensen tillbaka så att
//                       koden kan fylla den (`flikar.addPage("Logg")
//                       .add<glaze::TextArea>()`); `unadopt` förstör den, som
//                       varje ägt barn
//   `setPage(i, text)`  sidans EGNA data, rubriken — innehållet rörs inte
//                       (trädets `setNode`: barnen står kvar)
//   `setPages(lista)`   ALLA sidor byts: varje sida lämnar (en ägd förstörs,
//                       en lånad släpps — `unadopt`), och en ny ägd sida per
//                       text läggs till. ATOMISK: en sida med en öppen popup
//                       eller meny prövas FÖRE mutationen, och då händer
//                       ingenting (logg). Barn som inte är sidor rörs inte,
//                       och det LAGRADE valet står kvar (`setSelectedIndex`
//                       före `setPages` gäller efteråt).
// Index utanför ⇒ `gfx::log` + no-op. I `.glazeui` är de anropssatser och
// står EFTER trädbygget (`dockPanel`s regel): previewn bygger trädet före
// satserna, så en `addPage` före `flikar.adopt(…)` hade hamnat sist i
// previewn och först i appen.
//
// FLIKKONTROLLEN PLACERAR SIDORNA SJÄLV (`placesChildren`, Bar- och
// delarprejudikatet — sidornas x/y/dock ignoreras): den valda sidan fyller
// ytan under flikraden, innanför ramen. Den ÄGER också deras SYNLIGHET —
// den valda syns och de andra är dolda, så en dold sidas kontroller varken
// ritas, träffas eller nås med Tab. Barnen klipps mot ytan.
//
// VALET är ett INDEX bland sidorna. `setSelectedIndex` lagrar talet som det
// är och `selectedIndex()` svarar med det EFFEKTIVA — klampat till sidorna,
// −1 utan sidor — så satsordningen i en `.glazeui` aldrig avgör:
// `flikar.setSelectedIndex(1);` står i widgetens block, FÖRE trädbyggets
// `flikar.adopt(…)`, och gäller när den andra sidan kommer (talfältets
// `value()`-prejudikat). Ett index bortom sidorna visar den sista; ett
// negativt loggas och ignoreras.
//
// SIDORNA TAS BORT med trädets egna `unadopt`/`releaseOwned`, och VALET
// FÖLJER SIDAN: tas en sida FÖRE den valda bort flyttar indexet med, tas den
// VALDA bort väljs den som glider in på dess plats — den till höger, eller
// den till vänster när den var sist (Qt:s `SelectRightTab`). Inget event —
// borttagningen är appens, inte användarens.
//
// ANVÄNDAREN BYTER FLIK med ett tryck på fliken, och med tangentbordet:
//   flikkontrollen har fokus   Vänster/Höger (stannar i kanten),
//                              Home/End, Ctrl+Tab/Ctrl+Shift+Tab och
//                              Ctrl+PageDown/PageUp (runt om)
//   fokus står i sidan         Ctrl+Tab/Ctrl+Shift+Tab och
//                              Ctrl+PageDown/PageUp — fokus flyttar då till
//                              den nya sidans första tabbstopp, eller till
//                              flikkontrollen när sidan saknar ett (WinForms)
// Tangenterna i sidan når flikkontrollen genom `descendantKeyDown`
// (widget.h) — en tangent den fokuserade widgeten tackade nej till. En
// kontroll som själv använder Ctrl+PageDown (listvyns flerval) behåller den.
// Flikkontrollen är ETT tabbstopp, och den valda sidans kontroller följer
// efter den. `onChange` fyras EN gång per användarbyte — aldrig av
// `setSelectedIndex` eller av en borttagning.
//
// FOKUS STANNAR ALDRIG I EN DOLD SIDA: göms sidan som håller fokus (ett
// programmatiskt byte) flyttar fokus till flikkontrollen.
//
// STÄNGBARA FLIKAR (`setClosable(true)`, Qt:s `tabsClosable`): varje flik får
// en stängknapp (✕, synlig på den valda och den pekaren står över), och ett
// klick på den fyrar `onTabClose` med flikens index i args. FLIKKONTROLLEN
// TAR ALDRIG BORT SIDAN SJÄLV — sidan kan vara en värde-medlem, och bara
// appen vet om den ska bort, gömmas undan eller fråga först:
//     flikar.onTabClose = [this](glaze::Widget*, glaze::TabCloseArgs& e) {
//         flikar.unadopt(*flikar.page(e.index));
//     };
// Ett tryck på stängknappen väljer inte fliken, och dras pekaren bort innan
// släppet händer ingenting (en knapps semantik).
//
// RADEN SOM INTE RYMS: flikarna krymper lika långt mot `Style::tabMinW` och
// rubrikerna kortas med ellips; räcker inte ens det förskjuts raden så att
// den VALDA fliken alltid syns hel. Ingen rullning med pilknappar, ingen
// andra rad.
//
// SKRIVNA BEGRÄNSNINGAR: ingen autostorlek (Panel-beslutet — flikkontrollen
// är en placeringsyta appen dimensionerar), flikarna står alltid överst,
// inga ikoner i flikarna, ingen omordning med musen, inget eget event när
// en flik stängs med tangentbordet (appen binder sin Ctrl+W).

namespace glaze {

// Args till `onTabClose` — VILKEN flik användaren bad att få stänga.
struct TabCloseArgs : EventArgs {
    int index = -1;   // sidans index bland flikkontrollens sidor
};

class TabControl : public Widget {
public:
    TabControl() = default;

    // ---------------------------------------------------------- SIDORNA
    // Barnen som är `TabPage`, i adopt-ordning. `page` svarar nullptr
    // utanför; `pageIndex` −1 för en widget som inte är en sida här.
    int      pageCount() const;
    TabPage* page(int index) const;
    int      pageIndex(const Widget& page) const;

    // KOLLEKTIONSVOKABULÄREN (se headern). Programmatiskt: inget event, och
    // valet följer sidorna som vid `adopt`/`unadopt`.
    TabPage& addPage(TrText text = {});
    void     setPage(int index, TrText text);
    void     setPages(std::vector<TrText> texts);

    // ------------------------------------------------------------ VALET
    // TILLSTÅNDET är PRIVAT och byts via SETTRAR —
    // programmatisk sättning fyrar ALDRIG `onChange` (se headern ovan för
    // index utanför sidorna).
    void     setSelectedIndex(int index);
    int      selectedIndex() const;   // det EFFEKTIVA — −1 utan sidor
    TabPage* selectedPage() const;    // nullptr utan sidor

    // STÄNGKNAPPARNA (se headern).
    void setClosable(bool on);
    bool isClosable() const { return closable_; }

    // ANVÄNDAREN bytte flik — läs den nya ur sender (`selectedIndex()`).
    Handler<ChangeArgs>   onChange;
    // ANVÄNDAREN bad att få stänga fliken `e.index` — sidan tas bort av appen.
    Handler<TabCloseArgs> onTabClose;

    bool focusable() const override { return true; }

    // setProp-egenskaper: selectedIndex, closable; resten tar basen.
    bool setProp(const std::string& key, const std::string& value) override;

    // Sidorna placeras och klipps av flikkontrollen (Widget-krokarna,
    // Bar-formen). `placeChild` ger den valda sidan ytan under flikraden och
    // de dolda en tom rekt.
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;
    bool       clipsChildren() const override { return true; }
    gfx::Rectf clipRect(const Ctx& c) const override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    bool descendantKeyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseDrag(const Ctx& c, float mx, float my) override;
    bool mouseUp(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;

    void childAdded(Widget& child) override;
    void childRemoved(Widget& child) override;

    // ANVÄNDARVÄGEN — den ENDA som fyrar `onChange` (ListBoxens
    // `selectFromUser`-prejudikat; protected så en subklass, och en grind,
    // når den). `intoPage` = fokus stod i den gamla sidan (Ctrl+Tab
    // inifrån) och ska till den nya sidans första tabbstopp. Svarar true när
    // fliken byttes.
    bool selectFromUser(int index, bool intoPage);

private:
    // GEOMETRIN — EN sanning i widget-LOKALA LOGISKA px, och den tar ytan
    // `b` som argument (gruppramens form): ritningen skickar `c.bounds()`,
    // placeringen och musvägen `Draw::bounds(c, resolve(c))`.
    //   flikraden    överst, `tabHeaderH` hög (kapad mot ytan)
    //   visningsytan resten — flikkontrollens yta och ram
    //   sidytan      visningsytan minus ramens indrag (`panelInset`)
    //   flikarna     en rekt per sida i flikraden, förskjutna så att den
    //                valda syns (se headern)
    gfx::Rectf              stripRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf              frameRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf              pageRect(const Ctx& c, const gfx::Rectf& b) const;
    std::vector<gfx::Rectf> tabRects(const Ctx& c, const gfx::Rectf& b) const;
    // Fliken `t`s stängknapp (kvadraten längst till höger) och rubrikens yta.
    gfx::Rectf closeRect(const Ctx& c, const gfx::Rectf& t) const;
    gfx::Rectf titleRect(const Ctx& c, const gfx::Rectf& t) const;

    // TRÄFFEN — en FYSISK muspunkt mot geometrin ovan. −1 = ingen.
    int tabAt(const Ctx& c, float mx, float my) const;
    int closeAt(const Ctx& c, float mx, float my) const;

    // SYNLIGHETEN och FOKUS — den valda sidan syns, de andra göms, och fokus
    // i en sida som göms flyttar (`intoPage`: till den nya sidans första
    // tabbstopp, annars till flikkontrollen).
    void syncPages(bool intoPage);
    // Nästa/föregående flik runt om (Ctrl+Tab) — samma index när det bara
    // finns en.
    int cycled(int dir) const;
    // Musens flik-index nollas när sidorna ändras — de gällde den gamla raden.
    void glömMusen();

    // Det LAGRADE valet (se headern) och sidan som VISAS — den sista behövs
    // när en sida tas bort: var det den, ska valet stå kvar på platsen, annars
    // följer det sidan. Pekaren jämförs, den dereferenseras aldrig.
    int           selectedIndex_ = 0;
    const Widget* shown_         = nullptr;
    bool          closable_      = false;

    // Musen — ritkosmetik, invaliderat bara vid ändring (regel 1).
    int hoverTab_   = -1;   // fliken pekaren står över
    int hoverClose_ = -1;   // fliken vars stängknapp pekaren står över
    int pressClose_ = -1;   // stängknappen trycket började på
    bool pressStill_ = false;   // pekaren står kvar över den
};

} // namespace glaze
