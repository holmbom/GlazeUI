#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "glaze/canvas/image_source.h"
#include "glaze/language.h"
#include "glaze/widgets/panel.h"

// MarkdownView — FORMATERAD TEXT I EN RULLANDE YTA (WinForms `RichTextBox`
// som läsvy, Qt `QTextBrowser`, wx
// `wxHtmlWindow`). Ett Markdown-dokument — release notes, hjälp, en
// beskrivning — med rubriker, stycken, listor, citat, kod, länkar, bilder och
// tabeller. MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() och allt
// beteende bor i src/widgets/markdown_view.cpp (hård regel 3); tolkningen och
// layouten är interna (src/markdown/).
//
// EN EGEN WIDGET MED EGET LAYOUTTRÄD, inte Label/Link/Picture i en Panel: en
// rad med ett fett ord mitt i går inte att uttrycka med en etikett, en widget
// per ord hade gett tusentals widgets, och varje `Link` är ett tabbstopp.
// Innehållet är DATA — inga barn, inga tabbstopp. RULLNINGEN ÄR PANELS
// (TextArea/ListBox-prejudikatet): overlay-remsorna, hjulet utan fokus,
// `border`/`clip` och autoskrollen.
//
// TEXTEN ÄR ETT `TrText` (Labels form, regel 6: `text`): `setText(glaze::tr(N))`
// gör hjälptexten översättbar utan appkod, och ett språkbyte tolkar om
// dokumentet av sig självt (cachen jämför den UPPLÖSTA texten). En NY text
// rullar till toppen; ett väntande `scrollToAnchor` står kvar, så ordningen
// mellan de två anropen inte spelar roll.
//
// DELMÄNGDEN — CommonMark 0.31.2:s kärna plus GFM-tabeller:
//   rubriker `#`–`######` och setext (`===`/`---`) · stycken med mjuk och hård
//   brytning · linjer · punktlistor och numrerade (start, nästling, täta och
//   glesa — numret ritas med källans `.` eller `)`) · kodblock med staket och
//   indragna (ingen syntaxfärgning, tabbar till stopp 4, raderna BRYTS) ·
//   citat, nästlade och lata · fet/kursiv med flankeringsreglerna · kod
//   inline · länkar inline och som referenser, `<url>` och nakna
//   `http(s)://`-adresser · bilder · escapes och entiteterna `amp lt gt quot
//   apos nbsp copy reg trade hellip mdash ndash` plus de numeriska · tabeller
//   med justering. SKRIVNA GRÄNSER: HTML tolkas ALDRIG — det står som text,
//   utom kommentarer `<!-- -->`, som tas bort; genomstrykning, uppgiftslistor,
//   fotnoter, matematik och emoji-koder står som text; `www.` och nakna
//   e-postadresser länkas inte; referensetiketterna viks bara för ASCII och
//   Latin-1; Unicode-skiljetecken räknas som bokstäver i betoningsreglerna;
//   en tabell som inte ryms klipps (ingen vågrät rullning); en fet stil som
//   fonten saknar ritas i vanlig vikt.
//
// RUBRIKERNA: H1 i `headingFontSize`, H2 i `subheadingFontSize`, H3–H6 i
// brödtextens storlek — alla FETA (en `Label` med `Role::Heading` är med flit
// vanlig vikt). Varje rubrik är ett ANKARE med GitHubs slug ("Kom igång" →
// `kom-igång`, dubbletter `-1`, `-2`), och `[se nedan](#kom-igång)` rullar dit.
//
// LÄNKARNA: `onLinkClick` fyras när trycket OCH släppet träffar samma länk
// (en knapps klick). Handlern körs FÖRST; sätter den `e.handled` händer inget
// mer. Annars förvalet: `#ankare` rullar, `http`, `https` och `mailto` går till
// webbläsaren respektive e-postprogrammet (`gfx::openUrl`), och ALLT ANNAT —
// `file:`, `javascript:`, en relativ stig — gör ingenting. Ett dokument kan
// komma utifrån, och `file:///…exe` hade kört ett program. Handen visas över
// en länk, och tooltipen är länkens titel eller, utan titel, adressen.
//
// BILDERNA: `![alt](namn)` slår upp `namn` i vyns egen tabell (`setImage`),
// därefter som asset `lista/nyckel` — men aldrig när namnet har ett schema
// (`https://…`): inga filer och inget nät (Networking är ett sidopaket som
// ramverket aldrig länkar). En app med PNG-filer gör
// `setImage("logo", glaze::Bitmap::decode(…))`. En bild bredare än raden
// krymps med bevarade proportioner och står på baslinjen; en som saknas
// ritas som en bildikon med alt-texten. Bildens titel är dess tooltip. Byter
// en Bitmap STORLEK efter `setImage` syns det först vid nästa `setImage` eller
// text — tabellen är nyckeln, inte pixlarna.
//
// HÖJDEN: med en SATT höjd (eller `Dock::Fill`) rullar vyn; med AUTO-höjd i en
// sträckt eller Top/Bottom-dockad vy växer den med sitt innehåll
// (`heightForWidth`, Labels tabell). AUTO-bredden är `Style::markdownDefaultW`.
//
// INTE HÄR — skrivna gränser: ingen MARKERING och
// inget urklipp, och vyn är INTE fokuserbar — varken ett tabbstopp eller
// tangentrullning, så länkarna nås bara med musen (tillgängligheten är
// inte byggd än).

// Dokumentet och layouten är interna (src/markdown/) — här står bara
// typnamnet de privata deklarationerna behöver.
namespace glaze::md {
struct Document;
} // namespace glaze::md

namespace glaze {

// Args till `onLinkClick` — VILKEN länk (CellChangeArgs/TabCloseArgs-formen:
// identiteten i args, värdena i sender).
struct LinkClickArgs : EventArgs {
    std::string url;              // målet som det står i dokumentet (efter escapes)
    bool        handled = false;  // UTDATA: true = appen tog länken, förvalet körs inte
};

class MarkdownView : public Panel {
public:
    explicit MarkdownView(TrText t = {});
    // Ur headern av TextAreas skäl: dokumentet och layouten är interna typer.
    ~MarkdownView() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR: settern är
    // den som INVALIDERAR. LAYOUT-fälten (x/y/width/height/right/bottom/
    // dock/anchors/tabIndex/name) är fortsatt RÅA — se kontraktet i widget.h.

    // DOKUMENTET — Markdown-källan. `text()` är den UPPLÖSTA texten,
    // `textToken()` identiteten (Labels par). En ny text (tokenjämförelse)
    // rullar vyn till toppen.
    void               setText(TrText t);
    const std::string& text() const { return text_.str(); }
    const TrText&      textToken() const { return text_; }

    // BILDTABELLEN — en NYCKLAD tabell (menystigens form: en adress, inte ett
    // index, så kollektionsvokabulären gäller inte). En ogiltig bild (`{}`)
    // tar bort posten; `image` svarar `{}` för ett okänt namn.
    void        setImage(const std::string& name, ImageSource img);
    ImageSource image(const std::string& name) const;

    // Rulla till rubriken med ankaret `anchor` (slugen, utan `#`). Löses vid
    // nästa ritning, mot det dokument som då gäller; ett okänt ankare loggas
    // en gång och rullar ingenting.
    void scrollToAnchor(std::string anchor);

    // Länken vid en punkt i vyns LOKALA LOGISKA px (`MouseArgs::local`s rymd —
    // snabbmenyns "Kopiera länk"), eller tom: ingen länk där, eller inget
    // öppet fönster.
    std::string linkAt(float localX, float localY) const;

    Handler<LinkClickArgs> onLinkClick;

    // setProp-egenskaper: `text`, därefter Panels (`border`, `clip`) och basens.
    bool setProp(const std::string& key, const std::string& value) override;

    // `text` tar tokenet (`setText(glaze::tr(N))` i `.glazeui`), därefter basen
    // (toolTip-kedjan).
    bool setTextToken(const std::string& key, const TrText& value) override
    {
        if (key == "text") {
            setText(value);
            return true;
        }
        return Widget::setTextToken(key, value);
    }

protected:
    gfx::Sizef           contentSize(const Ctx& c) const override;
    std::optional<float> heightForWidth(const Ctx& c, float width) const override;
    gfx::Sizef           contentExtent(const Ctx& c) const override;
    void                 draw(const Ctx& c) override;

    // Musen: remsan först (Panels), därefter länken. Klicket är en knapps —
    // trycket och släppet på SAMMA länk.
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    bool mouseUp(const Ctx& c, float mx, float my) override;   // true = en handler körde
    bool mouseCancel(gfx::MouseButton b) override;

    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;
    TrText            toolTipAt(const Ctx& c, float x, float y) const override;

private:
    struct Layouts;      // de två layoutplatserna (markdown_view.cpp)
    struct LayoutSlot;   // en plats: nyckeln, layouten och bilderna

    // Delen under en punkt, som INDEX i dokumentet (länken, bilden; −1 =
    // ingen) — aldrig en pekare in i layouten, som kan läggas om.
    struct Hit {
        int link  = -1;
        int image = -1;
    };

    const md::Document& document() const;
    ImageSource         imageNamed(const std::string& src) const;
    float               contentWidth(const Ctx& c, float widgetW) const;
    float               autoHeight(const Ctx& c, float widgetW) const;
    const LayoutSlot&   layoutAt(const Ctx& c, float bredd) const;
    gfx::Pointf         origin(const Ctx& c) const;
    std::optional<Hit>  hitAt(const Ctx& c, float mx, float my) const;
    void                followLink(const std::string& url);
    void                contentChanged();

    TrText                                           text_;
    std::vector<std::pair<std::string, ImageSource>> images_;
    std::uint64_t                                    imagesGen_ = 0;
    std::string                                      pendingAnchor_;

    // CACHARNA — `mutable` för att mätningen (const) tolkar och lägger ut.
    // Dokumentet tolkas om när den UPPLÖSTA texten skiljer sig från den det
    // tolkades ur, och då stegas generationen och länkindexen nollas: de
    // pekar in i det gamla dokumentet.
    mutable std::string                   parsedFrom_;
    mutable std::unique_ptr<md::Document> doc_;
    mutable std::uint64_t                 docGen_ = 0;
    mutable std::unique_ptr<Layouts>      layouts_;
    mutable int                           hoverLink_ = -1;
    mutable int                           pressLink_ = -1;
};

} // namespace glaze
