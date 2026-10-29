#pragma once
#include <utility>

#include "glaze/draw.h"
#include "glaze/menu.h"
#include "glaze/widget.h"

// ToolButton — VERKTYGSKNAPPEN (wxAuiToolBar-verktyget,
// WinForms ToolStripButton). Ett barn i en `ToolBar`, men den går att
// placera var som helst. MÖNSTER: widgeten ritar SIG SJÄLV —
// draw()/contentSize() bor i src/widgets/tool_button.cpp med switch (c.ui)
// och ritar via Draw:: (hård regel 3). Mallen är `button.h`.
//
// VARFÖR EN EGEN TYP och inte en `Button` i en bar — tre saker skiljer dem,
// och ingen av dem är utseende ensamt:
//
//   * FOKUS. En `Button` är ett tabbstopp och TAR fokus vid klick. En
//     verktygsknapp gör ingetdera: klickar man "Fet" medan man skriver i
//     ett textfält ska caret och markering stå kvar där de stod. Därför
//     svarar `focusable()` false — AppWindow flyttar bara fokus till en
//     fokuserbar widget, så klicket lämnar fokus orört, och Tab passerar
//     remsan. (WinForms ToolStrip gör likadant.)
//   * PLATT. Ingen yta i vila — remsan ÄR ytan, och en rad upphöjda plattor
//     läser som en knapprad och inte som ett verktygsfält. Plattan tänds
//     först vid hover/tryck, som menyradens rubriker.
//   * VÄXLA. `setCheckable(true)` gör knappen till ett läge som slås av och
//     på (wxITEM_CHECK — "Visa rutnät", "Fet"). Ikryssat läge är en
//     INTRYCKT platta med kanten i `accent` — inte den fyllda accentytan,
//     som färgade ikoner försvinner i (skälet står i cpp:n).
//
// Bilden är ett `ImageSource` (`glaze::img("lista", "nyckel")` eller en
// `glaze::Bitmap`) precis som på `Button`, och namnen är Buttons: `text`,
// `img`, `imagePos` (regel 6 — delade saker delar namn). Tom text = bara
// ikon, ogiltig bild = bara text. En Bitmap ritas i `iconFallback`-rutan —
// eller i den sida `setIconSize` ger.
//
// INAKTIV: texten dimmas med sin färg, IKONEN med sänkt OPACITET
// (`Content::imgOpacity` = 1 − `Style::iconFade`) — tonad mot vilken yta
// som helst, så knappen kan stå på en panel eller i en gruppram lika väl
// som i remsan.
//
// TOOLTIPEN ärvs av basen (`setToolTip`) — en knapp med bara en
// ikon är dess främsta konsument, men den bor på `Widget` och inte här.
//
// RULLGARDINEN (WinForms ToolStripDropDownButton och
// ToolStripSplitButton). En knapp med MENYPOSTER får en pil ▾ i en egen ruta
// till höger (`Style::toolArrowW`, som AUTO-bredden växer med), och ett TRYCK
// var som helst på den öppnar menyn under knappen och stänger den igen
// (menyradens toggle) — `onClick` fyras inte och en växlingsbar knapp växlar
// inte. Med `setSplit(true)` blir den DELAD: huvuddelen är en vanlig knapp
// (klick och växling) och bara pildelen öppnar menyn — "Ångra ▾" med
// historiken bakom pilen. Utan poster är `split` verkningslös.
//
// MENYN byggs med SYSTEMFÄLTSIKONENS vokabulär (hård regel 8): den TOMMA
// stigen `{}` är roten, och det finns INGET genvägsargument. Ingen
// acceleratorväg frågar en knappmeny — fönstret frågar menyraden och
// programikonen — och en genväg som visas men aldrig verkar vore en lögn.
// Postens tillstånd har menybärarnas tre platta mutatorer.
//
//     ny.addItem({}, "Dokument");
//     ny.addSubmenu({}, "Mall");
//     ny.addItem("Mall", "Brev");
//     ny.setHandler("Dokument", [this] { nyttDokument(); });   // i kod
//     angra.setSplit(true);
//     angra.onDropDown = [this] { fyllHistorik(); };
//
// `onDropDown` fyras när ANVÄNDAREN trycker fram menyn, FÖRE den öppnas —
// där fylls en dynamisk meny (senaste filer), som systemfältsikonens
// `onContextMenu`. Menyn öppnas efteråt om den fortfarande har poster.
// `openMenu()` öppnar utan eventet, som systemfältsikonens. Menyn hamnar
// alltid UNDER knappen, också i ett lodrätt verktygsfält — en skriven
// begränsning.
//
// INTE HÄR med flit: radiogrupper av verktyg (wxITEM_RADIO). En radiogrupp
// går att bygga med `checkable` + `onClick` i dag.

namespace glaze {

class ToolButton : public Widget {
public:
    explicit ToolButton(TrText t = {});

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR: settern är
    // den som INVALIDERAR. LAYOUT-fälten är fortsatt RÅA — se widget.h.
    //
    // TEXTEN, BILDEN OCH BILDENS PLACERING ändrar knappens AUTO-bredd, så de
    // tre lägger också om syskonen (`relayoutSiblings`): i remsan flyttar de,
    // och fältet får veta det — ett flytande fälts fönster passas till den
    // nya längden också medan det är dolt.

    // TEXTEN ÄR ETT TOKEN — literal ELLER översättnings-id.
    // Getter-paret och skälen står i `label.h` (mallen).
    void setText(TrText t)
    {
        if (text_ == t)
            return;
        text_ = std::move(t);
        invalidate();
        relayoutSiblings();
    }
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    void setImg(ImageSource img)
    {
        if (img_ == img)
            return;
        img_ = std::move(img);
        invalidate();
        relayoutSiblings();
    }
    const ImageSource& img() const { return img_; }

    // Ikonens placering relativt texten — samma typ och samma namn som på
    // `Button`. Ignoreras när texten är tom (ikonen centreras då ensam).
    void setImagePos(ImagePos p)
    {
        if (imagePos_ == p)
            return;
        imagePos_ = p;
        invalidate();
        relayoutSiblings();
    }
    ImagePos imagePos() const { return imagePos_; }

    // IKONENS SIDA i LOGISKA px — 0 = AUTO
    // (assetens storleksklass, annars `iconFallback`), Buttons namn och
    // regel. En större ikon gör knappen högre, och verktygsfältets
    // AUTO-tjocklek växer med den högsta synliga knappen. Negativt ⇒ logg +
    // no-op. Lägger om syskonen, som bilden.
    void  setIconSize(float px);
    float iconSize() const { return iconSize_; }

    // VÄXLINGEN. Av = en vanlig verktygsknapp (bara `onClick`). På = varje
    // ANVÄNDARklick slår `checked` om och fyrar `onChange` FÖRE `onClick`
    // — Checkboxens ordning, så den som lyssnar på klicket redan ser det
    // nya läget. Att slå av `checkable` lämnar `checked` orört: läget är
    // ett eget fält, inte en följd av det andra.
    void setCheckable(bool on)
    {
        if (checkable_ == on)
            return;
        checkable_ = on;
        invalidate();
    }
    bool isCheckable() const { return checkable_; }

    // Programmatisk sättning avfyrar ALDRIG onChange (kontraktet, alla
    // widgets). Tillåten även när knappen inte är `checkable` — ett läge
    // som appen själv styr (en radiogrupp byggd i onClick) är legitimt.
    void setChecked(bool on)
    {
        if (checked_ == on)
            return;
        checked_ = on;
        invalidate();
    }
    bool isChecked() const { return checked_; }

    // Avfyras när ANVÄNDAREN slår om en växlingsbar knapp — läs läget ur
    // sender (`isChecked()`), regel 6.
    Handler<ChangeArgs> onChange;
    // ANVÄNDAREN ÄR KLAR — direkt efter `onChange`, för samma
    // växling (och före `onClick`). En knapp som inte växlar fyrar inget.
    Handler<ChangeArgs> onCommit;

    // KLICKA PROGRAMMATISKT (WinForms `ToolStripItem.PerformClick`)
    // — det ett klick på knappen gör: en växlingsbar knapp slår om läget och
    // fyrar `onChange` och `onCommit`, sedan `onClick`. En no-op när knappen
    // är inaktiverad. Menyn rörs inte: också på en knapp med poster är det
    // KLICKET (den delade knappens huvuddel) — `openMenu()` öppnar menyn.
    // Verktygsfältets överflödsmeny aktiverar en dold knapp den här vägen.
    // Kör användarkod: knappen kan vara riven efteråt. Ingen `.glazeui`-form
    // (en handling, inte ett tillstånd) — previewn känner raden men utför
    // den inte, som `Button::performClick`.
    void performClick();

    // ------------------------------------------------------------ MENYN
    // Systemfältsikonens platta form med den TOMMA stigen `{}` som roten och
    // utan genvägsargument (filhuvudet). Okänd stig loggas och ignoreras
    // (regel 4-arvet). Den FÖRSTA posten ger knappen pilen och bredden.
    void addItem(const MenuPath& path, TrText text);
    void addCheck(const MenuPath& path, TrText text, bool checked = false);
    void addRadio(const MenuPath& path, TrText text, bool checked = false);
    void addSeparator(const MenuPath& path);
    void addSubmenu(const MenuPath& path, TrText text);

    // Handlern på en stig — TEMPLATE av `AppIcon::setHandler`s skäl (den
    // parameterlösa formen adapteras i `Handler::operator=`). Postens sender
    // är knappen. Previewn utför den inte; den kompilerade koden gör det.
    template <class F>
    void setHandler(const MenuPath& path, F&& f)
    {
        if (MenuItem* it = itemAtPath(path))
            it->onClick = std::forward<F>(f);
        else
            logMissingPath("setHandler", path);
    }

    // MENYPOSTENS TILLSTÅND — menyradens, programikonens och systemfältets
    // tre platta mutatorer, med SAMMA namn och samma implementation
    // (src/menu_path.h): postens bild, `enabled` och kryss, SAMMA tillstånd
    // som `itemAtPath(stig)->…`. Okänd stig loggas och ignoreras; den tomma
    // stigen är menyn och ingen post. Ett kryss på en radiopost avmarkerar
    // inte syskonen. En öppen meny visar sin snapshot tills den öppnas igen.
    void setItemIcon(const MenuPath& path, ImageSource icon);
    void setItemEnabled(const MenuPath& path, bool on);
    void setItemChecked(const MenuPath& path, bool on);

    // Posten på en stig, eller nullptr. Den tomma stigen är menyn och ingen
    // post, så den svarar nullptr.
    MenuItem* itemAtPath(const MenuPath& path);

    // TRÄD-API:t för handkod — SAMMA tillstånd som den platta formen.
    // Referensen är stabil (menyn är ett fält). Posterna är DATA och
    // invaliderar ingenting: ändras de utanför en handler medan knappen
    // syns, ritar appen om (regel 1) — och eftersom den FÖRSTA posten gör
    // knappen bredare är det fönstret (`appWindow()->invalidate()`) och inte
    // bara knappen. Den platta formen sköter det själv.
    Menu& menu() { return menu_; }

    // DEN DELADE KNAPPEN — huvuddelen klickar, pildelen öppnar menyn.
    void setSplit(bool on);
    bool isSplit() const { return split_; }

    // Öppna menyn UNDER knappen — samma väg som trycket, men utan
    // `onDropDown`. En knapp utan poster eller utanför ett öppet fönster
    // öppnar ingenting. `closeMenu()` stänger (uppskjutet, som allt
    // stängande).
    void openMenu();
    void closeMenu();
    bool isMenuOpen() const { return open_; }

    // ANVÄNDAREN trycker fram menyn — fyras FÖRE den öppnas, så handlern
    // kan fylla den. Inte vid `openMenu()`. Sender är knappen.
    Handler<EventArgs> onDropDown;

    // INGET tabbstopp och INGEN fokustjuv — se filhuvudet.
    bool focusable() const override { return false; }

    // setProp-egenskaper: text, img ("lista/nyckel"), imagePos
    // (left/right/above/below), checkable, checked, split, iconSize (ett
    // tal). Menyn byggs med add-satserna, inte med props.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT text — regel 8:s tr-väg. Utan den blir
    // `setText(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text") { setText(value); return true; }
        return Widget::setTextToken(key, value);   // basens toolTip
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    void       clicked() override;
    void       mouseDown(const Ctx& c, float mx, float my) override;
    // En inaktiverad verktygsknapp har ingen öppen meny (färgväljarens regel).
    void       enabledChanged() override;

    // RITAS PILEN? — förvalet är att en knapp med poster har den:
    // rutan `toolArrowW` i AUTO-bredden och pilen i ritningen. Utan pil finns
    // ingen pildel, så hela knappen är menyns också när den är delad.
    // Verktygsfältets överflödsknapp svarar false — dess ikon säger att den
    // öppnar en meny (tool_bar.cpp). Trycket följer POSTERNA och inte pilen.
    virtual bool showsArrow() const { return hasMenu(); }

private:
    // DET ETT KLICK GÖR — växlingen med `onChange`/`onCommit` och sedan
    // `onClick` (`firePair`, vakten emellan). `clicked()` (musen) och
    // `performClick()` går hit; menyns tryck aldrig.
    void fireClick();

    // VERKTYGSFÄLTETS ÖVERFLÖDE fyrar en dold knapps `onDropDown`
    // genom knappens EGEN vakt (`fireEvent`) när det bygger sin meny — knappens
    // meny står där som en undermeny, och en dynamisk meny fylls här.
    friend class ToolBar;

    // Bild + text som EN sak (Button-mönstret), med ikonen tonad när
    // knappen är inaktiv. `fg` är textfärgen — den väljs av ritgrenen.
    Content content(const Ctx& c, gfx::Color fg) const;

    // Hover, tryck eller öppen meny på en aktiv knapp — det som tänder
    // plattan i alla tre ritningarna (vilken platta avgör grenen).
    bool hot() const;
    // Den TRYCKTA plattan: ett tryck, eller menyn som står öppen.
    bool down() const { return pressed() || open_; }

    // Har knappen menyposter — alltså en pil och ett tryck som öppnar?
    bool hasMenu() const { return !menu_.items.empty(); }
    // PILENS RUTA i widget-lokala LOGISKA px, ytterst till höger i `b` —
    // EN geometri för ritningen och träffen i den delade knappen.
    gfx::Rectf arrowRect(const Ctx& c, const gfx::Rectf& b) const;
    // En add-sats lade till en post: rita om, och lägg om syskonen när den
    // första posten gav knappen pilen (bredden ändrades).
    void itemsAdded(bool hadItems);

    static void logMissingPath(const char* fn, const MenuPath& path);

private:
    TrText      text_;
    ImageSource img_{};                     // ogiltig = ingen bild
    ImagePos    imagePos_  = ImagePos::Left;
    float       iconSize_  = 0.0f;   // 0 = AUTO
    bool        checkable_ = false;
    bool        checked_   = false;
    bool        split_     = false;
    bool        open_      = false;   // menyn visas
    // DET HÄR TRYCKET VAR MENYNS (öppnade eller stängde den) och blir inget
    // klick — satt i `mouseDown`, läst i `clicked()` för samma tryck.
    bool        menuPress_ = false;
    Menu        menu_;
};

} // namespace glaze
