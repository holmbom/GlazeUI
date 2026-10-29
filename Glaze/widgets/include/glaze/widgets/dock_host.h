#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "glaze/timer.h"
#include "glaze/widget.h"

// DockHost — DOCKNINGSYTAN (wxAuiManager, DockPanel Suites DockPanel, VS:s verktygsfönster). Värden för
// de dockningsbara panelerna: den ordnar dem i REMSOR längs sina kanter, i
// FLIKGRUPPER och STAPLAR i remsorna, fäller in dem till HANDTAG på kanten,
// låter användaren dra en panels rubrik eller flik till en kant eller en
// annan panel, och sparar hela läget som en sträng. MÖNSTER: widgeten ritar
// SIG SJÄLV — draw()/contentSize() i src/widgets/dock_host.cpp med
// switch (c.ui); modellen och geometrin bor i src/dock/ (intern), skiktets
// form är DELAD med delaren (src/sash.cpp).
//
// MODELLEN är KANT → REMSA → CELL → FLIK (src/dock/dock_model.h): en remsa
// har en kant och celler LÄNGS kanten, en cell är en flikgrupp med en aktiv
// panel och en vikt. Remsorna ligger i skärordning, yttersta först.
//
// BARNEN: varje `DockPanel` med `dock` Left/Top/Right/Bottom hör till
// modellen. En panel som ADOPTERAS blir innersta remsa vid sin kant — så
// `.glazeui`s trädbygge är startlayoutens ordning. ALLT ANNAT — en Panel, en
// editor, en panel med `Dock::None`/`Fill` — är HUVUDYTAN: det som blir kvar
// i mitten när handtagen och remsorna skurits, och den räknas om varje gång
// layouten räknas (in- och utfällning, drag, mått). Värden PLACERAR själv
// (`placesChildren`, Bar-prejudikatet), och skälet är ORDNINGEN: barnordningen
// är z-ordningen, som ingen API får flytta om.
//
// FÄLTEN ÄR SANNINGEN för kant och tjocklek, STRUKTUREN för gruppering och
// ordning. `panel.dock` och `width`/`height` läses som vanligt, och värden
// skriver tillbaka dem när ANVÄNDAREN ändrar något. Ett rått `dock` som
// ändras efter `adopt` gäller vid nästa ritning eller operation: panelen
// lämnar sin cell och blir innersta remsa vid den nya kanten (utanför en
// handler krävs `invalidate()`, layoutfältens kontrakt i widget.h).
//
// REMSANS TJOCKLEK är det största föredragna måttet bland dess öppna,
// utfällda paneler (AUTO = `dockDefaultSize`), klampad mot deras min/max
// (`DockPanel::setMinimumSize`): min = det största av minimimåtten, max = det
// minsta av maximimåtten, och min vinner när de krockar. Gränserna är MJUKA
// mot utrymmet — huvudytan krymper till 0 först, därefter klipps remsorna.
// CELLERNAS LÄNGD fördelas efter vikt och klampas likadant längs remsan.
//
// FLIKARNA: en cell med minst två öppna paneler får en FLIKRAD nederst (VS:s
// verktygsfönster), med lika breda flikar och ellips. Ett tryck på en flik
// väljer den; inaktiva flikar får en tom yta (de DÖLJS inte — `hide()` är
// stängd), och Tab går inte in i dem.
//
// HANDTAGEN: en infälld panel (`setCollapsed(true)`) och en ONÅLAD panel
// (knappnålen, `setPinned(false)`) blir ett handtag i en egen remsa vid sin
// kant, UTANFÖR alla panelremsor; flera på samma kant står bredvid varandra.
// Vid Top/Bottom bär handtaget titeln, vid Left/Right är det en kvadrat med
// ikonen eller titelns första tecken, och titeln är dess tooltip
// (`Widget::toolTipAt`). Ett klick på handtaget fäller ut en NÅLAD panel
// DOCKAD — den trycker undan huvudytan — och en ONÅLAD som OVERLAY.
//
// OVERLAYN (Visual Studios Auto Hide): den onålade panelen glider fram
// OVANPÅ huvudytan och remsorna vid sin kant, innanför handtagen, och trycker
// inte undan något. Den ritas sist och träffas först (`Widget::frontChild`)
// utan att barnordningen rörs, med en hårlinje i `frame` mot huvudytan.
// Tjockleken är panelens `width`/`height` (AUTO = `dockDefaultSize`) klampad
// mot dess min/max och mot ytan. Högst EN overlay är öppen: den som öppnas
// senast vinner, och den förra fälls in. Dess handtag står kvar, intryckt,
// och ett klick på det fäller in overlayn igen. Värden FÄLLER IN den när
// användaren LÄMNAT den — när BÅDE pekaren (över overlayn eller dess handtag),
// tangentfokus, ett tryck och popuperna (en lista, rubrikmenyn) alla är
// utanför panelen. Prövningen görs med en upprepad timer var
// `Style::dockHideDelayMs` medan en overlay är öppen och värden SYNS (genom
// förälderkedjan, i ett öppet fönster som inte är dolt — den stannar när
// något av dem döljs och startar igen när det visas; 0 = timerns minsta
// intervall, `Timer::kMinIntervalMs`); Escape inifrån
// fäller in den direkt och lämnar fokus på handtaget. Overlayn är ett
// DRIFTTILLSTÅND: `layout()` skriver en onålad panel som infälld, och att
// öppna eller fälla in den fyrar inget `onChange`. `activate()` på en onålad
// panel öppnar overlayn och lämnar fokus i den — annars hade lämna-prövningen
// fällt in den direkt. Knappnålen i overlayn fäster den: den blir dockad där
// den stod i strukturen. Öppna med hover finns inte (en skriven gräns).
//
// GLIDNINGEN: en panel glider ut och in — overlayn, och en remsa som
// kommer eller går med sin enda dockade panel (knappnålen, ett handtagsklick)
// — och huvudytan följer med bild för bild. Bara under förloppet: värdens
// `draw()` postar ETT steg per bild till pumpens svans (ritytans
// tick-mönster), och steget invaliderar värden tills glidningen är framme,
// därefter ber den inte om fler bilder (regel 1; ingen `setAnimating`).
// Längden är `Style::dockAnimMs` (0 = ingen glidning), och ett omslag mitt i
// förloppet glider från där det står. En programmatisk ändring glider bara
// när fönstret är öppet och värden redan har ritats; `setLayout` glider inte.
// En remsa vars andra celler står kvar, och en flik i en grupp, byts utan
// glidning.
//
// SKIKTEN — ett mellan en remsa och resten, ett mellan två celler — är
// greppbara (↔/↕) med delarens mekanik och FÖRHANDSVISNING: måttet skrivs
// först vid släppet, och ett avbrutet drag går tillbaka. Ett remsskikt
// skriver tjockleken till ALLA paneler i remsan, så de är överens; ett
// cellskikt ändrar vikterna för ALLA synliga celler i remsan, och dess
// förhandsvisning är layouten ur just de vikterna — det man ser är det
// släppet ger. Golvet är panelernas minsta mått, och `dockHeaderH` när inget
// är satt; ryms inte båda cellernas golv står skiktet still. SNAPPEN:
// inom `dockSnapZone` dras måttet till golvet, taket och det förvalda måttet
// — `dockDefaultSize` för en remsa, den jämna delningen mellan de två för ett
// cellskikt. Dras det under golvet minus zonen visar förhandsvisningen
// panelerna INFÄLLDA (remsans alla, eller cellens flikar), och släppet fäller
// in dem; måttet före draget står kvar, så de fälls ut i sin gamla storlek.
// Ett skiktdrag avbryter remsans glidning.
//
// RUBRIK- OCH FLIKDRAGET (VS-modellen): dra en panels
// rubrik — eller en flik — och panelen LOSSNAR vid `dragThreshold` till sitt
// flytande fönster, som följer pekaren (se LEVANDE DRAG nedan). Värden visar
// VAR den hamnar med en PLATSHÅLLARE (`dropHint`-ytan med `accent`-ram,
// ovanpå innehållet). Platshållaren är SIMULERAD: värden tillämpar
// operationen på en kopia av modellen och ritar exakt den yta panelen skulle
// få. Över en annan panel: inom `dockDropZone` från en av cellens kanter
// (som mest andelen `dockDropShare` av sidan) blir panelen en ny cell bredvid
// (längs remsan) eller en ny remsa (tvärs), annars en FLIK i cellen. Inte
// över någon panel: närmaste kant på värden inom `dockDropZone` lägger den
// YTTERST där, över hela kantens längd. Mitt i huvudytan eller utanför
// värden: ingen platshållare — släppt där förblir panelen flytande där den
// står. Skrivna gränser: ingen omordning av flikar med musen (en flik som
// släpps på en cell läggs sist), ingen dockning in i huvudytan, ingen mellan
// två värdar, ingen VS-kompass, och det burna fönstret är opakt.
//
// PROGRAMMATISKT: `dockPanel(p, kant)` lägger panelen ytterst vid kanten,
// `dockPanel(p, mål, var)` på en annan panel — `Dock::Fill` som flik, en kant
// bredvid. Båda aktiverar den dockade panelen; ett `activate()` efteråt
// bestämmer vilken flik som är vald. I `.glazeui` är de anropssatser med
// IDENTIFIERARE (`dock1.dockPanel(sok, utforskare, glaze::Dock::Fill);`), och
// de gäller paneler som REDAN är barn — raden ska stå EFTER trädbygget, som
// `setLayout` nedan.
//
// TANGENTBORDET: värden är ETT tabbstopp när den har handtag eller
// flikgrupper. Pilarna och Home/End flyttar ett val i en platt lista — alla
// flikar i flikraderna, därefter alla handtag — och Enter/Space aktiverar
// fliken eller fäller ut handtaget (fokus hamnar då på panelens första
// tabbstopp; den öppna overlayns handtag fäller in den, Escape likaså).
// Inifrån en panel byter Ctrl+PageUp/PageDown flik i cellen.
// FOKUS STANNAR ALDRIG i en dold flik eller en infälld panel — och det gäller
// också när appen gör ändringen (`show()`, `hide()`, `activate()`,
// `setCollapsed`, `setLayout`): fliken byts → den nya flikens första stopp,
// annars värden; panelen fälls in → dess handtag; panelen stängs → huvudytans
// första stopp, annars värden. Värden tar fokus bara när den är ett
// tabbstopp; annars töms fönstrets fokus (`AppWindow::clearFocus`), så att
// inget dolt fält behåller tangentbordet.
//
// LAYOUTEN SOM STRÄNG (`layout()`/`setLayout()`, wxAUI:s
// `SavePerspective`/`LoadPerspective`), version 2:
//
//     v2;s=left;c=1;p=utforskare,1,0,1,0,240,0;p=sok,0,0,1,0,240,0;…
//
// `s=kant` öppnar en remsa (skärordning), `c=vikt` en cell i den,
// `p=namn,aktiv,infälld,nålad,stängd,bredd,höjd` en flik,
// `f=namn,x,y,b,h,stängd,dock,bredd,höjd` en flytande panel (fönstrets
// plats och innehållsyta i logiska px, `-` för en plats som inte finns),
// `t=namn,kant,band,plats,dold` ett verktygsfält och
// `ft=namn,x,y,dold,dock,band` ett FLYTANDE verktygsfält (
// fönstrets plats och dockningsminnet) — grammatiken står i
// src/dock/dock_string.h. Panelen identifieras med sitt `name` (objektets
// namn, widget.h). Läsningen är ATOMISK i syntaxen: ett enda fel ger
// `gfx::log` och lämnar ALLT orört. Ett OKÄNT namn hoppas däremot över post
// för post (loggat, med en rad om hur många paneler som saknar `name`), och
// resten gäller; en cell eller remsa som blir tom tas bort. Paneler som
// strängen inte nämner behåller sina flaggor och läggs som innersta remsa
// vid sin kant i barnordning — som vid en färsk `adopt`. Ett objekt som
// flyter och inte nämns fortsätter att flyta, och en `p=` eller `t=` för ett
// som flyter dockar det. Den programmatiska vägen fyrar aldrig `onChange`.
// `v1` avvisas.
//
// I `.glazeui` är `setLayout` en vanlig setter (`dock1.setLayout("v2;…");`)
// och previewn UTFÖR den — men den gäller paneler som REDAN är barn, så
// raden ska stå EFTER trädbygget. GlazeBuilder lägger en ny rad sist, så
// det gäller av sig självt; en HANDSKRIVEN rad före `adopt` hade previewn
// ändå utfört (den bygger trädet före satserna), medan appen kör satserna
// i källordning och då loggar varje nämnd panel som okänd.
//
// `onChange` fyras efter varje ändring ANVÄNDAREN gjort av layouten (efter
// panelens eget `onChange`) — platsen att spara strängen på.
//
// VERKTYGSFÄLTEN (VS och Office lägger banden utanför panelerna):
// en `ToolBar` som är värdens DIREKTA barn och har en kant blir medlem i ett
// BAND vid den kanten. Banden skärs YTTERST, före handtagen och
// panelremsorna: Top, Bottom, Left, Right, och vid varje kant band 0 ytterst.
// Bandets tjocklek är det tjockaste synliga fältets; fälten packas i ordning
// med `toolGap`, var och ett i sin EGEN längd (tool_bar.h), och ett som
// inte ryms klipps (ingen överflödsmeny). Ett fält vid Left/Right STÅR
// (`Bar::isVertical`). VILKET band är fältets `band()` och vilken kant
// dess `dock` — fälten vann, som för en panel — och ordningen i bandet är
// adoptionsordningen; ett fält som adopteras under körning hamnar sist i sitt
// band, och ett band utanför antalet blir ett nytt innersta. Ett dolt fält
// skär inget.
//
// GREPPDRAGET: varje fält i en värd har ett GREPP i början (`toolGripW`).
// Draget LOSSAR fältet vid tröskeln (som panelerna; att flytta ett
// fält en plats i bandet gör det flytande under draget, och bandet lägger om
// sig när det lyfts). Platshållaren visas över ett annat band
// (mittpunktsregeln: före det första fält vars mitt ligger bortom pekaren),
// en annan plats i samma band, eller INNANFÖR bandområdet vid en kant (inom
// `dockDropZone`) — då blir det ett NYTT innersta band där; nya band skapas
// bara innerst. Platshållaren är SIMULERAD som panelernas, och släppet
// skriver tillbaka `dock` och det PACKADE `band` för fälten och fyrar värdens
// `onChange`; släppt någon annanstans flyter fältet. Ett fält som byter
// riktning byter också `width` och `height`, så tjockleken förblir
// tjocklek. Skriven gräns: ingen fri position i ett band (packade band).
//
// FLYTANDE VERKTYGSFÄLT: `toolBar.setFloating(true)` —
// eller ett dubbelklick på greppet — lyfter fältet ur sitt band till ett EGET
// fönster med samma maskineri som en flytande panel (nedan): en rubrik med
// fältets `text` och en stängknapp överst, fältet liggande under den, och
// FAST storlek som följer fältets innehåll. Fönstret aktiveras ALDRIG (ett
// klick på "Fet" lämnar tangentbordet i textfältet, som i VS). Ett drag i
// rubriken flyttar fönstret med pekaren och dockar fältet där platshållaren
// står vid släppet; ett dubbelklick på rubriken dockar det sist i band
// `band()` vid `dock`. Rubrikens ✕ och fönstrets egen stängning döljer
// fältet och fyrar värdens `onChange`.
//
// AUTO-STORLEK: ingen, Panel-beslutet — värden är en placeringsyta och
// dimensioneras av layouten (oftast `Dock::Fill` i fönstret).
//
// FLYTANDE PANELER: `panel.setFloating(true)` — eller
// rubrikmenyns "Flytande", ett dubbelklick på rubriken, ett rubrik- eller
// flikdrag som släpps utan platshållare — lyfter panelen ur layouten till ett
// EGET fönster,
// som värden äger och visar ÄGT av sitt eget fönster: Z-ordningen följer
// ägaren, fönstret stängs och döljs med den, och det har ingen knapp i
// aktivitetsfältet. Panelens rubrik är fönstrets titelrad, och en tangent
// ingen i fönstret tog går till ÄGARENS acceleratorer (menyraden,
// programmenyn). TILLSTÅNDET gäller direkt (`isFloating()`, `layout()`) och
// FLYTTEN mellan fönstren görs i pumpens svans. En flytt som en öppen popup i
// panelen nekar VÄNTAR och görs vid nästa ritning av värden eller av det
// flytande fönstret — rubrikmenyns egen stängning är det vanliga hindret, och
// den ritar om panelen. Inget fönster skapas när värdens fönster är stängt
// eller offscreen (glaze-render, previewn): där lämnar panelen bara
// layouten. Ett DOLT värdfönster skapar sina flytande fönster dolda, och ett
// som stängts och visas igen öppnar dem där de stod.
//
// LEVANDE DRAG: när ett
// rubrik-, flik- eller greppdrag passerar tröskeln LOSSNAR objektet — panelen
// eller fältet — till sitt flytande fönster, som skapas på bärandets plats i
// svansen och följer pekaren; greppet står kvar där handen tog (i en flik:
// mitt på rubriken). Släppt över en platshållare dockas objektet där;
// släppt någon annanstans — utanför fönstret, eller inne i det utan
// platshållare — förblir det flytande där det står. Ett flytande objekts eget
// drag (panelens rubrik, fältets rubrik) går likadant mot värden. Draget ÄGS
// av värden, som håller trycket med explicit capture medan objektet byter
// fönster. `onChange` fyras EN gång per drag, vid släppet (panelens och
// värdens, eller bara värdens för ett fält). Ett AVBRUTET drag —
// fokusförlusten, en capture som OS:et tog, ett objekt som dör — lämnar
// objektet flytande där det står, och `onChange` postas. Escape avbryter
// inte (en skriven gräns: tangenterna går till fokus). Nya panelfönster
// stjäl inte fokus vid sin första visning, och ett fälts fönster aldrig.
// PLATSHÅLLARVÄGEN gäller där draget inte kan bäras: en panel eller ett fält
// som inte får flyta (`setFloatable(false)`), en offscreen-värd, och där
// fönster saknar skärmposition — `isFloatDragSupported()` svarar false
// (Wayland, headless): platshållaren visas medan objektet står kvar,
// och ett släpp utanför gör ingenting. Där finns ingen avdockning med musen —
// menyn och dubbelklicket fungerar, kompositören placerar fönstret,
// namnlisten syns som flytthandtag, och en sparad position tillämpas inte
// (loggat en gång; `layout()` skriver `-`).
//
// Stänger användaren det flytande fönstret (Alt+F4, namnlisten på Wayland)
// stängs PANELEN, som med dess stängknapp — en panel som inte får stängas
// står kvar. Stängs ÄGAREN, eller något fönster längre upp i ägarkedjan,
// följer det flytande fönstret med utan att panelens tillstånd rörs: en
// `layout()` i huvudfönstrets `onClose` säger fortfarande "flytande, öppen".
//
// Skrivna gränser: `appWindow()` på en flytande panel (eller ett flytande
// verktygsfält) är dess eget fönster, och `AppWindow::find<>()` på värdens
// fönster hittar den inte — appkod når objektet genom medlemmen.
// Drag-och-släpp mellan ett flytande fönster och huvudfönstret finns inte
// (DnD gäller inom ett fönster), och en värd som lossas ur sitt fönster medan
// objekt flyter lämnar deras fönster ägda av det gamla tills värden rivs.

namespace glaze {

class DockPanel;
class ToolBar;
class DockFloatWindow;
class ToolFloatHeader;

namespace detail {
class DockModel;
struct DockGeometry;
struct DockLive;
struct DockFloat;
} // namespace detail

class DockHost : public Widget {
public:
    DockHost();
    ~DockHost() override;

    // Layouten som sträng — se headern. `setLayout` svarar false när
    // strängen avvisades (loggad, ingenting ändrat).
    std::string layout() const;
    bool        setLayout(const std::string& s);

    // DOCKA PROGRAMMATISKT — panelerna måste vara barn. Ett fel (inget barn,
    // samma panel två gånger, `Dock::None`, ett mål som inte är dockat)
    // loggas och ändrar ingenting. Inget `onChange`.
    void dockPanel(DockPanel& p, Dock edge);
    void dockPanel(DockPanel& p, DockPanel& target, Dock where);

    // LEVANDE DRAG finns: false där fönster
    // saknar skärmposition (Wayland, headless) — där görs ingen avdockning med
    // musen, och draget visar bara platshållaren (platshållarvägen).
    static bool isFloatDragSupported();

    // Användaren ändrade layouten.
    Handler<ChangeArgs> onChange;

    // setProp-egenskaper: layout (strängen ovan).
    bool setProp(const std::string& key, const std::string& value) override;

    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;
    bool       clipsChildren() const override { return true; }

    // Overlayns hårlinje och släppindikatorn ritas OVANPÅ panelerna och
    // innehållet — utanför barnklippet, i värdens eget Ctx.
    void drawAfterChildren(const Ctx& c) override;

    // Modellsynken, en väntande flytt och glidningens steg — också när värden
    // står i en inaktiv flik och inte ritas (widget.h).
    void frameStep() override;

    // Den öppna OVERLAYN — eller en som glider in — ritas sist och träffas
    // först, utan att barnordningen rörs (widget.h).
    Widget* frontChild() const override;

    // ETT tabbstopp när värden har handtag eller flikgrupper.
    bool focusable() const override;

    // Handtagens och flikarnas tooltip är panelens titel.
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    void mouseDown(const Ctx& c, float x, float y) override;
    void mouseDrag(const Ctx& c, float x, float y) override;
    bool mouseUp(const Ctx& c, float x, float y) override;
    void mouseMove(const Ctx& c, float x, float y) override;
    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;

    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    bool descendantKeyDown(gfx::Key k, std::uint32_t mods) override;

    // Modellen följer barnlistan och barnens synlighet (widget.h).
    void childAdded(Widget& child) override;
    void childRemoved(Widget& child) override;
    void childVisibilityChanged(Widget& child) override;
    // Värden eller fönstret visades eller doldes — lämna-prövningen följer med.
    void shownChanged() override;
    // Värdens TRYCK ELLER CAPTURE AVBRÖTS (fokusförlusten, OS:et, ett annat
    // fönster, ett nytt tryck — `Widget::mouseCancel`): ett buret drag som
    // värden höll avslutas nu som avbrutet, och värdens egna tryck — skikten,
    // fliken, handtaget — släpps med förhandsvisningen. `onChange` postas;
    // svaret är false.
    bool mouseCancel(gfx::MouseButton b) override;

private:
    // ---- delarna under pekaren
    enum class Part { None, StripSash, CellSash, Tab, Handle };
    struct Hit {
        Part       part  = Part::None;
        DockPanel* panel = nullptr;   // remsans/cellens/flikens/handtagets
        DockPanel* other = nullptr;   // cellskiktet: den visade efter skiktet
        bool operator==(const Hit& o) const
        {
            return part == o.part && panel == o.panel && other == o.other;
        }
    };
    // ---- släppmålet för ett rubrik- eller flikdrag: en kant på värden
    // eller en annan panel. Ingen reserv "hem" — ett objekt som släpps utan
    // mål förblir där det är.
    struct Drop {
        enum class Kind { None, Edge, Panel };
        Kind       kind   = Kind::None;
        Dock       where  = Dock::None;   // kanten (Edge) eller zonen (Panel)
        DockPanel* target = nullptr;      // Panel
        bool operator==(const Drop& o) const
        {
            return kind == o.kind && where == o.where && target == o.target;
        }
    };
    // ---- tangentbordets lista
    struct KbdItem {
        bool       handle = false;   // annars en flik
        DockPanel* panel  = nullptr;
    };

    // MODELLEN — fältens ändringar in i strukturen: saknade dockade barn
    // läggs in, en panel vars `dock` bytts flyttas. Först i `draw()` och i
    // varje operation. Svarar om något ändrades.
    bool syncModel();
    // Samma synk på en KOPIA — för const-vägarna (geometrin, `layout()`), så
    // de svarar mot fälten också innan nästa ritning har synkat modellen.
    detail::DockModel synced() const;
    // MODELLENS INVARIANT: den håller en panel bara medan den är mitt
    // barn eller på väg HEM ur ett av mina flytande fönster (levande). En
    // annan — en panel på väg hem som DOG under väntan, en som någon tog ur
    // sitt flytande fönster — tas bort med PEKAREN och derefereras aldrig.
    // Först i varje synk, FÖRE `normalize` (som läser panelerna). Svarar om
    // något togs bort.
    bool pruneModel(detail::DockModel& m) const;
    // `dock` ur remsans kant för varje panel i modellen — efter en verklig
    // ändring (värden skriver tillbaka fälten).
    void writeBackEdges();

    // Dragens förhandsvisningar med index i modellen `m` (den som ritas).
    detail::DockLive     live(const detail::DockModel& m) const;
    detail::DockGeometry geometry(const Ctx& c, const gfx::Rectf& b) const;
    // Delen under en FYSISK punkt (värdens behavior-Ctx), dess yta i
    // geometrin (logiska px) och omritningen av just den ytan.
    Hit        hitAt(const Ctx& c, float x, float y) const;
    gfx::Rectf hitRect(const detail::DockGeometry& g, const Hit& h) const;
    void       invalidateHit(const Ctx& c, const Hit& h);
    // Är `p` fortfarande mitt barn? Pekaren JÄMFÖRS, derefereras inte.
    bool owns(const DockPanel* p) const;

    // ---- DockPanel frågar och berättar
    bool showsPanel(const DockPanel& p) const;   // panelens showsChildren
    void panelChanged(DockPanel& p);             // ett mått, ikonen, titeln
    // FLAGGORNA ändrades (`collapsed`/`pinned`, `was*` = läget före): overlayn
    // öppnas eller fälls in, remsan glider och fokusvakten frågas.
    void panelStateChanged(DockPanel& p, bool wasCollapsed, bool wasPinned);
    void activatePanel(DockPanel& p);            // DockPanel::activate

    // ---- overlayn
    // Den ÖPPNA overlayn — en öppen, onålad, utfälld panel i modellen —
    // eller nullptr. Högst en.
    DockPanel* overlayPanel() const;
    // EN OVERLAY ÅT GÅNGEN: `recent` vinner när den är en kandidat (den
    // öppnades senast — ett handtagsklick, en sättare, en adoption), annars
    // den öppna, annars den första i modellens ordning; de andra fälls in
    // utan glidning. Startar eller stoppar lämna-prövningen. Svarar om en
    // annan panel TRÄNGDES UNDAN — då ska fokusvakten frågas,
    // för den kan ha haft fokus.
    bool syncOverlay(DockPanel* recent);
    // Prövas en öppen overlay — finns den, och SYNS värden: genom hela
    // förälderkedjan, i ett öppet fönster som inte är dolt ?
    bool watching() const;
    // Lämna-prövningens timer går bara medan `watching()` gäller — synkad
    // när overlayn öppnas och stängs, när värden eller fönstret visas och
    // döljs (`shownChanged`) och i `draw()`.
    void syncHideTimer();
    // Fäller in overlayn när pekaren, fokus, ett tryck och popuperna alla
    // är utanför den. Timerns handler.
    void hideCheck();
    bool pointerOverOverlay(DockPanel& o) const;
    // Overlayn fälls in — Escape, ett klick på dess handtag. Inget
    // `onChange` (drifttillstånd).
    void collapseOverlay();

    // ---- glidningen
    // EN glidning per panel. Andelen är 0–1 av den fulla tjockleken; `now`
    // räknas i `draw()` och läses av allt annat, så ritningen, placeringen och
    // träffprovet i samma bild ser samma tal.
    struct Anim {
        DockPanel*    panel   = nullptr;
        bool          overlay = false;   // overlayn, annars remsan
        float         from    = 0.0f;    // andelen vid start
        float         to      = 1.0f;    // 1 = ut, 0 = in
        std::uint64_t start   = 0;       // gfx::ticksMs()
        float         ms      = 0.0f;    // längden — resten av vägen
        float         now     = 0.0f;    // andelen vid senaste steget
    };
    bool canAnimate() const;
    void startAnim(DockPanel& p, bool overlay, bool out);
    void dropAnim(const DockPanel& p);
    // EN gång per bild, FÖRE nästa bilds skadeinsamling:
    // `draw()` postar steget till pump-svansen (ritytans tick-mönster), och
    // steget ritar om hela värden — också övergången till slutläget.
    void stepAnims();
    // Har remsan där `p` står ingen ANNAN dockad, utfälld panel? Då kommer
    // och går remsan med `p` — och glider.
    bool stripAloneWith(const DockPanel& p) const;
    // Användarens infällning ur snappen — utan glidning: förhandsvisningen
    // visade redan resultatet.
    bool collapseByUser(const std::vector<DockPanel*>& panels);

    // ---- flytande paneler och verktygsfält
    // Är `w` mitt — ett barn, eller ett objekt i ett av mina flytande
    // fönster? Pekaren JÄMFÖRS, derefereras inte.
    bool               mine(const Widget* w) const;
    detail::DockFloat* floatOf(const Widget* w) const;
    // Pekarna till en panel som lämnar layouten glöms (dragen, overlayn,
    // glidningen, hovern) — vid borttagningen och när den börjar flyta.
    void forgetPanel(const DockPanel* p);
    // …och till ett verktygsfält (greppdraget, rubrikdraget och målet).
    void forgetBar(const ToolBar* t);
    // DockPanel berättar: `floating` växlade. TILLSTÅNDET först — panelen
    // lämnar sin plats i modellen eller får en innersta vid sin `dock` — och
    // flytten mellan träden postas.
    void panelFloatingChanged(DockPanel& p);
    // ToolBar berättar samma sak: fältet lämnar sitt band, eller står
    // sist i band `band()` vid `dock` — och flytten postas.
    void barFloatingChanged(ToolBar& t);
    // Fältets egna mått eller titel ändrades (barnen och deras mått,
    // `setText`, ett språkbyte): banden räknas om, och ett flytande fälts
    // fönster passas i svansen — också ett dolt, som aldrig ritas.
    void barChanged(ToolBar& t);
    // `floatingBounds`/`floatingPosition` sattes — ett öppet fönster flyttar
    // dit.
    void floatBoundsChanged(Widget& w);
    // Det flytande fönstrets egen stängning (Alt+F4): panelen stängs, som med
    // dess stängknapp.
    void closeFloatingPanel(DockPanel& p);
    // Det flytande FÄLTETS ✕ och fönstrets egen stängning: fältet döljs
    // och värdens `onChange` fyras. Svarar om en handler kördes.
    bool closeFloatingBar(ToolBar& t);
    // Greppets och rubrikens dubbelklick — de enda vägarna som gör ett
    // fält flytande eller dockat OCH fyrar `onChange`. Svarar om en handler
    // kördes.
    bool userFloatBar(ToolBar& t, bool on);
    // Ett objekt i ett av mina flytande fönster DÖR (`~DockPanel`,
    // `~ToolBar`): fönstret släpper det och glömmer varje widgetpekare NU,
    // modellen tappar det, och posten rivs i svansen.
    void floatingItemDestroyed(Widget& w);
    // Står träden eller fönstren efter tillståndet — eller ett fälts fasta
    // storlek efter dess mått? Och synken som rättar dem — postad till
    // pump-svansen, en åt gången, bara med ett öppet fönster.
    bool floatsNeedSync() const;
    void postFloatSync();
    void syncFloats();
    // Ett HINDER för flytten — en öppen popup eller meny i objektet. Frågas
    // FÖRE `transferChild`, så en väntande flytt inte loggar vid varje försök.
    bool floatBlocked(const Widget& w) const;

    // ---- rubrik- och flikdraget
    // Ett NYTT tryck i en rubrik eller ett grepp (panelen och fältet
    // anropar): ett AVBRUTET drag — fokusförlust, inget släpp — lämnade sitt
    // mål kvar, och ett nytt drag mot samma mål hade då inte ritat om.
    void dragBegin();
    // VÄRDENS EGNA TRYCK (skikten, fliken, handtaget och platshållarvägens
    // panelmål) nollas — ett nytt tryck och ett avbrott. Svarar om något av
    // dem levde, alltså om en förhandsvisning ska bort.
    bool clearPress();
    Drop       dropAt(const Ctx& hc, const DockPanel& dragged, float x,
                      float y) const;
    gfx::Rectf simulated(const Ctx& c, const gfx::Rectf& b,
                         DockPanel& dragged, const Drop& d) const;
    // Målet under en punkt i VÄRDENS fysiska px, PRÖVAT: ett mål vars
    // simulering inte ändrar något är inget mål (`toolTargetAt`s form).
    Drop       panelTargetAt(const Ctx& hc, DockPanel& p, float x,
                             float y) const;
    bool       apply(DockPanel& p, const Drop& d);
    // Rubrikdraget (DockPanel anropar) och flikdraget (värden själv). Fysiska
    // px i KÄLLANS fönster: värdens för en dockad panel, det flytande för en
    // flytande. Där draget kan BÄRAS (`canCarry`) lossnar panelen vid
    // tröskeln; annars platshållarvägen: platshållaren i värden medan
    // panelen står kvar, och målet räknas om vid släppet.
    void       panelDragMove(DockPanel& p, float x, float y);
    void       panelDragTo(const Ctx& hc, DockPanel& p, float x, float y);
    bool       panelDragEnd(DockPanel& p, float x, float y);
    bool       liveDrag() const;

    // ---- skiktdragen
    bool liveStripSash() const;
    bool liveCellSash() const;

    // ---- användarens handlingar — de enda vägarna som fyrar `onChange`
    bool userActivate(DockPanel& p, bool focusInside);
    bool userExpand(DockPanel& p, bool focusInside);
    // Panelernas `onChange` (i ordning) och därefter värdens. Svarar om någon
    // handler kördes.
    bool userChanged(const std::vector<DockPanel*>& panels);

    // ---- verktygsfälten
    // Släppmålet för ett greppdrag: ett befintligt band — identifierat av ett
    // ANNAT fält i det, och platsen som fältet det ska stå före (nullptr =
    // sist) — eller ett nytt innersta band vid en kant. Fält och inte index:
    // ett band som bara rymmer det dragna fältet försvinner när det lyfts ur,
    // och då hade indexen flyttat.
    struct ToolDrop {
        enum class Kind { None, Band, NewBand };
        Kind           kind   = Kind::None;
        Dock           edge   = Dock::None;
        const ToolBar* bandOf = nullptr;   // Band: ett annat fält i bandet
        const ToolBar* before = nullptr;   // Band: före det; nullptr = sist
        bool operator==(const ToolDrop& o) const
        {
            return kind == o.kind && edge == o.edge && bandOf == o.bandOf &&
                   before == o.before;
        }
    };
    // Layouten ur en modell — verktygsfälten mäts i den riktning deras band
    // har (`ToolBar::sizeFor`). ALLA geometrifrågor går hit.
    detail::DockGeometry layoutIn(const detail::DockModel& m, const Ctx& c,
                                  const gfx::Rectf& b,
                                  const detail::DockLive& l) const;
    // `dock` och det PACKADE `band` för varje fält i modellen — efter en
    // verklig ändring. Ett fält som byter riktning byter
    // `width` och `height`.
    void writeBackBars();
    // Målet under en FYSISK punkt (värdens behavior-Ctx), och fältets yta
    // när målet tillämpats på en kopia (tom = ingen ändring). `toolTargetAt`
    // är målet PRÖVAT — ett vars simulering inte ändrar något är inget mål.
    ToolDrop   toolDropAt(const Ctx& hc, const ToolBar& dragged, float x,
                          float y) const;
    gfx::Rectf toolSimulated(const Ctx& c, const gfx::Rectf& b,
                             ToolBar& dragged, const ToolDrop& d) const;
    ToolDrop   toolTargetAt(const Ctx& hc, ToolBar& t, float x, float y) const;
    // Släppet på ett mål — också för ett FLYTANDE fält, som dockas och vars
    // flytt hem postas.
    bool       applyTool(ToolBar& t, const ToolDrop& d);
    // Greppdraget (ToolBar anropar) — fysiska px. Bärs det (`canCarry`)
    // lossnar fältet vid tröskeln, annars platshållarvägen.
    void toolDragMove(ToolBar& t, float x, float y);
    bool toolDragEnd(ToolBar& t, float x, float y);
    // Det FLYTANDE fältets rubrikdrag (`ToolFloatHeader` anropar) —
    // fysiska px i fältets eget fönster: fönstret följer pekaren (fall B).
    // Där draget inte kan bäras gör det ingenting.
    void toolHeaderDrag(ToolBar& t, float x, float y);
    bool toolHeaderDragEnd(ToolBar& t, float x, float y);
    // Platshållarvägens greppdrag lever (fältets tryck).
    bool liveToolDrag() const;
    // Är fältet fortfarande mitt barn? Pekaren JÄMFÖRS, derefereras inte.
    bool ownsBar(const ToolBar* t) const;

    // ---- LEVANDE DRAG (VS-modellen) — ett objekt som
    // BÄRS: det har lossnat till sitt flytande fönster, och fönstret följer
    // pekaren. Draget ÄGS av värden. FALL A — ett dockat objekt lossnar:
    // värden tar EXPLICIT capture i sitt fönster (`setPointerCapture`), så
    // rörelserna och släppet når den också när objektet flyttats till sitt
    // fönster i svansen. FALL B — ett flytande objekts eget drag: trycket
    // ligger i dess fönster, som flyttas. Ett verktygsfälts fönster aktiveras
    // aldrig, så där tar värden över trycket EN gång vid lossningen
    // (`takePointerCapture`) och draget går som A; svarar den false (ingen
    // förgrund, en annan app)
    // fortsätter draget i fältets eget fönster. I fall B håller OBJEKTET
    // (panelen, fältets rubrik) explicit capture i sitt fönster, så att
    // fönstrets avbrott når värden (`itemPressLost`) också när inget ritas.
    // KÄLLAN — fönstret vars händelse bär
    // punkten — följer med varje anrop och lagras aldrig.
    struct Carry {
        DockPanel*          panel = nullptr;   // ett av de två
        ToolBar*            bar   = nullptr;
        std::weak_ptr<char> life;              // objektets livstecken
        std::uint64_t       serial    = 0;     // dragets identitet; 0 = inget
        bool                hostPress = false; // värden håller trycket
        // ÖVERTAGANDET PÅGÅR: `takePointerCapture` släpper
        // källfönstrets tryck, och fältets rubrik får då `mouseCancel` — ett
        // avbrott som ÄR övertagandet och inte avslutar draget.
        bool        takingOver = false;
        // SLÄPPET PÅGÅR: objektets tryck
        // är redan släckt, och en ritning mitt i släppet (en flytt till en
        // skärm med annan DPI ritar synkront) får inte avsluta draget.
        bool        releasing = false;
        gfx::Pointf grab{};   // greppet, placeringsenheter från klienthörnet
        gfx::Pointf at{};     // fönstrets klienthörn, placeringsrymden
        Drop        drop{};       // panelens mål i värden
        ToolDrop    toolDrop{};   // fältets mål i värden
        bool active() const { return panel || bar; }
    };
    // LANDNINGEN — ett buret objekt vars
    // fönster inte hunnit öppnas när draget slutade (ett släpp före svansen):
    // bärandets plats och lyftet (en panels, ett fälts)
    // väntar tills `syncFloats` öppnat fönstret. Platsen tappas när appen
    // sätter en egen (`floatBoundsChanged`, `setLayout`), hela posten när
    // objektet dör eller dockas.
    struct Landing {
        DockPanel*                 panel = nullptr;   // ett av de två; jämförs
        ToolBar*                   bar   = nullptr;
        std::weak_ptr<char>        life;
        std::optional<gfx::Pointf> at;              // klienthörnet
        bool                       raise = false;   // ett släppt drag
        const Widget* item() const;   // objektet som widget; pekaren jämförs
    };
    Widget* carriedItem() const;   // objektet som widget; pekaren jämförs
    // Bärs `item` just nu (och lever det)?
    bool carrying(const Widget& item) const;
    // Kan draget BÄRAS? Där fönster har skärmposition, i ett öppet,
    // aktiverat värdfönster som inte är offscreen — ett dockat objekt bara
    // när användaren får göra det flytande, ett flytande alltid.
    bool canCarry(const Widget& item) const;
    // LOSSNINGEN vid tröskeln: greppet räknas i källans fönster,
    // tillståndet blir flytande utan `onChange`, fönstret skapas i
    // svansen på bärandets plats. `frånFlik` — draget började i en flik.
    void beginCarry(Widget& item, AppWindow& källa, float x, float y,
                    bool frånFlik);
    // Fall B för ett verktygsfält: värdens fönster tar över trycket EN gång
    // (`takePointerCapture`), och allt prövas igen efter de andra fönstrens
    // krokar. Svarar om draget fortsätter.
    bool takeOverToolPress(ToolBar& t, AppWindow& källa);
    // En rörelse: fönstret flyttas och målet räknas om (en omritning bara när
    // målet ändrades, regel 1).
    void carryMove(AppWindow& källa, float x, float y);
    // Släppet: på ett mål dockas objektet, annars förblir det flytande där
    // det står och en panels fönster lyfts. Svarar om en handler kördes.
    bool carryEnd(AppWindow& källa, float x, float y);
    // Objektets eget drag (rubriken, greppet, det flytande fältets rubrik) i
    // dess fönster: bärs det följer pekaren, kan det bäras lossnar det —
    // svaret true. False = varken eller, och platshållarvägen tar vid.
    bool carryStep(Widget& item, float x, float y);
    // Släppet av ett drag som BÄRS (`carrying(item)`): `carryEnd` i objektets
    // fönster, annars avbrutet. Svarar om en handler kördes.
    bool carryRelease(Widget& item, float x, float y);
    // Lever draget — värdens capture (A), panelens eller rubrikens tryck (B),
    // eller ett släpp som pågår?
    bool liveCarry() const;
    // Ett drag vars tryck försvann där ingen krok hörde det avslutas —
    // frågas först i värdens och det flytande fönstrets ritning.
    void checkCarry();
    // OBJEKTETS TRYCK AVBRÖTS — panelens rubrik, fältets
    // grepp eller det flytande fältets rubrik fick `mouseCancel`. Fall B:s
    // avbrott: bar värden objektet i dess
    // eget fönster avslutas draget som avbrutet, som värdens egen krok gör i
    // fall A — utom under övertagandet (`Carry::takingOver`). Platshållarvägens
    // drag av objektet släpper sitt mål, och platshållaren försvinner nu
    // i stället för vid nästa ritning.
    void itemPressLost(const Widget& item);
    // SLUTET: en ögonblicksbild först, därefter städas capture och
    // tillstånd, sist körs handlarna under livstidsvakter — POSTADE när
    // draget avbröts (ingen användarkod i en krok eller en ritning). Svarar
    // om en handler kördes.
    bool finishCarry(bool avbrutet);
    // Ett objekt som förblir flytande minns platsen — ur fönstret när det är
    // öppet, annars ur bärandet: ett släpp före svansen, och då väntar en
    // LANDNING med platsen och, för ett SLÄPPT drag, lyftet.
    void rememberCarryPlace(const Carry& c, bool släppt);
    // Var `syncFloats` öppnar ett buret objekts fönster (placeringsrymden) —
    // bärandets plats, eller en väntande landnings.
    std::optional<gfx::Pointf> carryPlacement(const Widget& item) const;
    // Fönstret öppnades: objektets landning är förbrukad, och ett släppt drag
    // lyfter fönstret nu — liksom ett FÄLT som fortfarande bärs (det
    // ska ligga över de flytande panelerna redan när det dyker upp).
    void landed(const Widget& item, DockFloatWindow& win);
    // Landningar för objekt som dog, dockades eller lämnade boken rensas.
    void pruneLandings();

    // ---- fokus
    std::vector<KbdItem> kbdItems() const;
    // Tangentbordets val till fliken (`handle` false) eller handtaget för
    // `p` — ett TRYCK väljer posten, så fokusringen står på den som just
    // valdes (TabControls regel). En post som inte finns i
    // listan lämnar valet orört.
    void selectKbdItem(bool handle, const DockPanel* p);
    // Flyttar fokus som står där ingen ser det — i en infälld panel, en
    // inaktiv flik, en stängd panel, eller på värden när den slutat vara ett
    // stopp. Efter varje ändring av synlighet, aktiv flik och infällning.
    void guardFocus();
    bool focusFirstStopIn(const Widget& w);
    // Huvudytans första tabbstopp — ett stopp i värden som inte ligger i en
    // dockad panel. Svarar om ett fick fokus.
    bool focusCenter();
    // Värden tar fokus när den är ett tabbstopp; annars töms fönstrets fokus
    // (`AppWindow::clearFocus`) — inget dolt fält får behålla tangentbordet.
    void focusHostOrClear();

private:
    std::unique_ptr<detail::DockModel> model_;

    // REMSSKIKTETS DRAG — en panel i remsan identifierar den (indexet kan
    // flytta om appen ändrar modellen mitt i draget).
    DockPanel* stripDrag_  = nullptr;
    float      grip_       = 0.0f;   // var i skiktet trycket tog (fysiska px)
    float      dragThick_  = 0.0f;   // förhandsvisningens tjocklek (logiska)
    float      startThick_ = 0.0f;
    // CELLSKIKTETS DRAG — de visade panelerna på var sida om skiktet, den
    // förstas längd, och remsans VIKTER som ger den (förhandsvisningen ÄR
    // layouten ur dem, och släppet skriver just dem).
    DockPanel*         cellDragA_ = nullptr;
    DockPanel*         cellDragB_ = nullptr;
    float              cellFirst_ = 0.0f;   // dragets längd (logiska)
    float              cellStart_ = 0.0f;
    std::vector<float> cellWeights_;        // alla remsans celler; tom = vila
    // SNAPPENS INFÄLLNING under ett skiktdrag: förhandsvisningen visar
    // remsans paneler infällda, respektive cellen före (1) eller efter (2)
    // cellskiktet.
    bool stripCollapse_ = false;
    int  cellCollapse_  = 0;
    // FLIKENS OCH HANDTAGETS TRYCK.
    DockPanel* pressTab_    = nullptr;
    DockPanel* pressHandle_ = nullptr;
    bool       pressInside_ = false;   // pekaren kvar över det tryckta handtaget
    bool       tabDrag_     = false;
    float      pressX_ = 0.0f, pressY_ = 0.0f;   // fysiska px
    // DRAGET som pågår (rubrik eller flik) och dess mål — indikatorn läses
    // tillsammans med trycket (`liveDrag`), så ett avbrutet tryck släcker
    // den av sig själv.
    DockPanel* dragPanel_   = nullptr;
    bool       dragFromTab_ = false;
    Drop       drop_{};

    // PLATSHÅLLARVÄGENS GREPPDRAG och dess mål — platshållaren läses
    // tillsammans med fältets tryck (`liveToolDrag`).
    ToolBar* toolDrag_ = nullptr;
    ToolDrop toolDrop_{};

    // OVERLAYN — pekaren jämförs via `overlayPanel()`, som prövar att
    // den fortfarande är en öppen, onålad panel i modellen.
    DockPanel*        overlay_ = nullptr;
    Timer             hideTimer_;   // lämna-prövningen
    std::vector<Anim> anims_;       // glidningarna
    bool              stepPosted_ = false;   // ett steg väntar i svansen
    bool              drawn_ = false;   // ritad minst en gång — glidningens villkor

    Hit  hover_{};
    int  kbdSel_   = 0;       // tangentbordets val i `kbdItems()`
    bool inLayout_ = false;   // `setLayout` tillämpar — krokarna väntar

    // FLYTANDE (paneler och verktygsfält) — en post per panel
    // eller fält som bor i ett av mina fönster (src/dock/dock_float.h).
    std::vector<std::unique_ptr<detail::DockFloat>> floats_;
    bool floatSyncPosted_ = false;   // en synk väntar i svansen

    // DET LEVANDE DRAGET — tomt mellan dragen — och nästa
    // dragets identitet (`Carry::serial`).
    Carry                carry_;
    std::uint64_t        carrySerial_ = 0;
    std::vector<Landing> landings_;   // släpp före svansen

    friend class DockPanel;
    friend class ToolBar;
    friend class DockFloatWindow;   // stängningen och synken
    friend class ToolFloatHeader;   // det flytande fältets rubrik
};

} // namespace glaze
