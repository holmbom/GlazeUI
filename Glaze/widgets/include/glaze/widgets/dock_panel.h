#pragma once
#include "glaze/canvas/image_source.h"
#include "glaze/menu.h"
#include "glaze/widget.h"

// DockPanel — den DOCKNINGSBARA PANELEN (wxAuiPaneInfo, DockPanel Suites DockContent,
// VS-verktygsfönster). En RUBRIKRAD — titel, menyknapp (▾), knappnål och
// stängknapp — och under den INNEHÅLLET, som är panelens barn. MÖNSTER:
// widgeten ritar SIG SJÄLV — draw()/contentSize() i src/widgets/
// dock_panel.cpp (hård regel 3); rubrikens ritning med switch (c.ui) delas
// med FloatingPanel och bor i src/dock/dock_header.cpp. Mallen är
// button.h/group_box.h.
//
// PLACERINGEN ÄR `Dock` — det fält och den remsskärning ramverket redan
// har. `panel.dock = glaze::Dock::Right;` säger vid vilken kant panelen
// står och `width` (vid Left/Right) eller `height` (vid Top/Bottom) hur
// tjock remsan är; AUTO (<= 0) är stilens `dockDefaultSize`. Det INTERAKTIVA
// kräver att panelen står i en `DockHost` (dock_host.h): flikar och staplar
// (`dockPanel`), infällning till ett HANDTAG på kanten, knappnålen och
// overlayn, rubrikdraget till en annan kant eller en annan panel, skikten och
// layoutsträngen. Utan värd är panelen en vanlig dockad remsa med rubrik som
// går att fälla och stänga, och remsskärningen i `Widget::resolve` placerar
// den. Default är `Dock::Left` (konstruktorn, ToolBar-prejudikatet åt andra
// hållet): en dockningsbar panel med `Dock::None` hade varit en fri ruta.
//
// RUBRIKKNAPPARNA ÄR DATA och inte barn (ListBox/MenuBar-prejudikatet): en
// `Button` är ett tabbstopp och tar fokus vid klick, och en knapp som stal
// fokus ur editorn bredvid hade varit värre än ingen knapp. Från höger:
// stäng (ytterst — fönsterknapparnas regel), knappnålen och menyknappen ▾.
//
// KNAPPNÅLEN (Visual Studios Auto Hide) — två oberoende booleaner,
// `pinned` och `collapsed`:
//
//     nålad  infälld  vad användaren ser
//     ja     nej      dockad, trycker undan huvudytan
//     ja     ja       ett HANDTAG på kanten; ett klick fäller ut panelen DOCKAD
//     nej    ja       ett handtag; ett klick fäller ut panelen som OVERLAY
//     nej    nej      overlayn är öppen — ett drifttillstånd, sparas som infälld
//
// Knappnålen på en dockad panel gör `pinned = false` och `collapsed = true`:
// panelen glider in, och handtaget står kvar. Knappnålen i en öppen overlay
// fäster den — den blir dockad. `setPinned(false)` fäller in panelen som
// knappen gör. Overlayn TRYCKER INTE undan huvudytan: den glider fram
// ovanpå (värdens `frontChild`), och värden fäller in den när BÅDE pekaren
// och fokus lämnat den (dock_host.h). Knappnålen och stängknappen verkar på
// panelen själv — i en flikgrupp den aktiva fliken; de andra flikarna står
// kvar.
//
// MENYKNAPPEN ▾ öppnar rubrikmenyn: "Flytande" (eller "Docka" när panelen
// flyter), "Dölj automatiskt" eller "Fäst" (samma handling som knappnålen),
// en skiljelinje och "Stäng". Rubrikknapparnas tooltips är ramverkets texter
// (`glaze::str::DockAutoHide`/`DockPin`/`DockClose`/`DockPosition`).
//
// FÄLLA IN (`setCollapsed`) — I EN VÄRD blir panelen ett HANDTAG i en egen
// remsa vid sin kant, utanför alla panelremsor, och ett klick på handtaget
// fäller ut den igen. Måtten står kvar, så den fälls ut i sin gamla storlek.
// Handtaget vid Top/Bottom bär titeln (och ikonen, `setImg`); vid Left/Right
// är det en kvadrat med ikonen eller titelns första tecken, och titeln som
// tooltip — det finns ingen roterad text. UTAN VÄRD krymper panelen till sin
// rubrik åt det håll den är dockad (knappnålen fäller och fäster den), och
// den hopfällda panelen är ett tabbstopp som Space/Enter fäller ut (i en värd
// är handtagen värdens tabbstopp i stället).
//
// INAKTIVA FLIKAR OCH INFÄLLDA PANELER DÖLJS INTE (`isVisible()` ändras
// inte): värden ger dem en tom yta, och klippet gör att barnen varken ritas
// eller träffas. `showsChildren()` stänger Tab-vägen in. `hide()` betyder
// fortsatt STÄNGD.
//
// STÄNG är `hide()` — panelen finns kvar och visas igen med `show()`
// (WinForms `Visible`, wxAUI `Show`), och i en värd blir den då den aktiva
// fliken i sin grupp. `setClosable(false)` döljer stängknappen
// (TabControls ord) och släcker menyns "Stäng". Ingen egen `onClose` med
// veto: en app som vill neka döljer stängknappen.
//
// MIN/MAX (`setMinimumSize`/`setMaximumSize`, AppWindows namn) i logiska px,
// 0 = ingen gräns per axel. Bredden begränsar en stående remsas tjocklek och
// en liggande remsas cellängd, höjden det omvända — och overlayns tjocklek;
// gränserna är MJUKA mot utrymmet (se dock_host.h).
//
// `activate()` öppnar en stängd panel, väljer dess flik och fäller ut den —
// som overlay om den inte är nålad. "Visa den här panelen", Visa-menyns rad.
//
// `onChange` fyras när ANVÄNDAREN ändrat panelen — fäst, dolt automatiskt,
// stängt, valt dess flik, gjort den flytande eller dockat den, dragit den
// någonstans eller dragit i dess skikt — aldrig av settrarna, och inte när
// overlayn öppnas eller fälls in: den är
// ett drifttillstånd som layoutsträngen inte bär. Läs läget ur sender
// (`isPinned()`, `isCollapsed()`, `isVisible()`, `isFloating()`, `dock`,
// `width`/`height`). Värden har dessutom ett `onChange` för HELA layouten,
// som fyras efter panelens (dock_host.h).
//
// FLYTANDE — `setFloating(true)` lyfter panelen ur värdens
// layout till ett EGET fönster, ägt av värdens: Z-ordningen följer ägaren,
// fönstret stängs med den och döljs med den, och det har ingen knapp i
// aktivitetsfältet. Panelens rubrik är fönstrets titelrad (titel, ▾ och
// stäng — ingen knappnål: en flytande panel är alltid nålad och utfälld).
// Dubbelklick på rubriken växlar mellan dockad och flytande (VS), som
// menyns "Flytande"/"Docka". Ett RUBRIKDRAG (eller ett drag i fliken) lossar
// panelen vid tröskeln till sitt fönster, som följer pekaren; släppt där
// värdens platshållare står dockas den, släppt någon annanstans förblir den
// flytande där — och det flytande fönstrets eget rubrikdrag går likadant
// (levande drag — dock_host.h). TILLSTÅNDET gäller direkt
// (`isFloating()` och `layout()`), och flytten mellan fönstren görs i
// pumpens svans. Panelens `dock` står kvar och är minnet för var den dockas
// tillbaka.
// `setFloatingBounds(x, y, b, h)` är fönstrets plats och INNEHÅLLSYTA i
// LOGISKA px (`AppWindow::setLocation`s rymd); `setFloatable(false)` tar bort
// användarens vägar till flytande (menyn, dubbelklicket, draget ut). Inget
// fönster skapas när värdens fönster är stängt eller offscreen (glaze-render)
// — där lämnar panelen bara layouten — och ett DOLT värdfönster skapar sina
// flytande fönster dolda. `appWindow()` på en flytande panel är dess eget
// fönster: appkod når panelen genom medlemmen, inte genom `find<>()`.

namespace glaze {

class DockHost;

class DockPanel : public Widget {
public:
    explicit DockPanel(TrText t = {});
    // En FLYTANDE panel som dör säger det till sin värd i samma ögonblick —
    // dess fönster släpper den innan det hinner rita, träffa eller ge fokus
    // till den igen.
    ~DockPanel() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar aldrig onChange.
    //
    // RUBRIKEN är `text` (TrText, regel 6) och tar `glaze::tr(id)`.
    void               setText(TrText t);
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // IKONEN — handtagets och flikens bild. Buttons namn (`img`) och
    // bildvärdet `ImageSource` (en asset, en Bitmap eller en RenderTexture).
    // Ogiltig = ingen ikon; ett stående handtag ritar då titelns första
    // tecken.
    void               setImg(ImageSource img);
    const ImageSource& img() const { return img_; }

    void setCollapsed(bool on);
    bool isCollapsed() const { return collapsed_; }

    // KNAPPNÅLEN. `setPinned(false)` fäller också in panelen — en
    // onålad panel är ett handtag tills den fälls ut som overlay.
    void setPinned(bool on);
    bool isPinned() const { return pinned_; }

    // Min/max i LOGISKA px, 0 = ingen gräns per axel. Ett negativt eller
    // icke ändligt tal loggas och ändrar ingenting.
    void       setMinimumSize(float w, float h);
    void       setMaximumSize(float w, float h);
    gfx::Sizef minimumSize() const { return minSize_; }
    gfx::Sizef maximumSize() const { return maxSize_; }

    // Stängknappen syns (default true).
    void setClosable(bool on);
    bool isClosable() const { return closable_; }

    // FLYTANDE — se headern. `setPinned`/`setCollapsed` på en flytande
    // panel loggas och ignoreras: den är nålad och utfälld så länge den flyter.
    void setFloating(bool on);
    bool isFloating() const { return floating_; }
    // Användarens vägar till flytande (default true). Appen kan alltid.
    void setFloatable(bool on);
    bool isFloatable() const { return floatable_; }
    // Det flytande fönstrets plats och innehållsyta i LOGISKA px — ändliga,
    // storleken >= 0 (0 = panelens föredragna mått). Ett ogiltigt värde loggas
    // och ändrar ingenting. Läsaren svarar med det senast satta — eller, när
    // panelen dockats tillbaka, fönstrets sista plats och storlek, så nästa
    // flytt öppnar där (värdens `layout()` läser det levande fönstret).
    void       setFloatingBounds(float x, float y, float w, float h);
    gfx::Rectf floatingBounds() const { return floatBounds_; }

    // Öppna om stängd, välj fliken och fäll ut — programmatiskt, så inget
    // `onChange`. En onålad panel fälls ut som overlay. Utan värd: `show()`
    // och utfälld.
    void activate();

    // Användaren ändrade panelen (se headern). Läs läget ur sender.
    Handler<ChangeArgs> onChange;

    // setProp-egenskaper: text, collapsed, pinned, closable, floating,
    // floatable (true/false), img ("lista/nyckel", Buttons form). Inget
    // `title`-alias — en ny widget har inga äldre filer att vara snäll mot.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT rubrik — regel 8:s tr-väg. Övriga nycklar går
    // till basen (verktygstipset).
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text") { setText(value); return true; }
        return Widget::setTextToken(key, value);
    }

    // UTAN VÄRD är den HOPFÄLLDA panelen ETT tabbstopp som ersätter sina
    // barn, och Space/Enter fäller ut den. I en värd är panelen aldrig ett
    // stopp: handtagen och flikarna är värdens (dock_host.h).
    bool focusable() const override;
    bool focusesChildren() const override { return true; }

    // Mina barn syns bara när jag själv gör det på riktigt: inte som en
    // inaktiv flik och inte infälld (widget.h, `showsChildren`) — men som
    // öppen overlay.
    bool showsChildren() const override;

    // Innehållet bor under rubriken och klipps alltid mot det — hopfälld,
    // som inaktiv flik eller infälld är ytan tom, så barnen varken ritas
    // eller träffas.
    gfx::Rectf childArea(const Ctx& c) const override;
    bool       clipsChildren() const override { return true; }
    gfx::Rectf clipRect(const Ctx& c) const override { return childArea(c); }

    // Rubrikknapparnas tooltip är ramverkets text för knappen; resten av
    // panelen har sin egen (basens).
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // REMSANS TJOCKLEK längs dockningsaxeln i LOGISKA px när panelen står
    // UTAN värd: hopfälld = rubrikens höjd, annars `width` (stående remsa)
    // eller `height` (liggande), AUTO = stilens `dockDefaultSize`. EN sanning,
    // som `contentSize()` läser. (I en värd räknar modellen remsan ur alla
    // dess paneler — src/dock/dock_model.h.)
    float stripThickness(const Ctx& c, bool standing) const;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       keyDown(gfx::Key k, std::uint32_t mods) override;

    void mouseDown(const Ctx& c, float x, float y) override;
    void mouseDrag(const Ctx& c, float x, float y) override;
    bool mouseUp(const Ctx& c, float x, float y) override;
    void mouseMove(const Ctx& c, float x, float y) override;
    // Dubbelklick på rubriken växlar flytande (VS).
    void mouseDoubleClick(const Ctx& c, float x, float y) override;
    // TRYCKET AVBRÖTS (`Widget::mouseCancel`): rubrikens
    // drag och knapparnas tryck släpps här, och värden får veta det
    // (`DockHost::itemPressLost`) — bar den panelen i dess eget fönster
    // (fall B) avslutas draget som avbrutet, och platshållaren försvinner. Ingen
    // användarkod: värden postar sitt `onChange`.
    bool mouseCancel(gfx::MouseButton b) override;

    // Rubrikmenyn är en popup i mitt delträd — värden fäller inte in en
    // overlay medan den är öppen, och en inaktiverad panel stänger den.
    bool hasOpenPopup() const override { return menu_.isOpen(); }
    void enabledChanged() override;

private:
    // Rubrikradens DELAR. `Header` är dragytan (titeln och den tomma raden);
    // knapparna är data i raden.
    enum class Part { None, Header, Menu, Pin, Close };

    // GEOMETRIN — EN sanning i widget-LOKALA LOGISKA px, och den tar ytan
    // `b` som argument (gruppramens form): ritningen skickar `c.bounds()`,
    // beteendet `Draw::bounds(c, resolve(c))`.
    bool       standingStrip() const;   // hopfälld vid Left/Right, utan värd
    gfx::Rectf headerRect(const Ctx& c, const gfx::Rectf& b) const;
    // Knappen på plats `slot` räknat från ytterkanten (stäng ytterst).
    gfx::Rectf slotRect(const Ctx& c, const gfx::Rectf& b, int slot) const;
    // 3 (▾, knappnål, stäng); utan stängknapp en färre, och en flytande
    // panel har ingen knappnål.
    int        buttonCount() const;
    // Tom när stängknappen inte syns (`setClosable(false)`).
    gfx::Rectf closeRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf pinRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf menuRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf titleRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf bodyRect(const Ctx& c, const gfx::Rectf& b) const;
    gfx::Rectf partRect(const Ctx& c, const gfx::Rectf& b, Part p) const;
    // Delen under en FYSISK punkt (musvägen, `ctxFor`-Ctx).
    Part partAt(const Ctx& c, float x, float y) const;
    DockHost* host() const;

    // Ett mått eller ikonen ändrades — panelen själv, syskonen och värden
    // ritas om, och värden får veta.
    void layoutChanged();
    // FLAGGORNA ändrades (`collapsed`/`pinned`) — `was*` är läget före. Som
    // `layoutChanged`, och värden får veta vad som hände: overlayn öppnas
    // eller fälls in, remsan glider, och fokus får inte stå kvar i en infälld
    // panel.
    void stateChanged(bool wasCollapsed, bool wasPinned);
    // UTAN VÄRD: fäst och utfälld, `stateChanged` bara vid en ändring —
    // `activate()`s och `userExpand()`s gemensamma steg.
    void pinAndExpand();
    // KNAPPNÅLEN SOM ANVÄNDAREN SER DEN: fäst (ett tryck döljer automatiskt)
    // eller inte (ett tryck fäster). I en värd är det `pinned`; utan värd
    // finns ingen overlay, och nålen står så länge panelen är utfälld.
    bool pinShown() const;

    // ANVÄNDARENS ändringar — de enda vägarna som fyrar `onChange` (panelens
    // och värdens). Svarar om någon handler kördes.
    bool userPin();       // knappnålen: fäst ↔ dölj automatiskt
    bool userExpand();    // utan värd: Space/Enter på den hopfällda
    bool userClose();     // stängknappen och menyns "Stäng"
    bool userFloat(bool on);   // menyn och dubbelklicket
    bool userChanged();

    // RUBRIKMENYN — byggs vid öppningen, öppnas under ▾-knappen.
    void openMenu();
    void closeMenu();

    // Står knappen `p` TÄND (hover) respektive NEDTRYCKT just nu? Rubriken
    // ritas av den delade `detail::drawPanelHeader` (src/dock/dock_header.h),
    // som får bara dessa flaggor och knapparnas rektar.
    bool partHot(Part p) const;
    bool partDown(Part p) const;

private:
    TrText      text_;
    ImageSource img_{};
    bool        collapsed_ = false;
    bool        closable_  = true;
    bool        pinned_    = true;   // KNAPPNÅLEN
    gfx::Sizef  minSize_{0.0f, 0.0f};
    gfx::Sizef  maxSize_{0.0f, 0.0f};

    Menu menu_;              // rubrikmenyn
    bool menuOpen_ = false;  // ▾ visas intryckt medan den är öppen

    // FLYTANDE. `floatHost_` är värden medan panelen bor i dess
    // flytande fönster (föräldern är då fönstrets kropp) — nollad av värden
    // när panelen kommer tillbaka och när värden rivs.
    bool       floating_      = false;
    bool       floatable_     = true;
    gfx::Rectf floatBounds_{};          // plats + innehållsyta, logiska px
    bool       floatPosKnown_ = false;  // platsen är satt (inte "-")
    DockHost*  floatHost_     = nullptr;

    Part  hoverPart_   = Part::None;   // mouseMove — läses med `hovered()`
    Part  pressPart_   = Part::None;   // delen trycket tog (mouseDown)
    bool  pressInside_ = false;        // pekaren kvar över den tryckta knappen
    // RUBRIKDRAGET. `headerDrag_` läses TILLSAMMANS med `pressed()`
    // (kontraktet vid `Widget::mouseUp`): ett avbrutet tryck släcker
    // `pressed()`, och då försvinner värdens platshållare av sig själv. MÅLET
    // räknar värden (`DockHost::panelDragMove`), och när panelen LOSSNAR ur
    // dockat läge tar värden över trycket och nollar de här (fall A).
    bool  headerDrag_ = false;
    float pressX_ = 0.0f, pressY_ = 0.0f;   // trycket, fysiska px

    friend class DockHost;
    friend class DockFloatWindow;   // storleken och platsen när fönstret öppnas
};

} // namespace glaze
