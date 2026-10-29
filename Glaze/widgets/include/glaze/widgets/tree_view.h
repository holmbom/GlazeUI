#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gfx/color.h>

#include "glaze/widgets/column_panel.h"

// TreeView — TRÄDET (wxTreeCtrl, WinForms TreeView).
// En rullande lista vars rader är NODER i en hierarki: varje nod har en
// text, en valfri bild och egna barn, och en nod med barn kan fällas ut och
// ihop. MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() och
// radgeometrin bor i src/widgets/tree_view.cpp (hård regel 3); ärver Panel
// genom `ColumnPanel`, så klipp, hjulscroll och overlay-remsorna kommer
// gratis — exakt ListBoxens grund.
//
// KOLUMNERNA (tabelldata i en hierarki: profilering,
// egenskaper per grupp) är tabellens, ur den delade basen `ColumnPanel`:
// samma `Column`-aggregat, samma kollektionsvokabulär (`addColumn`/
// `setColumn(index, …)`/`setColumns(lista)`), samma huvud, avdelardrag,
// fyllkolumn och sorteringspil. KOLUMN 0 är NODENS text — med indrag, pil,
// kryss och ikon, kortad med ellips mot kolumnens kant — och kolumn 1…n
// nodens CELLER (`TreeNode::cells`, `setCell(stig, kolumn, text)`), i nodens
// textfärg som tabellens rad. Ett träd UTAN kolumner har inget huvud och
// ritar som ett vanligt träd; med kolumner rullar det i sidled som tabellen. Också
// BREDDEN EFTER INNEHÅLLET är tabellens: en kolumn med
// `Column::autoWidth` följer rubriken och de SYNLIGA nodernas innehåll —
// kolumn 0 mäts med indrag, pil, ruta och ikon — och ett dubbelklick på en
// avdelare skriver den anpassade bredden som en fast bredd.
// SORTERINGEN sker INOM SYSKON: varje barnlista visas i kolumnens naturliga
// ordning (`setSortColumn`, ett klick i huvudet), men STIGARNA ÄR APPENS —
// `{0, 2}` är fortsatt det tredje barnet appen lade in, var det än syns
// (tabellens visningsordning). Spannet med Shift, pilarna och Ctrl+A följer
// det man SER.
//
// NODERNA ÄR DATA, inte widgets — samma val som ListBox och PropertyGrid och
// av samma skäl: N noder som N widgets hade varit N tabbstopp, och trädet
// har inga widget-barn alls. ETT tabbstopp; pilarna navigerar.
//
// ADRESSEN ÄR EN INDEXSTIG (`TreePath`, `{0, 2}` = första toppnodens tredje
// barn) och inte en textstig som menyradens. Beslutet och skälet:
//
//   * NODTEXTER UPPREPAS bland syskon (två filer som båda heter "Ny") — en
//     textstig kan inte peka ut den andra. Menyradens titlar är unika per
//     konstruktion; ett träds noder är det inte.
//   * KOLLEKTIONSVOKABULÄREN är indexbaserad (regel 8):
//     `addItem(text)` · `setItem(index, …)` · `setItems(lista)`. Trädet tar
//     samma tre former med en STIG där listan har ett index —
//     `addNode(förälder, text)` · `setNode(stig, …)` · `setNodes(lista)` —
//     och FÖRÄLDERN först, i menyradens ordning (`addItem(stig, text)`).
//   * Formen KAN EMITTERAS: `{0, 2}` är en C++-listliteral som grammatiken
//     redan bär som argument, och previewn avkodar den strikt
//     (`bld::parseIndexPath`). Ett pekar- eller referens-API (`TreeNode&`
//     som kedjas) hade inte kunnat stå i `.glazeui` alls.
//
// VALET, KRYSSET OCH UTFÄLLNINGEN FÖLJER NODEN, inte positionen: varje nod
// bär ett stabilt internt id och sina egna flaggor, så en nod som läggs in
// FÖRE en vald flyttar inte valet till en annan nod, och en nod som dras
// till en ny plats är fortsatt vald och kryssad. Stigarna räknas fram vid
// varje fråga.
//
// FLERVALET är LISTVYNS: enkelval som standard,
// och med `setMultiSelect(true)` Utforskarens form —
//   klick            välj BARA noden (en nod som REDAN är vald väljs ensam
//                    först när knappen SLÄPPS utan drag — annars hade det
//                    aldrig gått att dra ett helt val)
//   Ctrl+klick       vänd noden (Cmd+klick på macOS; båda räknas överallt)
//   Shift+klick      spannet från ANKARET — Ctrl+Shift+klick lägger till det
//   pilar/Home/End   flytta den AKTUELLA noden och välj bara den; med Shift
//                    spannet från ankaret, med Ctrl utan att välja
//   Ctrl+Space       vänd den aktuella noden
//   Ctrl+A           välj de SYNLIGA noderna
// Spannet räknas i SYNLIG ordning (raderna man ser — en hopfälld grens barn
// ingår inte), som listvyns visningsordning. Musens modifierare läses ur
// `AppWindow::modifierKeys()`. Den AKTUELLA noden är den tangentbordet står
// på (`currentPath()`); i enkelval är den alltid den valda. Med flerval och
// fokus ritas en hårlinje runt den, som i listvyn.
//
// KRYSSEN är ListBoxens: `setShowChecks(true)` visar en ruta per
// nod — efter pilen, före ikonen, i ListBoxens mått — och `setChecked(stig,
// på)`/`isChecked(stig)` är tillståndet. Kryssen är OBEROENDE: ingen kaskad
// mellan förälder och barn. Ett klick i rutans zon (rutan + `checkGap`, hela
// radens höjd) vänder krysset men flyttar INTE valet, ett dubbelklick där är
// två vändningar och ingen aktivering, och ett drag startar aldrig därifrån.
// Med synliga kryss vänder Space den aktuella nodens kryss — också i
// enkelval, och före valet. Rutan och bocken följer TEMAT, inte nodens färg.
// `setNode` lämnar krysset (tillstånd, som utfällningen), och massformens
// `checked` är startvärdet.
//
// TEXTFÄRGEN PER NOD är appens (`setNodeColor(stig, gfx::Color{…})`
// eller massformens sjunde fält) — en status och inte en widgetfärg, så inga
// temaroller tillkommer och regel 3 rubbas inte. Ingen satt färg är temats
// textfärg. Färgen gäller TEXTEN: pilen och kryssrutan följer temat (en röd
// bock hade lästs som ett fel i kryssets eget tillstånd). MARKERINGENS
// textfärg vinner på en vald rad, och ett inaktivt träd dimmar allt till
// `textDim`. Kontrasten mot ytan är appens ansvar.
//
// Värden läses ur SENDER (regel 6): `selectedPath()`/`selectedPaths()`/
// `currentPath()`/`changedPath()`/`isChecked(…)`, aldrig ur args. `onChange`
// fyras EXAKT EN gång per ANVÄNDARhandling som ändrade valet, `onCheck` när
// ANVÄNDAREN vänder ett kryss (nodens stig i `changedPath()`), `onExpand`/
// `onCollapse` när ANVÄNDAREN fäller ut/ihop; programmatiska settrar
// avfyrar ALDRIG något (kontraktet för varje widget).
//
// AKTIVERINGEN är listvyns: `onActivate` vid DUBBELKLICK eller ENTER på den
// aktuella noden (WinForms `NodeMouseDoubleClick`, filträdets "öppna") —
// samma event och samma ord, för varje lista med poster aktiverar likadant.
// Ett dubbelklick på en nod MED barn fäller den dessutom ut eller ihop, före
// aktiveringen (Windows trädvy); ett dubbelklick på PILEN är två fällningar
// och ingen aktivering, för varje tryck på pilen fäller redan.
//
// SNABBMENYN är basens `onContextMenu`, och
// noden läses ur SENDERN (regel 6): `contextPath()` är noden under det
// SENASTE högertrycket — `{}` på tom yta, i remsan, när trädet är inaktiverat
// (routingen hoppar då över det, och en förfaders handler ska få "ingen") och
// när noden försvunnit (den hålls vid id, inte vid stig). Värdet
// står kvar till nästa högertryck, så menyns handlers läser samma nod.
// Högertrycket VÄLJER som Utforskaren: en nod utanför valet väljs ensam
// (användarvägen — `onChange` + `onCommit`), en nod i valet lämnar valet och
// blir bara aktuell. Fokus flyttas inte. Träffvägen för egna
// gester är `pathAt(localX, localY)` i `MouseArgs::local`s rymd.
//
// DRAG-AND-DROP är OPT-IN med basens egenskaper: `setAllowDrag
// (true)` gör noderna till KÄLLOR, `setAllowDrop(true)` gör trädet till ett
// MÅL, och med båda går noder att FLYTTA inom trädet — före, efter eller IN
// I en annan nod. Lasten fylls med nodernas stigar i `kNodeFormat` och deras
// texter i `text/plain`, insättningslinjen ritas av trädet självt
// (`ownsDropIndicator`), och en FLYTT TILL ETT ANNAT MÅL tar bort noderna här
// (`Move` betyder att posten lämnar källan). Är den TRYCKTA noden vald dras
// HELA VALET, beskuret till sina översta noder (en vald ättling följer med
// sin förälder); annars bara den tryckta. Ett drag börjar aldrig på pilen, i
// rutan eller i remsan (`canDragFrom`, widget.h) — inte heller när appens
// `onDragStart` fyller lasten.
//
// APPENS GODKÄNNANDE — trädet FÖRESLÅR, appen
// AVGÖR:
//   förslaget  egna noder tas emot som `Move` på varje plats där det går
//              (inte inuti en dragen nod, inte där föräldern säger nej).
//   vetot      i `onDragOver`, som fyras på VARJE plats — också den första,
//              direkt efter `onDragEnter`: `e.effect = None` släcker linjen
//              och ger förbudsmärket. Trädet föreslår om platsen vid varje
//              rörelse, så ett veto i `onDragEnter` håller inte.
//   släppet    `onDrop` körs FÖRE den inbyggda flytten och ser det OFLYTTADE
//              trädet. `e.effect = None` nekar — trädet rörs inte — och
//              `e.handled = true` betyder att appen utförde släppet själv
//              (`moveNodes`, `setNodes` …), så det inbyggda hoppar över.
//              Annars flyttar trädet till EXAKT den lucka appen såg: den
//              hålls vid förälderns och grannarnas id och räknas inte om ur
//              pekaren. Har handlern ändrat trädet så att luckan inte finns
//              längre (en granne borta, något insatt emellan, föräldern
//              borta) blir släppet `None`, aldrig en tyst flytt till fel
//              ställe — och likaså om en dragen nod försvann.
//   kopian     sätter appen `Copy` flyttar det inbyggda ingenting: kopian är
//              appens att göra.
//   resultatet det slutliga valet går till källan i `onDragEnd`.
// Läsvägarna går genom sender: `draggedPaths()` (de dragna noderna, i
// trädordning), `dropParent()`/`dropIndex()` (luckan). Ett släpp från något
// annat än trädet självt tas INTE emot av det inbyggda — appen avgör i
// `onDragOver` och utför i `onDrop`, på samma läsvägar.
//
// REGELN PER NOD: `setAllowDropInto(stig, false)` stänger av "in i"
// för EN nod — en fil i ett filträd ska inte kunna bli en annan fils barn.
// Före och efter noden går fortfarande: radens mittzon delas då mellan dem
// (övre halvan före, nedre efter), så markeringen aldrig ramar in noden. Det
// är en regel om FÖRÄLDERN ett släpp landar i, och den gäller därför också
// en sådan nods eventuella barn (lagda med kod): före eller efter dem vore
// också "in i" noden, så där finns ingen plats alls — `dropIndex()` är −1
// och det inbyggda avvisar. `dropParent()` pekar alltså aldrig på en nod som
// säger nej, och det gäller lasten från en annan källa också. Regeln rör bara
// SLÄPPET: `addNode`/`moveNode` från koden går som vanligt. Den följer NODEN
// (id:t) när noden flyttas, och `setNode` lämnar den som utfällningen.
//
// SKRIVNA BEGRÄNSNINGAR (läs innan du "fixar" dem — de är beslut):
//
//   * INGEN AUTOSTORLEK. ListBox-prejudikatet: ett träd är en yta APPEN
//     dimensionerar. `width`/`height` 0 ⇒ 0.
//   * UTAN KOLUMNER ingen horisontell scroll och ingen ellips — ListBoxens
//     gräns. En djup nod med lång text KLIPPS mot innehållsytan.
//   * MED KOLUMNER sticker pilen, rutan och ikonen ut i nästa kolumn när
//     kolumn 0 är smalare än indraget (tabellens ikonregel — texten kortas,
//     inget klipps). Ingen sorteringsnyckel per cell: syskonen jämförs
//     naturligt på texten (`Table::setSortKey` har ingen tvilling här). Ett
//     SLÄPP i ett sorterat träd hamnar i luckan appen ser i DATA, men visas
//     på sin sorterade plats — linjen visar alltså förälderns nivå.
//   * KRYSSEN KASKADERAR INTE (oberoende kryss) — och har inget
//     tredje "delvis"-läge. Inga linjer mellan nivåerna (moderna träd —
//     Windows 11, macOS — ritar inga).
//   * Ctrl+A och `selectAll()` väljer de SYNLIGA noderna — valet ska vara det
//     man ser, så att en "Ta bort" aldrig träffar en nod i en stängd gren.
//     Ingen gummibandsmarkering med musen.
//   * Dragbilden är den tryckta nodens text och bild — ANTALET dragna noder
//     visas inte.
//   * INGEN PageUp/PageDown och ingen typeahead (ListBoxens skäl), och inte
//     numpadens `+`/`−`/`*`. Numpadens navigationsläge viks av `AppWindow`.
//   * AUTOSKROLLEN under ett drag är RAMVERKETS (`Widget::dragScroll`, varje
//     rullande yta), och hjulet rullar också mitt
//     i draget. AUTO-UTFÄLLNING under ett drag finns inte.

namespace glaze {

// En NODS ADRESS, index för index från roten. `{}` är roten själv —
// toppnodernas förälder — så `addNode({}, "Dokument")` lägger en toppnod.
using TreePath = std::vector<int>;

// MASSFORMENS nod — `setNodes` tar en lista av dem. Ett AGGREGAT med fälten
// i den ordning de oftast behövs (text, barn, bild, utfälld, släpp IN I,
// kryssad, färg), så samma rad är både handkod och `.glazeui`:
//
//     tree.setNodes({{"Rot", {{"Barn 1"}, {"Brev.txt", {}, {}, false, false,
//                                          true, gfx::Color{200, 40, 40}}},
//                     {}, true},
//                    {"Annan"}});
//
// Fälten växer SIST, så varje äldre rad betyder det den betydde: det femte är
// `setAllowDropInto`, det sjätte `setChecked`, det sjunde
// `setNodeColor` och det åttonde cellerna —
// samma tillstånd som de platta satserna ger. `.glazeui` tar aggregatet
// positionellt eller DESIGNERAT (`{.text = "Brev", .checked = true}`), med
// eller utan typnamnet. Färgen är en `std::optional`, så en bar klammerlista
// (`{200, 40, 40}`) kompilerar inte — skriv `gfx::Color{…}`.
struct TreeNode {
    TrText                    text;
    std::vector<TreeNode>     children;
    ImageSource               img{};
    bool                      expanded      = false;
    bool                      allowDropInto = true;
    bool                      checked       = false;
    std::optional<gfx::Color> color{};                 // tom = temats
    // CELLERNA för kolumn 1…n — `cells[0]` är kolumn 1, för kolumn
    // 0 är `text`. Strängar och inte `TrText`: innehåll, som tabellens celler.
    std::vector<std::string>  cells{};
};

class TreeView : public ColumnPanel {
public:
    TreeView();

    // DRAG-LASTENS FORMAT — de dragna nodernas stigar som text, åtskilda med
    // `;` i TRÄDORDNING (`"0,2;1"`; en ensam nod `"0,2"`, samma form som
    // `setProp("selectedPath", …)`). Ett eget format och inte `text/plain`
    // med flit: ett mål som bara läser text ska få nodernas TEXTER (åtskilda
    // med `\n`), och ett mål som förstår träd ska kunna känna igen noderna.
    static constexpr const char* kNodeFormat = "application/x-glaze-treenode";

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR: settern är
    // den som INVALIDERAR. LAYOUT-fälten (x/y/width/height/right/bottom/
    // dock/anchors/tabIndex/name) är fortsatt RÅA — se kontraktet i widget.h.

    // -------------------------------------------- KOLLEKTIONSVOKABULÄREN
    //
    // `addX` · `setX(adress, …)` · `setXs(lista)` (regel 8 — samma form som
    // ListBox/Dropdown/PropertyGrid, med en STIG där de har ett index).
    // Ogiltig stig ⇒ `gfx::log` + no-op, aldrig fail-safe och aldrig tyst
    // tillväxt — samma regel som `ListBox::setItem`.

    // Lägg till en nod SIST bland förälderns barn. Svarar med den nya
    // nodens stig (tom vid ogiltig förälder) — för HANDKODEN, som kan
    // kedja `auto p = tree.addNode({}, "Rot"); tree.addNode(p, "Barn");`.
    // `.glazeui` skriver stigen som literal och använder aldrig svaret.
    // `cells` är kolumn 1…n, som `TreeNode::cells`.
    TreePath addNode(const TreePath& parent, TrText text, ImageSource img = {},
                     std::optional<gfx::Color> color = std::nullopt,
                     std::vector<std::string> cells = {});

    // ERSÄTT nodens EGNA fält — text, bild, färg OCH celler, så en utelämnad
    // bild blir ingen bild, en utelämnad färg temats och utelämnade celler
    // tomma (tilldelning, inte partiell uppdatering — tabellens `setRow`).
    // Barnen, utfällningen, valet och krysset rörs INTE: de är inte nodens
    // fält utan trädets struktur och tillstånd, och att tömma ett delträd för
    // att en rubrik byttes vore att förstöra data.
    void setNode(const TreePath& path, TrText text, ImageSource img = {},
                 std::optional<gfx::Color> color = std::nullopt,
                 std::vector<std::string> cells = {});

    // FYLL OM ALLT. Valet och den aktuella noden nollas (ingen nod finns kvar
    // att följa) och scrollen klampas, precis som `ListBox::setItems`.
    void setNodes(std::vector<TreeNode> nodes);

    // Ta bort en nod och hela dess delträd. Var den — eller något i den —
    // VALD eller AKTUELL försvinner det ur valet (utan event — det var inget
    // användarval).
    void removeNode(const TreePath& path);

    // FLYTTA en nod (med delträd) till `index` bland `toParent`s barn —
    // indexet räknas som om noden REDAN vore borttagen, så att flytta inom
    // samma förälder blir det man ser. Ogiltigt: okänd källa eller
    // förälder, index utanför [0, antal], eller ett mål INUTI noden själv.
    // Svarar false + logg då, och ingenting ändras. Valet följer noden.
    bool moveNode(const TreePath& from, const TreePath& toParent, int index);

    // FLYTTA FLERA — samma semantik som släppet:
    // indexet räknas i FÖRE-flytten-koordinater (`moveNode`s), en nod som
    // ligger INUTI en annan i listan följer med sin förfader och flyttas inte
    // för sig, och noderna sätts in i TRÄDORDNING. Valet, kryssen och
    // utfällningen följer noderna. ATOMISKT: varje stig och målet valideras
    // FÖRE någon mutation — en ogiltig stig, en tom lista, ett mål inuti en
    // av noderna eller ett index utanför [0, antal] ger false + logg, och
    // ingenting ändras. `draggedPaths()` ger exakt indata, så en `onDrop` som
    // utför släppet själv skriver
    //     t->moveNodes(t->draggedPaths(), t->dropParent(), t->dropIndex());
    // `.glazeui`: `trad.moveNodes({{0, 1}, {2}}, {1}, 0);`.
    bool moveNodes(const std::vector<TreePath>& from, const TreePath& toParent,
                   int index);

    // ------------------------------------------------------------ läsvägen
    int                nodeCount(const TreePath& parent = {}) const;
    bool               hasNode(const TreePath& path) const;
    // Den UPPLÖSTA texten — tom sträng vid ogiltig stig. Referensen gäller
    // tills trädet ändras (samma kontrakt som `ListBox::items()`).
    const std::string& nodeText(const TreePath& path) const;
    // IDENTITETEN — literal eller översättnings-id, som `textToken()`.
    const TrText&      nodeTextToken(const TreePath& path) const;
    // Bilden — en asset eller pixlar; en ogiltig vid ogiltig stig. Referensen
    // gäller tills trädet ändras.
    const ImageSource& nodeImg(const TreePath& path) const;

    // --------------------------------------------- textfärgen
    // `std::nullopt` eller `{}` = temats textfärg. Ogiltig stig ⇒ logg +
    // no-op; invaliderar bara vid ändring. `nodeColor` är tom vid ogiltig
    // stig.
    void                      setNodeColor(const TreePath& path,
                                           std::optional<gfx::Color> color);
    std::optional<gfx::Color> nodeColor(const TreePath& path) const;

    // ------------------------------------------------ cellerna
    // EN CELL — tabellens `setCell(rad, kolumn, text)` med en STIG där
    // tabellen har ett index. Kolumn 1…n; kolumn 0 är nodens text och byts
    // med `setNode` (logg + no-op här). Kolumnen behöver INTE finnas än — en
    // `.glazeui` sätter kolumnerna och noderna i källordning — och en nod
    // med färre celler fylls ut med tomma. Ogiltig stig eller en negativ
    // kolumn ⇒ logg + no-op; invaliderar bara vid ändring. `cell` svarar med
    // nodens text för kolumn 0 och en TOM sträng utanför; referensen gäller
    // tills trädet ändras.
    void               setCell(const TreePath& path, int column, std::string text);
    const std::string& cell(const TreePath& path, int column) const;

    // ---------------------------------------------------------- utfällning
    // Programmatisk: invaliderar, avfyrar INGET och flyttar inget val. Ett
    // utfällt LÖV är tillåtet (det blir synligt utfällt den dag det får barn).
    void setExpanded(const TreePath& path, bool on);
    bool isExpanded(const TreePath& path) const;

    // ------------------------------------------------- släpp IN I
    // Får en dragen nod släppas IN I noden — bli dess barn? Default ja; nej
    // gör noden till ett LÖV för drag-and-drop (före/efter går alltid — se
    // filhuvudet). Programmatisk: invaliderar inget (ingenting syns förrän
    // ett drag), avfyrar inget; ogiltig stig ⇒ logg + no-op, och svaret är
    // false för en nod som inte finns.
    void setAllowDropInto(const TreePath& path, bool on);
    bool allowDropInto(const TreePath& path) const;

    // -------------------------------------------------------- kryssen
    // Rutorna syns eller inte; tillståndet finns oavsett. `setChecked` är
    // programmatisk (avfyrar aldrig `onCheck`), ogiltig stig ⇒ logg + no-op,
    // och `isChecked` är false för en nod som inte finns. Ingen kaskad.
    void setShowChecks(bool on);
    bool showsChecks() const { return showChecks_; }
    void setChecked(const TreePath& path, bool on);
    bool isChecked(const TreePath& path) const;
    // Alla kryssade i TRÄDORDNING — också de i en hopfälld gren
    // (`selectedPaths()`s parhäst).
    std::vector<TreePath> checkedPaths() const;

    // ---------------------------------------------------------------- valet
    // Programmatisk sättning: ändrar, invaliderar, avfyrar INGET — och
    // scrollar INTE (`ensureVisible` finns). Ogiltig stig ⇒ logg + no-op.

    // FLERVAL av/på. Av (standard) ⇒ högst EN vald nod, och den är den
    // aktuella; slås det av med flera valda behålls bara `selectedPath()`.
    void setMultiSelect(bool on);
    bool isMultiSelect() const { return multiSelect_; }

    // Välj BARA noden och gör den aktuell. `{}` = inget val — och då ingen
    // aktuell nod heller (trädets kontrakt: Enter bubblar utan val; här
    // avviker trädet från listvyn, som behåller sin aktuella rad).
    void        setSelectedPath(const TreePath& path);
    // DEN valda noden: den aktuella om den är vald, annars den första valda
    // i TRÄDORDNING (dold eller inte) — `{}` när inget är valt.
    TreePath    selectedPath() const;
    std::string selectedText() const;   // `selectedPath()`s text

    // Noden för sig. Utan flerval väljer `setSelected(stig, true)` BARA den
    // (och gör den aktuell), och `false` på den valda lämnar inget valt.
    void setSelected(const TreePath& path, bool on);
    bool isSelected(const TreePath& path) const;
    // Alla valda i TRÄDORDNING — också de i en hopfälld gren.
    std::vector<TreePath> selectedPaths() const;
    // Den nod tangentbordet står på (listvyns `currentRow()`) — `{}` utan.
    TreePath currentPath() const;

    void selectAll();        // de SYNLIGA — bara med flerval
    void clearSelection();   // allt, och den aktuella noden

    // Vilken nod som fälldes ut eller ihop, eller vars kryss vändes — läs i
    // `onExpand`/`onCollapse`/`onCheck` (och dess `onCommit`) (regel 6,
    // ListBoxens `changedRow()`). `{}` efter en ren valhändelse.
    const TreePath& changedPath() const { return changedPath_; }

    // SNABBMENYN (se filhuvudet). `pathAt` är
    // träffvägen i LOGISKA px från trädets hörn (`MouseArgs::local`,
    // `DragArgs::localX/localY`): `{}` nedanför sista raden, i remsan,
    // utanför innehållsytan och utan ett öppet fönster. `contextPath` är
    // noden under det SENASTE högertrycket — läs den i `onContextMenu` och i
    // menyns handlers; `{}` när trädet är inaktiverat eller noden borta.
    TreePath pathAt(float localX, float localY) const;
    TreePath contextPath() const;

    // Var ett släpp HAMNAR, giltigt under ett drag över trädet (läs i
    // `onDragOver`/`onDrop`): lasten blir barn nummer `dropIndex()` till
    // `dropParent()`, indexet i FÖRE-flytten-koordinater (`moveNodes`).
    // Luckan hålls vid förälderns och grannarnas ID och LÖSES vid varje
    // fråga, så stigen är by value och gäller också
    // när `onDrop` ändrat trädet. Utanför ett drag, där ingen plats finns och
    // när luckan inte längre finns: `{}` och −1.
    TreePath dropParent() const;
    int      dropIndex() const;
    // DE DRAGNA NODERNA — i TRÄDORDNING, läst ur sender (källan): från
    // `onDragStart` till och med målets `onDrop`. Tom före tröskeln, i
    // `onDragEnd` (draget är redan avslutat) och efter en start som avböjdes.
    // En dragen nod som tagits bort under draget saknas i svaret.
    std::vector<TreePath> draggedPaths() const;

    // Valet ändrades av ANVÄNDAREN (klick, Ctrl/Shift, pilar, Home/End,
    // Ctrl+A, Vänster upp till föräldern, eller en ihopfällning som gömde
    // något valt).
    Handler<ChangeArgs> onChange;
    // ANVÄNDAREN ÄR KLAR — direkt efter `onChange` för samma val,
    // och direkt efter `onCheck` för samma kryss (skilj dem åt med
    // `changedPath()`, som är `{}` för ett val). En ut- eller ihopfällning är
    // inget värde och fyrar inget här.
    Handler<ChangeArgs> onCommit;
    // ANVÄNDAREN vände ett kryss (rutan, Space) — nodens stig i
    // `changedPath()`, och `onCommit` direkt efter.
    Handler<ChangeArgs> onCheck;
    // ANVÄNDAREN fällde ut respektive ihop en nod (pilen, Höger/Vänster) —
    // nodens stig i `changedPath()`. Det är här ett LATT träd fyller på
    // barnen, och handlern får ändra trädet fritt.
    Handler<ChangeArgs> onExpand;
    Handler<ChangeArgs> onCollapse;
    // ANVÄNDAREN aktiverade den aktuella noden — dubbelklick eller Enter
    // (listvyns event, se filhuvudet). Noden läses ur `currentPath()`; i
    // enkelval är den `selectedPath()`. Utan handler tar trädet inte Enter
    // — tangenten bubblar till dialogens standardknapp.
    Handler<ActivateArgs> onActivate;

    bool focusable() const override { return true; }

    // setProp-egenskaper: selectedPath ("0,2" — TEXTformen; C++-literalen
    // `{0, 2}` går via `bld::parseIndexPath` i verktygsvägen, och de
    // klamrarna tas också emot här), multiSelect och showChecks; sorteringen,
    // huvudet och fyllkolumnen tar ColumnPanel.
    bool setProp(const std::string& key, const std::string& value) override;

    // Fäll ut förfäderna och rulla raden i synfältet (WinForms
    // `EnsureVisible`). Utfällningen sker alltid; rullningen kräver ett
    // öppet fönster (annars no-op, som `ListBox::ensureVisible`).
    void ensureVisible(const TreePath& path);

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Raderna är DATA ⇒ basens barn-extent räcker inte. Bredden är
    // VIEWPORTENS (ingen horisontell scroll) — med kolumner kolumnernas,
    // som tabellens.
    gfx::Sizef contentExtent(const Ctx& c) const override;

    // KOLUMNERNA — basens krokar: huvudet finns bara med
    // kolumner, en ny ordning bygger om raderna (syskonen sorteras när
    // raderna byggs), ett klick i huvudet rullar den aktuella noden i
    // synfältet, och ett språkbyte kan ändra nodtexternas ordning.
    bool hasHeader() const override { return columnCount() > 0; }
    void orderChanged() override;
    void headerSorted() override;
    void languageChanged() override;
    // BREDD EFTER INNEHÅLLET — `Column::autoWidth` och dubbelklicket
    // på en avdelare mäter de SYNLIGA raderna: kolumn 0 är nodens block
    // (indrag, pil, ruta, ikon, text), kolumn 1…n cellerna. Allt som ändrar
    // det — raderna (`rowsChanged`), en nods text, ikon eller celler och
    // rutornas synlighet — säger `contentChanged()`.
    float cellContentW(const Ctx& c, int column) const override;

    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    // SOLO VID SLÄPPET: ett tryck på en redan vald nod i ett flerval
    // väljer bara den först här — ett tryck som blev ett drag får ingen
    // `mouseUp` (widget.h), och då drogs hela valet.
    bool mouseUp(const Ctx& c, float mx, float my) override;
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    // Högertrycket: noden blir `contextPath()` och väljs som i
    // Utforskaren. Svarar ALLTID false, så snabbmenyn fyras.
    bool rightDown(const Ctx& c, float mx, float my) override;

    // DRAG-AND-DROP — det inbyggda (se filhuvudet och krokarna i widget.h).
    // Inget drag från remsan, pilen eller rutans zon — SAMMA frågor som
    // trycket ställer (`onExpander`/`onCheckZone`).
    bool canDragFrom(const Ctx& c, float x, float y) const override;
    void dragStart(const Ctx& c, DragStartArgs& e) override;
    void dragEnd(DragEndArgs& e) override;
    void dragEnter(const Ctx& c, DragArgs& e) override;
    void dragOver(const Ctx& c, DragArgs& e) override;
    void dragLeave() override;
    void drop(const Ctx& c, DragArgs& e) override;
    bool ownsDropIndicator() const override { return true; }

private:
    // Den INTERNA noden. `id` är stabilt över hela nodens liv (aldrig
    // återanvänt) och är det den aktuella noden, ankaret, ett drag och
    // radcachen håller fast vid — positioner flyttar sig, id:n gör det inte.
    // Valet och krysset är FLAGGOR i noden, så de följer den utan bokföring.
    struct Node {
        TrText                    text;
        ImageSource               img{};
        std::optional<gfx::Color> color;
        std::vector<std::string>  cells;   // kolumn 1…n
        bool                      expanded      = false;
        bool                      allowDropInto = true;
        bool                      selected      = false;
        bool                      checked       = false;
        std::uint64_t             id            = 0;
        std::vector<Node>         kids;
    };

    // En SYNLIG rad: noden, dess djup och dess stig. Byggs om LATT när
    // trädet eller utfällningen ändrats (`rowsDirty_`) — `contentExtent` och
    // ritningen är const, därav `mutable`. Pekarna gäller bara tills nästa
    // mutation, och varje mutation sätter flaggan. Valet och krysset bygger
    // INTE om cachen.
    struct Row {
        const Node* n     = nullptr;
        int         depth = 0;
        TreePath    path;
    };
    const std::vector<Row>& rows() const;
    void                    rowsChanged();   // flagga + invalidera

    // Stigens nod / dess barnlista. nullptr vid ogiltig stig; `{}` ger
    // rotlistan i `kids()`.
    Node*                    node(const TreePath& p);
    const Node*              node(const TreePath& p) const;
    std::vector<Node>*       kids(const TreePath& parent);
    const std::vector<Node>* kids(const TreePath& parent) const;
    bool                     pathOf(std::uint64_t id, TreePath& ut) const;
    Node                     fromPublic(TreeNode&& t);
    int                      rowOf(std::uint64_t id) const;   // −1 = dold/ingen

    // ---- VALET. `assignSelection` sätter flaggorna EXAKT till `ids` i
    // hela trädet, invaliderar vid ändring och svarar om något ändrades —
    // användarvägarna fyrar bara då. De andra är läsvägar i TRÄDORDNING.
    bool                       assignSelection(std::vector<std::uint64_t> ids);
    std::vector<std::uint64_t> selectedIds() const;
    std::vector<std::uint64_t> topLevelSelected() const;   // utan ättlingar
                                                           // till andra valda
    std::vector<std::uint64_t> visibleIds() const;         // raderna
    // Nollar den aktuella noden, ankaret och det väntande solovalet om de
    // ligger i `rot`s delträd (rot själv inräknad) — före en borttagning.
    void forgetIn(const Node& rot);

    // ---- RADGEOMETRIN, EN sanning i LOGISKA px (scroll-förskjuten) som
    // ritningen läser rakt av och musvägen projicerar. ListBoxens mönster.
    // Raderna står i basens rullningsytan (`scrollViewport` — under huvudet
    // när trädet har kolumner, annars hela innehållsytan), och pilen,
    // rutan och texten följer den horisontella scrollen med kolumn 0.
    gfx::Rectf rowRect(const Ctx& c, int row) const;
    gfx::Rectf cellRect(const Ctx& c, int row, int column) const;
    gfx::Rectf expanderRect(const Ctx& c, int row) const;   // pilens ruta
    gfx::Rectf checkRect(const Ctx& c, int row) const;      // efter pilen
    gfx::Rectf checkZone(const Ctx& c, int row) const;      // rutan + checkGap
    float      contentX(const Ctx& c, int row) const;       // ikon och text
    int        rowAt(const Ctx& c, float my) const;          // fysisk y; −1
    // Raden i HELA innehållsytan (fysiska px) — −1 i remsan och utanför i x
    // och y. `pathAt` och högertrycket delar den.
    int        hitRow(const Ctx& c, float mx, float my) const;
    // Träffar ett tryck (fysiska px) PILEN på raden? Bara en nod med barn
    // har en — trycket, dubbelklicket och dragstarten frågar samma sak.
    bool       onExpander(const Ctx& c, int row, float mx, float my) const;
    // Träffar det RUTANS ZON? Bara när kryssen syns — samma tre frågare.
    bool       onCheckZone(const Ctx& c, int row, float mx, float my) const;

    // ANVÄNDARVÄGARNA — de ENDA som fyrar event. Var och en svarar false när
    // en handler rev eller lossade trädet (`firePair`/`fireEvent`), och då
    // rör anroparen aldrig `this` igen. `selectFromUser` svarar dessutom
    // false när inget fyrades (valet var redan det) — `mouseUp`s svar.
    bool selectFromUser(int row, std::uint32_t mods, bool fromKey);
    bool toggleFromUser(int row);        // onExpand/onCollapse (+ onChange
                                         // när ihopfällningen gömde valda)
    bool toggleCheckFromUser(int row);   // onCheck + onCommit
    void activateFromUser();             // onActivate för den aktuella noden

    void setHoverRow(int row);

    // ---- DRAG-AND-DROP
    // Var ett släpp hamnar — en LUCKA i förälderns barnlista, hållen som ID:N
    // och inte som stig:
    // föräldern (`parentId`, 0 = roten) och grannarna på var sida (`prevId`
    // före luckan, `nextId` efter den; 0 = listans början respektive slut).
    // `onDrop` körs FÖRE flytten och får ändra trädet, och då kan samma stig
    // eller samma index peka på en annan nod eller en annan lucka; id:na
    // löses i stället på nytt (`resolveDrop`). `index` är luckans plats när
    // den räknades och −1 = INGEN plats (föräldern säger nej till "in i",
    // regeln per nod). Resten är markeringen: `row` raden den ritas vid, `into` = IN
    // I raden (en ram runt den), annars en LINJE i överkanten av `row`
    // (`before`) eller underkanten (`!before`), indragen till `depth`.
    struct DropSpot {
        std::uint64_t parentId = 0;
        std::uint64_t prevId   = 0;
        std::uint64_t nextId   = 0;
        int           index    = -1;
        int           row      = -1;
        int           depth    = 0;
        bool          into     = false;
        bool          before   = false;
        bool operator==(const DropSpot&) const = default;
    };
    DropSpot dropSpotAt(const Ctx& c, float localY) const;
    // Luckan LÖST NU: förälderns stig och platsen där grannarna står i dag.
    // false när ingen plats finns — eller när luckan inte finns längre.
    bool     resolveDrop(TreePath& parent, int& index) const;
    void     drawDropIndicator(const Ctx& c, gfx::Color col) const;
    // Är lasten VÅRA EGNA noder (källan är trädet, lasten bär nodformatet och
    // trädets egen `dragStart` fyllde den)? Bara då tar det inbyggda emot —
    // ett släpp från en annan källa är appens att avgöra.
    bool     ownPayload(const DragArgs& e) const;
    // Lever ALLA dragna noder fortfarande? En `onDrop` som tog bort en av dem
    // (eller fyllde om trädet) gör släppet omöjligt.
    bool     draggedAlive() const;
    // Tar FÖRÄLDERN `parent` emot ett släpp som barn? Roten alltid.
    bool     acceptsDropInto(const TreePath& parent) const;
    // Ligger `parent` INUTI (eller är) någon av de dragna noderna?
    bool     insideDragged(const TreePath& parent) const;
    // FLYTTEN AV FLERA — den publika `moveNodes` och släppet går båda
    // hit: alla källor och målet valideras FÖRE någon mutation, indexet
    // räknas i FÖRE-flytten-koordinater (`moveNode`s konvention) och minskas
    // för varje källa med samma förälder och lägre index, en källa inuti en
    // annan följer med sin förfader, målet hålls fast vid sitt ID över
    // uttaget, och noderna sätts in i TRÄDORDNING.
    bool     moveIds(const std::vector<std::uint64_t>& ids,
                     const TreePath& toParent, int index);

    std::vector<Node>          roots_;
    std::uint64_t              nextId_        = 1;
    std::uint64_t              currentId_     = 0;   // 0 = ingen aktuell nod
    std::uint64_t              anchorId_      = 0;   // spannets fasta ände
    std::uint64_t              pendingSoloId_ = 0;   // solo vid släppet
    std::uint64_t              contextId_     = 0;   // senaste högertrycket
    std::vector<std::uint64_t> dragIds_;             // noderna ett drag bär
    bool                       multiSelect_   = false;
    bool                       showChecks_    = false;
    TreePath                   changedPath_;
    DropSpot                   drop_;
    int                        hover_         = -1;
    mutable std::vector<Row>   rows_;
    mutable bool               rowsDirty_     = true;
};

} // namespace glaze
