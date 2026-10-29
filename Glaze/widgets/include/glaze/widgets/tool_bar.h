#pragma once
#include "glaze/widgets/bar.h"

// ToolBar — VERKTYGSFÄLTET (wxAuiToolBar, WinForms
// ToolStrip). En `Bar` — samma flexmotor, samma remsa som menyraden och
// statusraden — med verktygsfältets EGNA mått ur stilen och Dock=Top som
// default (StatusBar-prejudikatet åt andra hållet, och WinForms
// ToolStrip.Dock).
//
// BARNEN ÄR RIKTIGA WIDGETS, inte data — och det är det som skiljer
// verktygsfältet från menyraden, vars rubriker ÄR data. Skälen, i ordning:
//
//   1. REGEL 8 gratis: `toolBar1.adopt(spara);` är redan en sats grammatiken
//      kan, och `spara.setImg(glaze::img("toolbar", "save"));` och
//      `spara.onClick = …;` är vanliga fält- och event-rader. En data-form
//      (`addTool(…)`) hade krävt nya applyCall-grenar, en handler-tabell à
//      la menyradens `setHandler` och en egen kollektionsvokabulär.
//   2. VAD SOM HELST får stå i remsan: en `Dropdown` för zoomnivån, ett
//      sökfält, en `Label` — wxAuiToolBar::AddControl är en vanlig
//      användning, och den kommer här utan en rad extra kod.
//   3. TABBSTOPPEN, som tvingade menyradens rubriker till data, är inget
//      problem här: verktygsknappen (`ToolButton`) är INTE fokuserbar.
//      Ett fält i remsan är det däremot, och ska vara det.
//
// En skiljelinje i remsan är en vanlig `Separator`; i en bar står den alltid
// TVÄRS baren (separator.h), så den följer med när fältet vänder.
//
// STÅENDE: med `dock` Left eller Right packar remsan sina
// barn uppifrån och ned (`Bar::isVertical`), och luften byter axel —
// `toolBarPadX` längs fältet, `toolBarPadY` tvärs.
//
// EGEN LÄNGD: är `width` (liggande) eller `height` (stående) ≤ 0 är
// fältets längd summan av greppet, luften, barnen och luckorna — inte 0. En
// dockad Top-remsa i ett vanligt fönster sträcks som vanligt (dockningen ger
// bredden); ett fält med `Dock::None` och bara vänsterankare syns nu.
//
// I EN DOCKNINGSYTA (`DockHost`, dock_host.h): ett fält som är värdens
// DIREKTA barn och har en kant blir medlem i ett BAND vid den kanten, och
// `band()` säger vilket (0 = yttersta; ett band utanför antalet blir ett nytt
// innersta). Fältet får ett GREPP i början (`toolGripW`, `Draw::grip`), och
// ett drag i greppet LOSSAR det vid tröskeln till sitt flytande fönster, som
// följer pekaren — värden visar med en simulerad
// platshållare var det hamnar i ett annat band, på en annan plats i bandet
// eller vid en annan kant, skriver vid släppet tillbaka `dock` och `band` och
// fyrar sitt `onChange`; släppt utan platshållare flyter fältet. Ett fält som
// inte får flyta (`setFloatable(false)`) står kvar medan platshållaren visas.
// `setAllowedEdges` begränsar kanterna användarens drag når.
// Ett fält utanför en värd har inget grepp.
//
// FLYTANDE (VS:s flytande verktygsfält) —
// `setFloating(true)` lyfter fältet ur sitt band till ett EGET fönster, som
// värden äger och visar ÄGT av sitt eget fönster (samma maskineri som en
// flytande `DockPanel`): överst en RUBRIK med fältets `text` och en
// stängknapp, under den fältet LIGGANDE i sin egen längd. Fönstret har FAST
// storlek och följer fältets innehåll — en knapp som läggs till gör det
// bredare. Fältet LIGGER alltid när det flyter (`isVertical()` false), och
// `dock`/`band()` står kvar som MINNET för var det dockas tillbaka: sist i
// band `band()` vid `dock` (adoptionsregeln — platsen i bandet minns
// inte, som i VS). Byts den EFFEKTIVA riktningen byts också `width` och
// `height`, så tjockleken förblir tjocklek och en satt längd förblir längd.
// Ett dubbelklick på GREPPET gör ett dockat fält flytande, ett dubbelklick på
// RUBRIKEN dockar det; ett drag i rubriken flyttar fönstret med pekaren och
// dockar fältet där värdens platshållare står vid släppet. Fönstret
// aktiveras aldrig. Rubrikens ✕ — och fönstrets egen stängning
// (Alt+F4) — DÖLJER fältet (`hide()`), och värdens `onChange` fyras; `show()`
// visar det igen. TILLSTÅNDET gäller direkt (`isFloating()`, värdens
// `layout()` med posten `ft=`) och flytten mellan fönstren görs i pumpens
// svans. Inget fönster skapas när värdens fönster är stängt eller offscreen
// (glaze-render, previewn) — där lämnar fältet bara sitt band. Ett flytande
// fält har inget grepp och ingen hårlinje (`drawsInnerEdge`).
// `setFloatable(false)` tar bort användarens vägar (dubbelklicket); appen
// kan alltid. `setFloatingPosition(x, y)` är fönstrets plats i LOGISKA px
// (`AppWindow::setLocation`s rymd); utan den öppnas fönstret mitt över
// ägaren, och på Wayland placerar kompositören (namnlisten syns där som
// flytthandtag, rubriken under den).
//
// ÖVERFLÖDET (WinForms `ToolStrip.CanOverflow`, VS:s verktygsfält) —
// ryms barnen inte i fältets längd DÖLJS de sista i layouten och en
// ÖVERFLÖDSKNAPP (⋯, `StockIcon::MoreHorizontal`, längden `toolOverflowW`)
// står i fältets ände; de dolda behåller sitt `visible`, som är appens. Ett
// tryck på knappen öppnar en `Menu` med de dolda barnen i sin ordning, byggd
// vid varje tryck:
//   * en VERKTYGSKNAPP blir en post med sin text (annars sitt verktygstips),
//     sin ikon, sitt kryss och sitt inaktiva läge, och posten KLICKAR knappen
//     (`ToolButton::performClick` — sender är knappen);
//   * en knapp med POSTER blir en undermeny som aktiverar knappens egna poster
//     med knappen som sender (en delad knapp först med sitt eget klick), och
//     dess `onDropDown` fyras innan menyn byggs;
//   * en `Separator` blir en skiljelinje (aldrig först, sist eller två i rad),
//     en `Spacer` ingenting;
//   * en INBÄDDAD KONTROLL (en `Dropdown`, ett sökfält) blir en post med sitt
//     verktygstips som text (annars ramverkets "Kontroll") som ger den FOKUS
//     — och ett barn i överflödet som har fokus VISAS i fältet, sist före
//     knappen och med så många av de andra dolda som det kräver, tills fokus
//     lämnar det. Samma regel gör att Tab når kontrollerna i överflödet. Ett
//     barn som inte kan ta fokus (en `Label`, en `Progress`) har ingen post.
// Fältets sista synliga barn före knappen är aldrig en separator. I ett band
// i en `DockHost` krymper fälten bakifrån ned till greppet, luften och
// knappen, så varje fält har sin knapp; ett fält i sin egen längd (flytande,
// eller med `Dock::None`) flödar aldrig över. `setOverflow(false)` ger det
// gamla — barnen rinner över och klipps. Knappen är INREDNING: den lossas
// inte (`canReleaseChild`), står inte i flödet och är inget tabbstopp.
//
// MÖNSTER: ritningen är Bars (ytan + hårlinjen mot innehållet) plus greppet,
// och placeringen Bars flex — det som är verktygsfältets eget är MÅTTEN:
// höjden (`toolBarH`), luften runt barnen (`toolBarPadX/Y`), luckan mellan
// dem (`toolGap`), greppet (`toolGripW`) och överflödsknappen
// (`toolOverflowW`). Det flytande fönstrets rubrik är ramverkets egen
// interna widget (src/dock/dock_float.h).

namespace glaze {

class DockHost;
class ToolButton;
struct MenuItem;

class ToolBar : public Bar {
public:
    // Dock=Top, och överflödsknappen som ett ägt barn (inredning).
    ToolBar();
    // Ett FLYTANDE fält som dör säger det till sin värd i samma ögonblick —
    // dess fönster släpper det innan det hinner rita, träffa eller ge fokus
    // till det igen (DockPanels regel).
    ~ToolBar() override;

    // TITELN i det flytande fönstrets rubrik — `text` som på varje widget
    // (regel 6), och den tar `glaze::tr(id)`. Tom = ramverkets "Verktygsfält"
    // (`glaze::str::DockToolBar`). Dockat syns den inte.
    void               setText(TrText t);
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // BANDET vid kanten i en dockningsyta — 0 är yttersta. Ett negativt
    // tal loggas och ändrar ingenting. Utanför en värd betyder det inget.
    // Värden läser det när fältet adopteras eller talet ändras, och skriver
    // tillbaka det PACKADE indexet efter en användarändring. När fältet flyter
    // är det minnet för bandet det dockas tillbaka till.
    void setBand(int b);
    int  band() const { return band_; }

    // FLYTANDE — se headern. Programmatiskt, så inget `onChange`.
    void setFloating(bool on);
    bool isFloating() const { return floating_; }
    // Användarens vägar till flytande (default true). Appen kan alltid.
    void setFloatable(bool on);
    bool isFloatable() const { return floatable_; }
    // KANTERNA användaren får docka fältet vid (Qt
    // `QToolBar::setAllowedAreas`) — förval alla fyra. Gäller ANVÄNDARENS
    // drag, som `setFloatable`: greppdraget, rubrikdraget och det burna
    // draget får ingen platshållare vid en otillåten kant, och ett buret fält
    // som släpps där flyter. Appen kan alltid (`dock`, `setLayout`). En kant
    // som inte är tillåten är inget mål — också fältets nuvarande, så en
    // omordning i sitt band vid en otillåten kant går då inte. `Fill`/`None`
    // i listan loggas och ignoreras; `{}` = ingen kant (fältet kan bara
    // flyta). `allowedEdges()` svarar i ordningen Left, Top, Right, Bottom.
    // I `.glazeui`: `verktyg.setAllowedEdges({glaze::Dock::Top, glaze::Dock::Bottom});`
    void              setAllowedEdges(std::vector<Dock> edges);
    std::vector<Dock> allowedEdges() const;
    bool              allowsEdge(Dock e) const;
    // Det flytande fönstrets plats i LOGISKA px — ändliga tal; annat loggas
    // och ändrar ingenting. Läsaren svarar med den senast satta — eller, när
    // fältet dockats tillbaka, fönstrets sista plats (värdens `layout()`
    // läser det levande fönstret).
    void        setFloatingPosition(float x, float y);
    gfx::Pointf floatingPosition() const { return floatPos_; }

    // STÅR fältet? Aldrig när det flyter — då är `dock` bara minnet.
    bool isVertical() const override;

    // ÖVERFLÖDET — se headern. På som förval; av rinner barnen över och
    // klipps som i en vanlig `Bar`, och fältet krymper inte i sitt band. Ett
    // byte ritar om fältet och lägger om banden; en öppen överflödsmeny
    // stängs när det slås av.
    void setOverflow(bool on);
    bool overflow() const { return overflowOn_; }

    // FLEXEN MED ÖVERFLÖDET: ryms barnen är det Bars flex rakt av (knappen står
    // utanför den och får en tom rekt); annars de som visas packade från
    // början, knappen i fältets ände och de dolda en tom rekt.
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    // Överflödsknappen är inredning (fältet håller en rå pekare till den) —
    // allt annat går att lossa som vanligt.
    bool canReleaseChild(const Widget& w) const override;

    // setProp-egenskaper: text, band (heltal ≥ 0), floating, floatable,
    // overflow (true/false); resten tar Bar/basen.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT titel — regel 8:s tr-väg. Övriga nycklar går
    // till basen (verktygstipset).
    bool setTextToken(const std::string& key, const TrText& value) override;

protected:
    // BARNENS YTA: remsan minus greppet (i början, i en värd) och stilens
    // luft (`toolBarPadX` längs fältet, `toolBarPadY` tvärs). Flexen packar
    // mot den, så varje barn får remsans INNERhöjd (stående: innerbredd).
    // Protected som Panels override: kroken är PUBLIK på `Widget`, och
    // åtkomsten prövas mot den statiska typen — previewvandringen och
    // grindarna når den därigenom.
    gfx::Rectf childArea(const Ctx& c) const override;

    // AXELREGELN med verktygsfältets egna default-mått: satt tjocklek
    // vinner, osatt blir `toolBarH` — eller tjockare, när en verktygsknapp
    // med större ikon kräver det (`autoThickness`); längden är den egna
    // när den inte är satt.
    gfx::Sizef contentSize(const Ctx& c) const override;

    // Flexens lucka — stilens `toolGap` (Bar packar annars kant i kant).
    float childGap(const Ctx& c) const override;

    // Bars ritning, och greppet i en värd.
    void draw(const Ctx& c) override;

    // Ett flytande fält vetter inte mot något innehåll — ingen hårlinje.
    bool drawsInnerEdge() const override { return !floating_; }

    // GREPPET (i en värd): trycket och draget går till värden, som räknar
    // målet, och ett DUBBELKLICK gör fältet flytande. Allt annat är Bars.
    void              mouseDown(const Ctx& c, float x, float y) override;
    void              mouseDrag(const Ctx& c, float x, float y) override;
    bool              mouseUp(const Ctx& c, float x, float y) override;
    void              mouseDoubleClick(const Ctx& c, float x, float y) override;
    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;
    // GREPPETS TRYCK AVBRÖTS (`Widget::mouseCancel`) —
    // trycket släpps och värden får veta det (`DockHost::itemPressLost`): den
    // platshållaren försvinner nu. Ett tryck utanför greppet är
    // Bars.
    bool              mouseCancel(gfx::MouseButton b) override;

    // BARNEN ÄNDRAR FÄLTETS EGEN LÄNGD — värden lägger om banden, och ett
    // flytande fönster passas till den nya storleken (i pumpens svans), också
    // när det är dolt och aldrig ritas: ett barn kom eller gick, eller dess
    // plats i flödet ändrades (`childLayoutChanged` — visat, dolt, eller ett
    // eget mått som verktygsknappens text och bild).
    void childAdded(Widget& child) override;
    void childRemoved(Widget& child) override;
    void childLayoutChanged(Widget& child) override;
    // …och ett SPRÅKBYTE byter barnens texter och rubrikens utan att en
    // sättare körs — samma väg, för ett flytande fält.
    void languageChanged() override;

    // ÖVERFLÖDET: knappen står utanför flexen; fokus som kommer in i
    // eller lämnar ett barn kan visa eller dölja det ur överflödet, så fältet
    // ritas om; och knappen följer fältets `enabled` (inredningens regel,
    // talfältets fält).
    bool inFlow(const Widget& child) const override;
    void childFocusChanged(Widget& child) override;
    void enabledChanged() override;

private:
    // Värden: föräldern — eller, medan fältet bor i ett flytande fönster (eller
    // är på väg hem ur ett), den som äger fönstret (`floatHost_`).
    DockHost* host() const;
    // I ETT BAND: värdens DIREKTA barn med en kant, och inte flytande. Då har
    // det ett grepp.
    bool  inBand() const;
    float gripLength(const Ctx& c) const;   // 0 utanför ett band

    // Greppets yta i widget-LOKALA LOGISKA px (gruppramens form: ytan `b`
    // som argument) — tom utanför ett band.
    gfx::Rectf gripRect(const Ctx& c, const gfx::Rectf& b) const;

    // Fältets mått OM det stod liggande (`vertical` false) eller stående, med
    // eller utan grepp — logiska px, i den riktningens axlar. Tjockleken och
    // längden behåller sin betydelse när fältet vänder: de satta fälten läses
    // i fältets NUVARANDE riktning (`width` är längden liggande, tjockleken
    // stående), så värdens indikator kan räkna ytan i ett band vid en annan
    // kant innan fältet flyttats — också ett FLYTANDE fält, som ska mätas MED
    // greppet det får i bandet (platshållaren är slutgeometrin). `contentSize` är svaret i den
    // nuvarande riktningen, med grepp bara i ett band.
    gfx::Sizef sizeFor(const Ctx& c, bool vertical, bool withGrip) const;
    // Den EGNA längden i riktningen `vertical`.
    float ownLength(const Ctx& c, bool vertical, bool withGrip) const;
    // AUTO-TJOCKLEKEN: max(`toolBarH`, den största
    // synliga verktygsknappsIKONEN + 2 × (`toolButtonPadY` + `toolBarPadY`)).
    // Ikonen och inte knappens mätta höjd: radhöjden är mätt per font och
    // skala, och en vanlig textknapp ska ge exakt `toolBarH`.
    float autoThickness(const Ctx& c) const;
    // EN REGEL FÖR RIKTNINGSBYTET: `wasVertical` är den effektiva
    // riktningen FÖRE ändringen av `floating_` eller `dock` — har den bytts
    // byts `width` och `height`. Anropas efter varje sådan ändring.
    void turnIfNeeded(bool wasVertical);
    // Ett mått som värden ska veta om ändrades (barnen, titeln).
    void notifyHost();

    // ---------------------------------------------- ÖVERFLÖDET
    // Knappen — en verktygsknapp med fältets meny (tool_bar.cpp).
    class OverflowButton;

    // SNITTET i `c` (barnens yta, fysiska px): fältet flödar över när
    // flödets barn inte ryms, och då visas de `shown` första i flödet plus
    // `revealed` — ett barn bortom dem som har fokus — med längden
    // `revealedLen` (fysiska px). Tillståndslöst som flexen: räknat per fråga
    // ur barnen, ytan och fokus.
    struct OverflowCut {
        bool          active      = false;
        std::size_t   shown       = 0;
        const Widget* revealed    = nullptr;
        float         revealedLen = 0.0f;
    };
    OverflowCut overflowCut(const Ctx& c) const;
    // Visas flödets barn nummer `i`, `w`, i snittet?
    static bool shows(const OverflowCut& cut, std::size_t i, const Widget& w)
    {
        return !cut.active || i < cut.shown || &w == cut.revealed;
    }
    // Mitt DIREKTA barn som har fokus eller en ättling med fokus — inte
    // knappen — eller nullptr.
    const Widget* focusedChild() const;
    // Barnets längd längs fältet i FYSISKA px — flexens mått.
    float flowLength(const Widget& w, const Ctx& c) const;
    // Fältets KORTASTE längd i riktningen `vertical`, logiska px som
    // `sizeFor`: med överflödet greppet, luften och knappen (aldrig mer än
    // den egna), annars den egna. Bandets krympning läser den (värden).
    float minLength(const Ctx& c, bool vertical, bool withGrip) const;
    // Bygger knappens meny ur de dolda barnen; `c` är knappens Ctx, alltså
    // mina barns. false = knappen dog i en `onDropDown` — rör den inte mer.
    bool fillOverflowMenu(const Ctx& c);
    // En dold verktygsknapp som post, och en inbäddad kontroll (false = den
    // kan inte ta fokus och får ingen post). `order` är fönstrets Tab-ordning.
    MenuItem buttonItem(ToolButton& b) const;
    bool     controlItem(Widget& w, const std::vector<Widget*>& order,
                         MenuItem& out) const;
    // En knapps poster som undermeny i `to`: samma text, bild och tillstånd,
    // och varje post aktiveras genom KNAPPENS meny (`Menu::activatePath`) med
    // knappen som sender — `path` är stigen dit, och `guard` knappens vakt.
    static void mirrorItems(const std::vector<MenuItem>& from,
                            std::vector<MenuItem>& to, ToolButton& b,
                            const HandlerGuard& guard, std::vector<int>& path);

    OverflowButton* overflow_   = nullptr;   // ägd, skapad i konstruktorn
    bool            overflowOn_ = true;

    TrText text_;
    int    band_ = 0;

    // FLYTANDE. `floatHost_` är värden medan fältet bor i dess flytande
    // fönster (föräldern är då fönstrets kropp) — nollad av värden när fältet
    // kommer tillbaka och när värden rivs.
    bool        floating_      = false;
    bool        floatable_     = true;
    // De tillåtna kanterna — fyra bool och ingen mask.
    bool        edgeLeft_      = true;
    bool        edgeTop_       = true;
    bool        edgeRight_     = true;
    bool        edgeBottom_    = true;
    gfx::Pointf floatPos_{};              // fönstrets plats, logiska px
    bool        floatPosKnown_ = false;   // platsen är satt (inte "-")
    DockHost*   floatHost_     = nullptr;

    // GREPPDRAGET — `gripDrag_` läses TILLSAMMANS med `pressed()` (kontraktet
    // vid `Widget::mouseUp`): ett avbrutet tryck släcker platshållaren av sig
    // själv. Målet räknar värden (`DockHost::toolDragMove`), och när fältet
    // LOSSNAR tar värden över trycket och nollar de här (fall A).
    bool  gripPress_ = false;
    bool  gripDrag_  = false;
    float pressX_ = 0.0f, pressY_ = 0.0f;   // trycket, fysiska px

    friend class DockHost;
    friend class DockFloatWindow;   // måtten när fönstret passas
};

} // namespace glaze
