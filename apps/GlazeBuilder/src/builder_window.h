#pragma once
#include <glaze/glaze.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "asset_panel.h"
#include "component_tree.h"
#include "window_model.h"

// GlazeBuilder — WYSIWYG-editorn för tre-fils-modellen.
//
//   GlazeBuilder [fönster.h]      (ingen fil = nytt tomt fönster)
//
// Modellen är WindowModel (window_model.h), läst/skriven av cpp_reader/
// cpp_writer: buildern parsar BARA sin egen genererade delmängd innanför
// #pragma region GlazeBuilder + i *.glazeui — användarens kod läses
// aldrig som kod och klistras tillbaka byte för byte (splasa). Skadat
// stängsel ⇒ fail-safe: vägra redigera/spara, visa koden.
//
// Canvasen ritas med samma kod som appen använder: registret
// (glaze::makeWidget) + setProp + ett RIKTIGT adopterat träd under en
// ägarlös rot-panel — WYSIWYG är bokstavligt, och dockade barn ser sina
// syskon.
class BuilderWindow : public glaze::AppWindow {
public:
    explicit BuilderWindow(std::string hPath);

private:
    // Palett-tabellen bor i palette.h/.cpp:
    // bld::kPalette/bld::paletteByCpp — verktygslådans metadata.

    // Fält-metadata för egenskapspanelen: ingen
    // reflektion finns (setProp returnerar bara bool), så vokabulären —
    // vilka fält som är tal/bool/enum/text och vilka val de har — bor
    // HÄR, bredvid paletten. Okänt fält ⇒ läs-bar opak rad (regel 4:
    // aldrig låta användaren skriva rå C++-uttryckstext via gridden).
    // Integer är Number med heltalskrav: runtime-fältet ÄR en int, så
    // "2.5" hade trunkerats i C++ medan gridden fortsatte visa 2.5 —
    // modell, UI och verkligt värde hade sagt olika saker.
    enum class FieldKind { Text, Number, Integer, Bool, Enum, Raw };
    struct FieldMeta {
        const char* cppType;      // nullptr = alla typer
        const char* field;
        FieldKind   kind;
        const char* choices;      // ';'-separerad; "" = fritext
        const char* enumPrefix;   // C++-prefixet vid Enum ("glaze::Dock::")
    };
    static const FieldMeta kFieldMeta[];
    static const FieldMeta* fieldMeta(const std::string& cppType,
                                      const std::string& field);

    void frame(const glaze::Ctx& c) override;
    bool event(const gfx::Event& e) override;
    // SPRÅKBYTET (designtaggens `lang=`, eller appens eget byte): previewträdet
    // är ÄGARLÖST och når därför aldrig `Widget::languageChanged` — det byggs
    // om i stället, så det en widget härleder ur språket och lagrar
    // (talfältets decimaltecken) följer med.
    void onLanguageChanged() override;

    // Modell/fil-flödet.
    void openFile();                    // läs .h (+ syskon) → modell
    void newWindowModel();              // "nytt fönster"-mall i minnet
    void save();                        // Ctrl+S: splasa + total .glazeui
    void enterFailSafe(const std::string& why, bool showGenerated);

    // Preview-trädet (ägarlös rot — byggs om vid varje modelländring).
    // RITVANDRINGEN bor i `bld::drawPreviewTree` (preview_tree.h) — en funktion
    // i stället för en privat kopia av `AppWindow::drawTree`, och därmed
    // grindbar.
    void rebuildPreview();
    glaze::Widget* hitTestTree(glaze::Widget& w, const glaze::Ctx& c,
                               float mx, float my);
    int nodeIndexFor(glaze::Widget* w) const;

    // Egenskapspanelen (PropertyGrid — ramverkswidgeten, generisk):
    // modell → rader vid markering; rad → modell i onChange (omkodning
    // per FieldKind → node.set → rebuildPreview). Raderna byggs ALDRIG
    // om under pågående redigering (setRows dödar aktiv editor) — bara
    // vid markerings-/strukturändring.
    void refreshPropPanel();
    void commitPropEdit();              // griddens onChange-väg

    // Redigering.
    gfx::Rectf  canvasRect();
    // Innehållsytan minus de två panelerna — den yta där ett handtag får
    // greppas även när det sticker utanför canvasen.
    gfx::Rectf  workArea();
    glaze::Ctx  canvasCtx();

    // EN KOORDINATVÄG för markering, handtag, flytt och storleksändring.
    // `ctxForNode` ger den Ctx nodens `resolve()` ska räknas i — alltså
    // FÖRÄLDERNS area — genom att vandra HELA förälderkedjan och lägga på
    // ett `childArea` per led (samma vandring som `drawPreviewTree`/
    // `hitTestTree`). Att slå upp bara den direkta föräldern och anropa
    // dess `childArea` på rotcanvasen är FEL för ett barnbarn:
    // `Widget::resolve` tolkar alltid `c.area` som den aktuella förälderns
    // area, så morförälderns bidrag saknas — handtagen ritades och träffades
    // på fel plats, och `areaW/areaH` i ankarformlerna blev fel. Samma
    // genväg satt i `dragTo` och är omlagd på den här.
    glaze::Ctx  ctxForNode(int nodeIdx);

    // INVERSEN. `ctxForNode` ger
    // FÖRÄLDERNS area; den här ger den yta `Widget::resolve` faktiskt
    // placerar ett ICKE-DOCKAT barn mot — samma area minus alla SYNLIGA
    // dockade syskons remsor. De två är olika saker, och
    // varje ställe som räknar BAKLÄNGES (placering, drag, storleks-
    // ändring) måste använda den här: annars skriver buildern `x`/`y` i
    // ett annat koordinatsystem än det `resolve()` läser dem i, och en
    // släppt widget hoppar en menyradshöjd vid mouse-up.
    //
    // `nodeIdx < 0` (eller en nod utan preview-widget) betyder "en NY nod
    // på rotnivån": föräldern är canvasroten och ingen widget utesluts —
    // exakt vad `placeAt` behöver, eftersom koordinaterna räknas INNAN
    // preview-widgeten finns.
    //
    // Skickas ALDRIG till `resolve()`: den skär själv, och en redan skuren
    // area hade skurits två gånger.
    gfx::Rectf  layoutArea(int nodeIdx, const glaze::Ctx& pc) const;

    gfx::Rectf  selectionRect(int nodeIdx);   // resolve + ±2, FYSISKA px
    std::array<gfx::Rectf, 4> handleRects(const gfx::Rectf& r) const;
    bool        isDocked(int nodeIdx) const;

    // EN SYSKONANKRAD KANT ÄR LÅST — samma princip
    // som en dockad nod: docken respektive ankaret äger placeringen, och
    // UI:t ska inte lova vad draget inte kan hålla. Skillnaden är att detta
    // är en PER-KANT-fråga och inte en per-nod: `anchorLeft` låser bara
    // vänsterkanten, medan höger sida fortfarande går att dra.
    //
    // `handleActive` är den fråga både RITNINGEN och HIT-TESTEN ställer, så
    // det man ser och det man kan greppa inte kan glida isär — samma skäl
    // som `selectionRect`/`handleRects` delas.
    bool        edgeAnchored(int nodeIdx, const char* field) const;
    bool        handleActive(int nodeIdx, int corner) const;

    // MÅLNODEN för ett syskonankare, eller -1. Speglar
    // `Widget::earlierSibling`: samma förälder OCH tidigare i nodordningen
    // — och modellens nodordning ÄR previewträdets barnordning, eftersom
    // `rebuildPreview` adopterar i den. `gap` fylls vid träff.
    int         anchorTargetNode(int nodeIdx, const char* field,
                                 float& gap) const;

    // KOPPLINGARNA SKA SYNAS — annars är de osynlig magi som
    // flyttar saker utan att något i canvasen säger varför. Linje från den
    // ankrade kantens mitt till målets motstående kant, med avståndet som
    // text. BARA för den markerade noden i v1: alla samtidigt blir oläsligt
    // i ett tätt fönster.
    void        drawAnchorLinks(const glaze::Ctx& c, int nodeIdx);

    // ------------------------------------------------- KOPPLINGSLÄGET
    //
    // Egenskapspanelens Raw-rad går att skriva för hand, men det är inte
    // så någon vill dra en koppling. Läget är den riktiga vägen: dra från
    // A till B, välj KANT i en meny, klart. Formen är gamla iOS Interface
    // Builders.
    //
    // Läget är ett EGET musläge och inte en modifierare på det vanliga
    // draget: i läget betyder ett drag "koppla" och inte "flytta", och
    // därför ritas ingen markeringsram — UI:t ska aldrig visa en
    // affordans gesten inte längre har.
    void        toggleLinkMode();
    void        openLinkMenu(int fromNode, int toNode, float mx, float my);
    // Indexen duger inte över menyns livstid (menyn är icke-blockerande och
    // handlern körs senare), så kopplingen bär ID-STRÄNGAR och slås upp på
    // nytt när den verkställs.
    void        applyAnchorLink(const std::string& fromId,
                                const std::string& toId,
                                const std::string& field, float gap);

    void        placeAt(float lx, float ly);
    void        dragTo(int nodeIdx, float lx, float ly);
    void        resizeTo(int nodeIdx, float mx, float my);   // fysiska px
    void        removeSelected();

    // Varje vanlig nod bär `name = "<nodnamn>"`, en KANONISK rad som
    // migreras in vid öppning (`bld::migrateNames`) och valideras
    // (`bld::nameState` — en avvikande rad ger FAIL-SAFE). Det finns därför
    // bara ETT namn per nod, och `n.id` ÄR svaret på "vilket namn bär den
    // här noden?" — genererad kod sätter alltid `Widget::name`, så ett
    // ankare fungerar både i previewn och i den byggda appen.
    void        connectClickHandler(int nodeIdx);   // dubbelklicket

    // AUTO-NAMNET: `Button1`, `Button2`, … `base` är KLASSNAMNETS form
    // (`glaze::Button` → `Button`) och inte registrets gemena typnamn —
    // namnet är objektets namn, och ett objekt heter `Button1` i C#/
    // WinForms, inte `button1`.
    std::string genId(const std::string& base) const;
    // "glaze::Button" → "Button" (sista `::`-segmentet).
    static std::string classBaseName(const std::string& cppType);

    std::string hPath_;        // "" = namnlöst nytt fönster
    std::string guiPath_;
    std::string userCppPath_;
    std::string hNl_   = "\n";
    std::string cppNl_ = "\n";

    bld::WindowModel model_;
    bool newFile_  = false;    // tripletten skapas från mall vid spara
    bool failSafe_ = false;    // vägra redigera/spara; visa koden
    std::vector<std::string> failText_;

    // Handlers kopplade sedan senaste sparningen — appendas (deklaration
    // + stub) vid Ctrl+S, med substring-vakt mot dubbletter.
    std::vector<bld::EventBind> pendingHandlers_;

    std::unique_ptr<glaze::Panel>  previewRoot_;
    std::vector<glaze::Widget*>    previewByNode_;   // per nodindex (null ok)
    // De ICKE-VISUELLA medlemmarna — skapade och
    // utförda bredvid trädet i varje `rebuildPreview`. Ingenting ritar dem
    // (komponentfacket är fas 2), men de lever med samma satser som appen.
    std::vector<bld::PreviewComponent> previewComponents_;

    int           selected_ = -1;     // nodindex
    int           dragging_ = -1;
    float         dragOffX_ = 0.0f;   // greppunkt i widgeten, logiska px
    float         dragOffY_ = 0.0f;

    // STORLEKSÄNDRINGEN (hörnhandtagen). `rebuildPreview()` river hela det
    // gamla previewträdet, så tillståndet får bära ENDAST nodindex och
    // NUMERISK startgeometri — aldrig en `glaze::Widget*` eller en
    // referens över anropet (`dragTo` är säker i dag just för att den inte
    // gör det). Kanterna är i FÖRÄLDER-LOGISKA px, samma rymd som modellen.
    int           resizing_     = -1;
    int           resizeCorner_ = 0;   // 0=VÖ 1=HÖ 2=VN 3=HN (frame-ordn.)
    float         resizeL_ = 0.0f, resizeT_ = 0.0f;   // kanter vid greppet
    float         resizeR_ = 0.0f, resizeB_ = 0.0f;
    float         resizeGrabX_ = 0.0f, resizeGrabY_ = 0.0f;   // muspos då
    std::string   pendingType_;       // palettens cppType som väntar
    std::uint64_t lastClickMs_   = 0; // dubbelklick-detektering
    int           lastClickNode_ = -1;

    // KOPPLINGSLÄGETS tillstånd. `linkFrom_` är nodindex under
    // pågående gummibandsdrag och nollställs på mouse-up; menyns handler
    // bär i stället id-strängar (se `applyAnchorLink`).
    bool           linkMode_ = false;
    int            linkFrom_ = -1;
    float          linkX_ = 0.0f, linkY_ = 0.0f;   // muspos, fysiska px
    glaze::Button* linkBtn_ = nullptr;             // verktygsradens växel
    // MEDLEM och inte lokal variabel: `popupAt` är ICKE-BLOCKERANDE, och en
    // lokal `Menu` hade förstörts direkt — dess destruktor stänger sina
    // presentationer. Samma skäl som demons `snabbmeny_`.
    std::unique_ptr<glaze::Menu> linkMenu_;   // FÄRSK per öppning — se showLinkMenu

    glaze::Label* status_ = nullptr;
    glaze::PropertyGrid* propGrid_ = nullptr;   // högerpanelen

    // Asset-läget: den redigerade appens assets.bin + fönstret.
    bld::AssetDoc                assetDoc_;
    std::unique_ptr<AssetWindow> assetWin_;     // lat — öppnas via knappen
};
