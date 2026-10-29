#pragma once
#include <memory>
#include <optional>

#include <gfx/geometry.h>

#include "glaze/app_window.h"
#include "glaze/widget.h"

// INTERN header (`dock_model.h`-prejudikatet) — DOCKNINGENS FLYTANDE FÖNSTER.
// Konsumenten är `DockHost`; en app har inget ärende hit.
//
// ETT FLYTANDE OBJEKT FLYTTAS — en `DockPanel` eller en `ToolBar`, samma
// maskineri för båda — ingen kopia: hela dess delträd lämnar värden
// (`Widget::transferChild`, ägandet följer med) och läggs i en `FloatBody` i
// ett eget `DockFloatWindow`, som värden äger och visar ÄGT av sitt eget
// fönster — Z-ordningen följer ägaren, fönstret kaskadstängs med den och har
// ingen knapp i aktivitetsfältet. Objektets `dock` (och fältets `band()`)
// står kvar och är minnet för var det dockas tillbaka.
//
// PANELENS fönster är panelen över hela ytan — dess rubrik är titelraden, och
// fönstret går att storleksändra. FÄLTETS fönster har en egen RUBRIK överst
// (`ToolFloatHeader`: titeln och en stängknapp) och fältet LIGGANDE under
// den, i FAST storlek ur fältets egna mått.
//
// NAMNRYMDEN är `glaze` för fönstren (som `ToolTipWindow`/`TrayHostWindow`),
// så att friend-raden i app_window.h träffar `glaze::DockFloatWindow`;
// posten värden håller är `detail::`.
// Destruktorerna står i cpp:n — `unique_ptr` till ofullständiga typer.

namespace glaze {

class DockHost;
class DockPanel;
class ToolBar;
class DockFloatWindow;

// DET FLYTANDE VERKTYGSFÄLTETS RUBRIK — en intern,
// oregistrerad widget överst i fältets fönster: titeln (fältets `text`,
// annars ramverkets "Verktygsfält") och en stängknapp. Panelrubrikens recept
// (dock_header.cpp) med egen `switch (c.ui)` och samma mått — `dockHeaderH`,
// `dockHeaderPadX`, `dockGlyph`, `dockButtonInset` — inga nya stilfält och
// inga nya temaroller. Ett drag i ytan går till värden (`toolHeaderDrag`),
// ett dubbelklick dockar fältet och ✕ döljer det. Ingen fokus: rubriken är
// fönstrets chrome, inte ett tabbstopp.
class ToolFloatHeader final : public Widget {
public:
    explicit ToolFloatHeader(DockFloatWindow& win);

    void   draw(const Ctx& c) override;
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // Pågår ett DRAG i rubriken? Läst TILLSAMMANS med `pressed()` (kontraktet
    // vid `Widget::mouseUp`): ett avbrutet tryck släcker draget av sig självt.
    bool dragging() const { return drag_ && pressed(); }
    // Var trycket tog — fysiska px i fönstret, vars klienthörn är rubrikens.
    // Det levande dragets GREPP: fönstret följer pekaren
    // med handen kvar där den tog.
    gfx::Pointf pressPoint() const { return gfx::Pointf{pressX_, pressY_}; }
    // DET BURNA DRAGETS TRYCK (utan övertagande):
    // rubriken håller explicit capture i fönstret medan fältet
    // bärs här, så att fönstrets avbrott — `MouseCaptureLost`, ett annat
    // fönster som tar trycket — når värden (`mouseCancel`). Värden tar och
    // släpper den.
    void holdCarry() { setPointerCapture(); }
    void releaseCarry()
    {
        if (hasPointerCapture())
            releasePointerCapture();
    }

protected:
    // Placeras av kroppen (`FloatBody::placeChild`) — höjden är rubrikens.
    gfx::Sizef contentSize(const Ctx& c) const override;

    void mouseDown(const Ctx& c, float x, float y) override;
    void mouseDrag(const Ctx& c, float x, float y) override;
    bool mouseUp(const Ctx& c, float x, float y) override;
    void mouseMove(const Ctx& c, float x, float y) override;
    void mouseDoubleClick(const Ctx& c, float x, float y) override;
    // TRYCKET AVBRÖTS (`Widget::mouseCancel`) — rubrikens
    // drag och stängknappens tryck släpps, och värden får veta det
    // (`DockHost::itemPressLost`): ett buret drag i fönstret avslutas som
    // avbrutet, utom under värdens övertagande av trycket.
    bool mouseCancel(gfx::MouseButton b) override;

private:
    enum class Part { None, Header, Close };

    // GEOMETRIN i widget-LOKALA LOGISKA px med ytan som argument (gruppramens
    // form): stängknappen är en kvadrat längst till höger (stäng hör till
    // ytterkanten, panelrubrikens regel), titeln resten minus luften.
    gfx::Rectf closeRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf titleRect(const Ctx& c, const gfx::Rectf& b) const;
    Part       partAt(const Ctx& c, float x, float y) const;
    std::string title() const;

    DockFloatWindow& win_;
    Part             hover_       = Part::None;   // läses med `hovered()`
    Part             press_       = Part::None;   // delen trycket tog
    bool             pressInside_ = false;        // pekaren kvar över ✕
    bool             drag_        = false;
    float            pressX_ = 0.0f, pressY_ = 0.0f;   // trycket, fysiska px
};

// KROPPEN — fyller fönstrets innehållsyta och PLACERAR objektet: panelen över
// hela ytan oavsett dess `dock` (som är tillbakadockningens minne och inte
// får skrivas över), fältet under rubriken. När objektet visas eller döljs
// följer fönstret (postat — kroken körs mitt i en sättare). Kroppen ritar
// ingenting, men dess `draw()` körs vid varje omritning av fönstret: där
// frågar den värden om något väntar — en flytt som en popup höll kvar, eller
// ett fält vars mått ändrats utan en händelse, som ett stilbyte
// (`DockHost::syncFloats`; barnen och språkbytet postar synken själva).
class FloatBody final : public Widget {
public:
    // `withHeader` — fältets fönster: rubriken adopteras överst.
    FloatBody(DockFloatWindow& win, bool withHeader);

    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // RIVNINGSVÄGEN — objektet ut ur
    // kroppen utan att dess delträd rörs (`Widget::severChild`). Ett ÄGT
    // objekt lämnas ut.
    std::unique_ptr<Widget> release(Widget* item, bool alive)
    {
        return severChild(item, alive);
    }

    const ToolFloatHeader& header() const { return header_; }
    ToolFloatHeader&       header() { return header_; }

protected:
    gfx::Sizef contentSize(const Ctx&) const override;
    // Bara OBJEKTETS synlighet räknas — rubriken är fönstrets egen.
    void childVisibilityChanged(Widget& child) override;

private:
    DockFloatWindow& win_;
    ToolFloatHeader  header_;
};

// DET FLYTANDE FÖNSTRET — `Chrome::Custom` med namnlisten och programikonen
// DOLDA där fönster går att placera: panelens rubrik respektive fältets
// `ToolFloatHeader` är titelraden. Där fönster inte går att placera
// (Wayland) syns namnlisten i stället, som flytthandtag. Ingen
// minimera- eller maximeraknapp, ingen knapp i aktivitetsfältet. En panels
// fönster går att storleksändra (kantgreppen); ett fälts har FAST storlek.
// AKTIVERINGEN: en panels fönster stjäl inte fokus vid sin
// FÖRSTA visning (`showInactive_` — lossningen mitt i ett drag, och en dold
// start där huvudfönstret ska behålla aktiveringen), ett fälts fönster
// aktiveras ALDRIG (`noActivate_` + `toolWindow_`, som VS:s flytande
// verktygsfält: ett klick på "Fet" lämnar tangentbordet i textfältet).
class DockFloatWindow final : public AppWindow {
public:
    DockFloatWindow(DockHost& host, DockPanel& panel);
    DockFloatWindow(DockHost& host, ToolBar& bar);
    ~DockFloatWindow() override;

    FloatBody body;

    DockHost& host() const { return host_; }
    // Objektet om det LEVER — panelen eller fältet; det andra är nullptr.
    DockPanel* panel() const;
    ToolBar*   bar() const;
    Widget*    item() const;

    // ÖPPNA ägt av `owner`. PANELEN: storleken och platsen ur
    // `floatingBounds()` (eller panelens föredragna mått, och mitt över ägaren
    // när platsen är okänd), gränserna ur panelens min/max. FÄLTET: den fasta
    // storleken ur fältets mått (`fitToolBar`) och platsen ur
    // `floatingPosition()`. `placement` är ett BURET objekts plats — fönstrets
    // klienthörn i placeringsrymden: den vinner över den
    // sparade och klämmer inte, för ett fönster som följer pekaren får stå
    // delvis utanför skärmen. DOLT när ägaren är dold eller objektet stängt.
    // Ett fönster som stängts med sin ägare öppnas igen där det stod.
    void open(AppWindow& owner, std::optional<gfx::Pointf> placement);
    // Synligheten följer objektet och ägaren: synligt bara när objektet är
    // öppet och ägaren är öppen och visad. Ett fälts fönster passas FÖRE det
    // visas — ett dolt fönster ritas inte, så ingen ritning har passat det
    // under tiden.
    void syncVisible();
    // Objektets sparade plats (och panelens innehållsyta) till ett ÖPPET
    // fönster — platsen bara där sådana finns.
    void applyBounds();
    // Objektets titel till fönstret; panelens min/max, eller fältets rubrik
    // och fasta storlek (`DockHost::panelChanged`, `syncFloats`).
    void itemChanged();
    // Fönstrets nuvarande innehållsyta och plats i LOGISKA px;
    // `hasPosition` false där positioner inte finns, och för ett fönster som
    // aldrig öppnats.
    gfx::Rectf bounds(bool& hasPosition) const;
    bool       opened() const { return opened_; }
    // Kroppen ritades — värden avslutar ett buret drag vars tryck försvann
    // (`DockHost::checkCarry`) och synkar om något väntar.
    void requestSync();
    // Objektet DÖR (`DockHost::floatingItemDestroyed`): kroppen släpper det
    // utan att röra det, fönstret glömmer varje widgetpekare
    // (`AppWindow::forgetWidgets`) och döljs. Rivs i värdens synk.
    void itemDestroyed();

    // FÄLTETS FASTA STORLEK: rubriken (`dockHeaderH`) plus fältet
    // liggande i sin egen längd (`ToolBar::sizeFor`), aldrig smalare än
    // rubrikens stängknapp och dess luft — min == max, så fönstret går inte
    // att dra i. Mätt i fönstrets eget Ctx när det är öppet (dess skala och
    // font), annars i värdens. `fitWanted()` svarar när måtten inte längre
    // stämmer: ett barn, en text, en bild, ett stil- eller språkbyte. Barnen
    // och språkbytet POSTAR passningen när de händer (`DockHost::barChanged`,
    // också för ett dolt fönster); ritningens fråga är reserven för resten.
    void fitToolBar();
    bool fitWanted() const;

protected:
    // En tangent ingen i fönstret tog går till ÄGARENS acceleratorer.
    bool acceleratorFallback(gfx::Key k, std::uint32_t mods) override;
    // Fönstret följer ägarens `hide()`/`show()`.
    void ownerVisibilityChanged(bool shown) override;

private:
    // Det gemensamma för båda sorterna: chromet, knapparna, kroppen och
    // stängningen. En KASKAD (ägaren eller någon längre upp stänger) känns
    // igen på skälet, `CloseReason::OwnerClosing`,
    // och får inte vetas.
    void setUp();
    // Panelens fönster: gränserna ur panelens min/max — panelens gränser
    // gäller INNEHÅLLET, och fönstrets gäller klientytan; skillnaden är det
    // chrome som syns (namnlisten på Wayland), uppmätt och aldrig ett
    // stilmått i koden.
    void applyLimits();
    // Fältets önskade innehållsyta (logiska px); false när den inte går att
    // mäta (fältet är dött, eller inget fönster är öppet att mäta i).
    bool wantedBarSize(gfx::Sizef& out) const;
    // Fältets titel — dess `text`, annars ramverkets "Verktygsfält".
    TrText barTitle() const;

    DockHost&           host_;
    DockPanel*          panel_ = nullptr;   // ett av de två
    ToolBar*            bar_   = nullptr;
    std::weak_ptr<char> life_;
    bool                opened_ = false;    // har öppnats minst en gång
    gfx::Sizef          fitted_{};          // fältets passade mått (önskade)
    bool                fitKnown_ = false;
};

namespace detail {

// Ett FLYTANDE OBJEKT i värdens bok: panelen ELLER fältet (derefereras bara
// medan livstecknet lever), livstecknet och fönstret det bor i.
struct DockFloat {
    DockPanel*                       panel = nullptr;   // ett av de två
    ToolBar*                         bar   = nullptr;
    std::weak_ptr<char>              life;
    std::unique_ptr<DockFloatWindow> win;

    // Objektet som widget — pekaren JÄMFÖRS; den derefereras bara medan
    // `life` lever.
    Widget* item() const;

    DockFloat();
    ~DockFloat();
};

// Fönstret `w`s KLIENTPUNKT för en punkt i skärmrymden (fasadens
// placeringsrymd) — räknad bara ur `clientToScreen`: origo ur (0,0) och skalan
// ur (1,1). Avbildningen är affin på Windows, macOS och X11; på Wayland finns
// ingen skärmrymd (där används den inte).
gfx::Pointf toClientOf(const gfx::Window& w, gfx::Pointf screen);

} // namespace detail

} // namespace glaze
