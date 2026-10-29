// preview_gate — VERKTYGSVÄGENS grind.
//
// Kör:  ./build/tests/preview_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// Skälet att den finns: previewn och glaze-render måste dela EN
// `applyProp` (`bld::applyProp` i builder_preview), annars kan de glida isär
// utan att något mäter det. En publik, dokumenterad identitet kunde annars
// tyst försvinna i previewn medan bygget var grönt.
//
// Det här programmet mäter `bld::applyProp`: modellens råa
// C++-uttryckstext in, ett riktigt
// `glaze::Widget` ut, verifierat fält för fält. Headless — inga fönster,
// ingen pump, ingen `gfx::init` (widgets konstrueras och `setProp`:as
// utan renderer).
//
// FALLGROP som grinden själv vilar på: länka `glaze`, ALDRIG `glaze_impl`
// (WHOLE_ARCHIVE-regeln i rot-CLAUDE.md) — annars strippas
// widget-registreringarna och `makeWidget` returnerar nullptr för varje
// typ, vilket hade sett ut som ett fel i registret.
//
// Träd, listor och bilder mäts i `fallTrad` (flerval, kryss, färg,
// massformens sju fält positionellt/designerat/typat, nio fält och en bar
// `{200, 0, 0}` opaka — och KOLUMNERNA: cellerna som åttonde
// fältet och femte argumentet, `setCell`, `setColumns` genom ColumnPanels
// gren och sorteringens setter-fält på trädet), `fallListvy` (radfärgen),
// `fallMenyStigPreview`,
// `fallProgramikon` och `fallSystemfaltetsPoster` (menypostens tre
// tillstånd på alla tre menybärarna), kravmatrisen (trädets två setter-fält)
// och `fallCallsTradTabell` (VARJE trad- och tabell-sats i calls-fixturen
// utförs). SABOTAGEN som ska fälla den: `kSetterFields`-raden för
// multiSelect/showChecks borttagen (matrisen), `parseTreeLiteral` som
// stannar vid fem fält (fallTrad, fallCallsTradTabell), `valfri` som inte tar
// `std::nullopt` (fallTrad, fallListvy), `applyMenuItemState` bortkopplad ur
// menyradens gren (fallMenyStigPreview), ur programikonens
// (fallProgramikon) och ur systemfältsikonens i `applyComponentCall`
// (fallSystemfaltetsPoster).
//
// Trädlistornas kryss mäts i `fallListvy` (listvyns `showChecks`
// som setter-fält och `setChecked(rad, på)` som Call — tre opaka former och en
// rad utanför), `fallTrad` (`moveNodes` med stiglistan: utfört i trädordning,
// `{0}` som lista, ett saknat index och ett saknat komma opaka, avslutande
// komma giltigt, ensamt komma opakt), kravmatrisen (`showChecks` på ListView)
// och `fallCallsTradTabell` (tabellens kryss som sluttillstånd, fixturens
// `moveNodes` utförd). SABOTAGEN som ska fälla den: settertabellraden
// borttagen (matrisen), listvyns `setChecked`-gren borttagen
// (fallCallsTradTabell och fallListvy), `argPathList` som tar `{0}` (fallTrad)
// och `moveNodes` borttagen ur `ärTräd` (fallCallsTradTabell: opak sats).
//
// De icke-visuella komponenterna och fönsteranropen mäts i `fallKomponenter` (komponentfixturen
// genom `bld::buildComponents`: timerns och ikonens värden, menyn och
// tooltipens token — och att `start()` och `show()` INTE utförs),
// `fallEgnaKomponenter` (previewkontraktet: en Inert-
// registrerad egen komponent skapas och får sina rader, en StandIn-
// registrerad och en okänd skapas aldrig), `fallFonsterRam` (fönstrets nya
// anrop: ramen, startplatsen, läget, väntepekaren, `resetCursor()`),
// kravmatrisen (timerns, ikonens och pekarens setter-fält) och
// komponentoraklet i `fallSetterMatris`. SABOTAGEN som ska fälla den:
// `start()` utförd i `applyComponentCall` (fallKomponenter: isRunning),
// `show()` utförd (fallKomponenter: isVisible), Timer-raden i `kSetterFields`
// borttagen (matrisen — roundtrip_gate fäller INTE: setter- och anropsformen
// ger samma bytes), `setChrome`-grenen borttagen ur `applyWindowCall`
// (fallFonsterRam: topBar), previewformen ignorerad i `buildComponents`
// (fallEgnaKomponenter: StandIn skapas).
//
// Dockningen (modellen och layoutsträngen v2) mäts i
// `fallDockning` (panelens `closable`/`img` som setter-fält,
// `setMinimumSize`/`setMaximumSize`/`activate()` som anrop med orörda mått
// vid fel, värdens `setLayout("v2;…")` tur och retur och atomiskt, `v1`
// avvisad, `dockPanel` i båda formerna med IDENTIFIERARE och
// `glaze::Dock::`-prefix — och tre opaka former), `fallCallsDockning`
// (calls-fixturens dockning genom `bld::buildWindowTree`: `layout()` efter
// satserna är referensen) och kravmatrisen (`closable`, `img`). SABOTAGEN som ska
// fälla den: `dockPanel`-grenen borttagen ur `applyCall` (fallDockning,
// fallCallsDockning: layouten olik), `activate`-grenen borttagen
// (fallCallsDockning: sok förblir aktiv), `argKant` som godtar vilket
// enum-prefix som helst (fallDockning: `glaze::Align::Left`), `argPanel` som
// godtar en strängliteral (fallDockning) och `closable`/`img` borttagna ur
// `kSetterFields` (matrisen).
//
// Verktygsfältens band mäts i `fallCallsDockning`
// (fixturens verktygsfält med `setBand(2)` och det råa `dock = Left`, satta
// EFTER trädbygget — `layout()` säger band 0 vid vänsterkanten och fältets
// tal står kvar) och i kravmatrisen (`band`). SABOTAGEN som ska fälla den:
// `band`-raden borttagen ur `kSetterFields` (matrisen), `ToolBar::setProp`
// utan "band" (calls-dockning: `band()` 0 och strängen säger vänsterkanten
// ändå — kontrollen av `band()` fäller), och värdens synk som inte läser
// fältens `dock`/`band()` efter adoptionen (calls-dockning: fältet står kvar
// överst).
//
// Knappnålen och overlayn mäts i `fallDockning`
// (`pinned` som setter-fält: onålad är infälld och skrivs så i `layout()`;
// en onålad panel som fälls ut är värdens främre barn, och att fästa den gör
// den dockad), `fallCallsDockning` (fixturens `egenskaper.setPinned(false)`
// efter `setCollapsed(false)` ger ett handtag), `fallPreviewVandring` fall 8
// (previewns barnloop ritar `frontChild` SIST och ignorerar ett svar som inte
// är ett barn) och kravmatrisen (`pinned`). SABOTAGEN som ska fälla den:
// `frontChild` ignorerad i `drawPreviewTree` (fall 8), `pinned`-raden
// borttagen ur `kSetterFields` (matrisen), `DockPanel::setProp` utan
// "pinned" (fallDockning, calls-dockning) och `syncOverlay` som inte gör den
// senast utfällda till overlay (fallDockning: inget främre barn).
//
// De flytande panelerna mäts i `fallDockning`
// (`floating`/`floatable` som setter-fält; previewn UTFÖR tillståndet — en
// flytande panel lämnar layouten, dess barn syns inte, och `layout()` skriver
// `f=` — men öppnar inget fönster, för trädet står inte i ett öppet fönster;
// `setFloatingBounds` med fyra tal och atomiskt; `f=` tur och retur, med
// platsen `-` där fönster saknar skärmposition; fyra avvisade `f=`-former),
// `fallCallsDockning` (fixturens `setFloatingBounds` och `setFloatable(false)`
// når panelerna, och strängen är oförändrad — ingen panel flyter) och
// kravmatrisen (`floating`, `floatable`). SABOTAGEN som ska fälla den:
// `floating`/`floatable` borttagna ur `kSetterFields` (matrisen),
// `setFloatingBounds`-grenen borttagen ur `applyCall` (fallDockning,
// calls-dockning), `DockModel::normalize` som inte tar bort en flytande panel
// (fallDockning: `p=a` står kvar i strängen) och parserns platsregel
// borttagen (fallDockning: den blandade platsen godtas).
//
// Rullgardinsknappen mäts i `fallKnappmeny`
// (`split`, knappmenyn med `{}`-roten och postens tre tillstånd, atomiciteten
// och VARJE nyKnapp-sats i calls-fixturen) och i kravmatrisen (`split`).
// SABOTAGEN som ska fälla den: ToolButton-grenen borttagen ur `applyCall`
// (fallKnappmeny: menyn tom, fixturens anrop opaka), `applyMenuItemState`
// bortkopplad ur den (fallKnappmeny: posttillstånden) och `split`-raden
// borttagen ur `kSetterFields` (matrisen här; roundtrip_gate: ingen Prop —
// anropsformen utförs ändå av setter-fallbacken och fäller inte
// fixturfallet). `applyRootMenuCall<true>` för knappen fälls av BYGGET, inte
// här: knappens `addItem` har inget tredje argument.
//
// Flytande verktygsfält mäts i
// `fallVerktygsfaltFlytande` (titeln med tr-id och som literal, flytbarheten,
// `setFloatingPosition` med två tal och atomiskt, `setFloating(true)` UTFÖRT
// utan fönster med `ft=` och ingen `t=`, `ft=` tur och retur, tre avvisade
// `ft=`-former, en `t=` för ett flytande fält som dockar det STÅENDE med
// längden flyttad till höjden), `fallVerktygsfalt` (ToolBar har en
// text-setter), `fallCallsDockning` (fixturens `flytrad` och
// `verktygsrad.setFloatable(false)`) och kravmatrisen (`text`, `floating`,
// `floatable`). SABOTAGEN som ska fälla den: `text` (eller `floating`/
// `floatable`) borttagen ur `kSetterFields` (matrisen), ToolBar-grenen
// `setFloatingPosition` borttagen ur `applyCall` (fallVerktygsfaltFlytande:
// platsen lagras inte; calls-dockning: 0,0), `ToolBar::setTextToken` utan
// "text" (tokenet blir inte 166), `normalize` som inte tar bort ett flytande
// fält (`t=b` står kvar i strängen) och `turnIfNeeded` borttagen (höjden blir
// inte 140).
//
// Verktygsfältets överflöde mäts i
// `fallVerktygsfaltFlytande` (`overflow` som setter-fält åt båda hållen, och
// verktygsknappens `performClick()` KÄND men aldrig utförd, med argument opak)
// och i kravmatrisen (`overflow`). SABOTAGEN: `overflow` borttagen ur
// `kSetterFields` (matrisen), `ToolBar::setProp` utan "overflow" (fältet
// slås inte av) och `ToolButton` borttagen ur performClick-grenen (raden blir
// opak).
//
// Standard- och avbrytknapp, maxlängd och
// platshållare) mäts i `fallDialogerOchFalt` (fältens `placeholder` som
// literal och som tr-token och `maxLength` med ett opakt −1, på båda fälten;
// knappens `dialogResult`; `performClick()` KÄND men aldrig utförd; fönstrets
// `setAcceptButton`/`setCancelButton` med en MEDLEM: träff, blanktecken,
// `nullptr`, okänt namn, strängliteral och icke-knapp) och i kravmatrisen
// (fem rader). SABOTAGEN som ska fälla den: en av raderna borttagen ur
// `kSetterFields` (matrisen — roundtrip_gate skriver då en rå fältrad som inte
// kompilerar, och calls_fixture fäller bygget), `placeholder` borttagen ur
// översättningsgrenen (tokenet blir inte 2), grenen i `applyWindowCall`
// borttagen (acceptButton förblir nullptr), `performClick` utförd i previewn
// (onClick körs) och `ärIdentifierare` som godtar en strängliteral.
//
// De nya widgetformerna mäts i `fallFas1B` (en widget
// i taget genom verktygsvägen: kryssrutans `threeState`/`checkState`,
// listboxens och datagriddens flerval med `setSelected`/`selectAll`/
// `clearSelection`, datagriddens `selectionMode`, flikarnas `addPage`/
// `setPage`/`setPages`, färgväljarens `allowAlpha` med `#RRGGBBAA` i båda
// satsordningarna, datumväljarens `showTime`/`setTime` och statusradens
// `addPanel`/`setPanel`/`setPanelText`/`setPanels` — och varje FEL form
// OPAK), i `fallCallsFas1B` (calls-fixturens satser genom
// `bld::buildWindowTree`, sluttillståndet mot appens) och i kravmatrisen
// (sju rader). SABOTAGEN står vid de två fallen. VAD DEN INTE MÄTER:
// widgetarnas egna beteenden (checkbox_gate, listbox_gate, tab_gate,
// datagrid_gate, colorpicker_gate, datepicker_gate, statusbar_gate och
// range_gate) och att raderna KOMPILERAR (`calls_fixture`).
//
// Layoutstöden mäts i `fallStorlekOchKanter` (fönsteranropet
// `setSizeToContentMode` i tre prefixformer, `…::Minimum` opakt och läget
// orört), `fallReq7` (etikettens `widthGroup`: en strängliteral utförs, en
// identifierare är OPAK och etiketten orörd — också genom setter-fallbacken —
// och calls-fixturens två rader utförs), kravmatrisen (`widthGroup` på Label)
// och `fallDemoOversatt` (formulärets texter är tr-id). SABOTAGEN som ska
// fälla den: enumgrenen borttagen ur `applyWindowCall` (GrowOnly sätts inte),
// `widthGroup` genom den allmänna `setProp`-vägen ("grupp" accepteras) och
// raden borttagen ur `kSetterFields` (kravmatrisen).
//
// Gränserna, textstorleken och popuptangenterna mäts i `fallFonsteranrop` (gränserna och den
// låsta storleken i innehållsmått — namnlistens höjd räknad ur STILEN här,
// klientformen satt sist gäller, ett argument för lite är opakt — och
// `setPopupTakesKeys`), `fallTextstorlek` (setter-fältet `textSize` på de fyra
// i alla måttformer, ett icke-mått och världsenheter lämnar widgeten orörd,
// setter-fallbacken för en egen subklass och `bld::parseLength`) och i
// kravmatrisen (fyra rader). SABOTAGEN: en av de tre grenarna borttagen ur
// `applyWindowCall` (fallFonsteranrop), `setPopupTakesKeys`-grenen borttagen,
// `textSize`-grenen i `applyProp` borttagen och `harTextstorlek` utan en typ
// (fallTextstorlek).

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>
#ifdef GLAZE_PREVIEW_TERMINAL
#include <glaze_terminal/terminal.h>   // sidopaketet (fallTerminal)
#endif

#include "apply_props.h"
#include "component_tree.h"
#include "cpp_reader.h"
#include "design_theme.h"
#include "palette.h"
#include "preview_tree.h"
#include "standin_widget.h"
#include "window_model.h"
#include "window_tree.h"

namespace {

int g_fails = 0;
int g_cases = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s — fick \"%s\"\n", what.c_str(), fick.c_str());
    ++g_fails;
}

// Bygg en widget ur REGISTRET precis som previewträdet gör (klassnamnet ur
// medlemsraden), och applicera fältraderna i modellordning.
std::unique_ptr<glaze::Widget> build(const char* cppType,
                                     std::initializer_list<bld::Prop> props)
{
    std::unique_ptr<glaze::Widget> w = glaze::makeWidgetOfClass(cppType);
    if (!w) {
        std::printf("  FEL  registret känner inte %s (WHOLE_ARCHIVE?)\n",
                    cppType);
        ++g_fails;
        ++g_cases;
        return nullptr;
    }
    for (const bld::Prop& p : props)
        bld::applyProp(*w, p, "preview_gate");
    return w;
}

// ---------------------------------------------------------------- fallen

// TERMINALEN — sidopaketets widget i previewn, bara när
// paketet är länkat (`GLAZE_PREVIEW_TERMINAL`, apps/GlazeBuilder/CMakeLists.txt).
// Fixturen tests/fixtures/terminal.* körs genom den ENDA satsvägen: en riktig
// `glaze::Terminal` ur registret (ingen stand-in), setter-fälten genom
// `setProp`, textstorleken TYPAD, miljön och den egna färgen genom
// apply_terminal.cpp — och ingen process: `start()` är känd men opak, och en
// okänd metod likaså. Sabotaget `setProp` utan `program` fäller fallet.
void fallTerminal()
{
#ifdef GLAZE_PREVIEW_TERMINAL
    const bld::ReadResult r =
        bld::readWindow(std::string(GLAZE_TEST_ROOT) + "/tests/fixtures/terminal.h");
    check(r.ok, "terminal: fixturen läses", r.error);
    if (!r.ok)
        return;
    std::unique_ptr<glaze::Widget> w = glaze::makeWidgetOfClass("glaze::Terminal");
    auto* t = dynamic_cast<glaze::Terminal*>(w.get());
    check(t != nullptr, "terminal: registret ger en riktig glaze::Terminal, ingen stand-in");
    if (!t)
        return;
    bool startOpak = false;
    for (const bld::Stmt& s : r.model.stmts) {
        if (s.target != "term")
            continue;
        if (s.kind == bld::Stmt::Kind::Call && s.name == "start")
            startOpak = !bld::applyCall(*w, bld::Call{s.name, s.value}, "preview_gate");
        else
            bld::applyStmt(*w, s, "preview_gate");
    }
    check(t->program() == "pwsh" && t->arguments() == "-NoLogo" &&
              t->workingDirectory() == "C:/src",
          "terminal: programmet, argumenten och katalogen lagras", t->program());
    check(t->colorScheme() == glaze::TerminalScheme::Solarized &&
              t->colorMode() == glaze::TerminalColorMode::Dark && t->scrollback() == 5000 &&
              !t->altAsMeta(),
          "terminal: schemat, läget, historiken och Alt som Meta lagras");
    check(t->textSize().value == 12.0f && t->textSize().unit == glaze::Unit::Px,
          "terminal: textstorleken går den typade vägen (glaze::px(12))",
          std::to_string(t->textSize().value));
    check(t->environmentVariable("LANG") == std::optional<std::string>("sv_SE.UTF-8") &&
              !t->environmentVariable("PROMPT").has_value(),
          "terminal: miljövariabeln lagras och borttagningen gäller");
    check(t->palette()[glaze::TerminalColor::Red] == gfx::Color{230, 90, 80, 255},
          "terminal: den egna färgen lagras ovanpå schemat (mörk variant, låst läge)");
    check(!t->isRunning() && startOpak,
          "terminal: start() är opak — ingen process i previewn");
    check(!bld::applyCall(*w, bld::Call{"blinka", ""}, "preview_gate"),
          "terminal: en okänd metod är opak");
    check(!bld::applyCall(*w,
                          bld::Call{"setSchemeColor",
                                    "glaze::ThemeMode::System, glaze::TerminalColor::Red, "
                                    "gfx::Color{1, 2, 3}"},
                          "preview_gate"),
          "terminal: ThemeMode::System är ingen variant — raden är opak");
    check(bld::applyCall(*w, bld::Call{"clearSchemeColors", ""}, "preview_gate") &&
              t->palette()[glaze::TerminalColor::Red] != gfx::Color{230, 90, 80, 255},
          "terminal: clearSchemeColors() utförs");
#endif
}

// En `name`-rad ska nå `Widget::name`, inte bli opak.
void fallNamn()
{
    auto w = build("glaze::Button", {{"name", "\"div\""}});
    if (!w)
        return;
    check(w->name == "div", "name: raden når fältet", w->name);
}

// Raden appliceras EFTER att anropsställena satt medlemsnamnet (samma
// ordning här som i rebuildPreview/RenderWindow). De två är
// ALLTID samma sträng — den kanoniska formen — men ordningen mäts ändå:
// den är det som gör att en handskriven fil aldrig kan hamna mellan två
// sanningar.
void fallNamnSlarMedlemsnamn()
{
    std::unique_ptr<glaze::Widget> w = glaze::makeWidgetOfClass("glaze::Button");
    if (!w) {
        check(false, "name: registret känner glaze::Button");
        return;
    }
    w->name = "okBtn";                       // medlemsnamnet
    bld::applyProp(*w, {"name", "\"spara\""}, "preview_gate");
    check(w->name == "spara", "name: raden appliceras efter medlemsnamnet",
          w->name);
}

// …och nyckeln `id` är okänd: den blir en OPAK rad, precis som vilket
// okänt fält som helst (regel 4 — en `.glazeui` med den kompilerar inte,
// men verktygen förstör den inte).
void fallGamlaIdNyckeln()
{
    std::unique_ptr<glaze::Widget> w = glaze::makeWidgetOfClass("glaze::Button");
    if (!w) {
        check(false, "id: registret känner glaze::Button");
        return;
    }
    w->name = "okBtn";
    bld::applyProp(*w, {"id", "\"spara\""}, "preview_gate");
    check(w->name == "okBtn", "gamla nyckeln 'id' är OPAK — rör inte name",
          w->name);
}

// Basfälten sätts direkt ur råtexten (ingen setProp inblandad).
void fallBasfalt()
{
    auto w = build("glaze::Panel", {{"x", "12"},
                                    {"y", "34.5"},
                                    {"width", "200"},
                                    {"height", "80"},
                                    {"anchors", "{true, false, false, true}"}});
    if (!w)
        return;
    check(w->x == 12.0f && w->y == 34.5f, "basfält: x/y",
          std::to_string(w->x) + "," + std::to_string(w->y));
    check(w->width == 200.0f && w->height == 80.0f, "basfält: width/height",
          std::to_string(w->width) + "," + std::to_string(w->height));
    check(w->anchors.left && !w->anchors.top && !w->anchors.right &&
              w->anchors.bottom,
          "basfält: anchors", "");
}

// Strängliteral avciteras, enum → sista segmentet gement (setProp-formen).
void fallSetProp()
{
    auto w = build("glaze::Label", {{"text", "\"Hej åäö\""},
                                    {"dock", "glaze::Dock::Top"}});
    if (!w)
        return;
    auto* l = static_cast<glaze::Label*>(w.get());
    check(l->text() == "Hej åäö", "setProp: sträng avciteras", l->text());
    check(l->dock == glaze::Dock::Top, "setProp: enum → sista segmentet", "");
}

// A1: `Label::align` når hela vägen genom verktygsvägen. Justeringen
// SJÄLV är rent visuell och har ingen automatisk grind (en glaze-render-
// PNG eller ögat) — men att `.glazeui`-raden PARSAS har den.
void fallAlign()
{
    auto w = build("glaze::Label", {{"align", "glaze::Align::Right"}});
    if (!w)
        return;
    check(static_cast<glaze::Label*>(w.get())->align() == glaze::Align::Right,
          "align: enum → Draw::Align", "");

    // SAMMA nyckel på `TextInput` — regel 6 säger att
    // delade saker delar namn, och utan den måste en display välja mellan
    // högerjustering och att kunna markera/kopiera.
    auto t = build("glaze::TextInput", {{"align", "glaze::Align::Center"}});
    if (!t)
        return;
    check(static_cast<glaze::TextInput*>(t.get())->align() ==
              glaze::Align::Center,
          "align: samma nyckel på TextInput", "");

    // Och på `Dropdown` — datagridens val-editor står i
    // kolumnens justering, så fältet har samma egenskap med samma namn.
    auto d = build("glaze::Dropdown", {{"align", "glaze::Align::Right"}});
    if (!d)
        return;
    auto* dd = static_cast<glaze::Dropdown*>(d.get());
    check(dd->align() == glaze::Align::Right,
          "align: samma nyckel på Dropdown", "");
    bld::applyProp(*d, {"align", "glaze::Align::Kanske"}, "preview_gate");
    check(dd->align() == glaze::Align::Right,
          "align: ett okänt värde lämnar dropdownen orörd", "");
}

// `Label::wrap` når previewn. Utan den här vägen hade
// `.glazeui`-raden kompilerat i appen medan previewn ritade EN rad där
// den byggda appen ritar tre — ett WYSIWYG-brott av exakt den sort en
// publik egenskap infört i editorn ska stänga.
void fallWrap()
{
    auto w = build("glaze::Label", {{"wrap", "true"}});
    if (!w)
        return;
    check(static_cast<glaze::Label*>(w.get())->wrap(),
          "wrap: bool → widget", "");

    auto w2 = build("glaze::Label", {{"wrap", "false"}});
    if (!w2)
        return;
    check(!static_cast<glaze::Label*>(w2.get())->wrap(),
          "wrap: false är false", "");
}

// A2: `TextInput::readOnly` når previewn. Utan den här vägen hade
// `.glazeui`-raden kompilerat i appen medan previewn ritade ett vanligt,
// redigerbart fält i fel ton — alltså ett NYTT WYSIWYG-brott infört av
// ett pass vars halva syfte är att stänga dem.
void fallReadOnly()
{
    auto w = build("glaze::TextInput", {{"readOnly", "true"}});
    if (!w)
        return;
    check(static_cast<glaze::TextInput*>(w.get())->isReadOnly(),
          "readOnly: bool → widget", "");

    auto w2 = build("glaze::TextInput", {{"readOnly", "false"}});
    if (!w2)
        return;
    check(!static_cast<glaze::TextInput*>(w2.get())->isReadOnly(),
          "readOnly: false är false", "");
}

// `losen.setPassword(true);` når previewn — setter-fältet, så
// previewn ritar fältet maskerat precis som appen (regel 8). Och `undo()`/
// `redo()` är KÄNDA metodanropssatser på de två fält som bär historiken:
// raden utförs (historiken är tom, så inget syns — som i appens
// `initializeComponent`), och med argument är den OPAK.
void fallLosenordAngra()
{
    auto w = build("glaze::TextInput", {{"password", "true"}});
    if (!w)
        return;
    auto* ti = static_cast<glaze::TextInput*>(w.get());
    check(ti->isPassword(), "password: bool → widget (maskerat i previewn)",
          "");
    auto w2 = build("glaze::TextInput", {{"password", "false"}});
    if (w2)
        check(!static_cast<glaze::TextInput*>(w2.get())->isPassword(),
              "password: false är false", "");
    check(bld::isSetterField("glaze::TextInput", "password") &&
              !bld::isSetterField("glaze::TextArea", "password"),
          "password: setter-fält på TextInput och INTE på TextArea", "");

    for (const char* typ : {"glaze::TextInput", "glaze::TextArea"}) {
        auto f = build(typ, {});
        if (!f)
            continue;
        const bool ångra = bld::applyCall(*f, {"undo", ""}, "preview_gate");
        const bool görOm = bld::applyCall(*f, {"redo", "  "}, "preview_gate");
        check(ångra && görOm,
              std::string("undo/redo: ") + typ + " — kända och utförda", "");
        const bool opak1 = bld::applyCall(*f, {"undo", "1"}, "preview_gate");
        const bool opak2 = bld::applyCall(*f, {"redo", "true"}, "preview_gate");
        check(!opak1 && !opak2,
              std::string("undo/redo: ") + typ +
                  " — med argument är raden OPAK",
              "");
    }
    // Kompositerna har inga vidarebefordrade metoder (en skriven gräns).
    if (auto s = build("glaze::SpinBox", {}))
        check(!bld::applyCall(*s, {"undo", ""}, "preview_gate"),
              "undo: talfältet har ingen — raden är opak", "");
}

// Previewn UTFÖR fältens platshållare (literal och
// `glaze::tr(N)` — tokenet, aldrig den råa uttryckstexten) och
// maxlängd (ett heltal ≥ 0; annat är opakt), knappens `dialogResult`, och
// fönstrets `setAcceptButton`/`setCancelButton` med en MEDLEM som argument —
// identifieraren slagen mot trädet på `name`, `nullptr` nollställer, och ett
// okänt namn, en strängliteral och en icke-knapp är opaka. `performClick()` är
// KÄND men utförs aldrig (den kör användarkod och stänger fönstret).
void fallDialogerOchFalt()
{
    for (const char* typ : {"glaze::TextInput", "glaze::TextArea"}) {
        const std::string t = typ;
        auto f = build(typ, {{"placeholder", "\"H\xC3\xB6jd i meter\""},
                             {"maxLength", "8"}});
        if (!f)
            continue;
        const auto läs = [&](auto* fält) {
            check(fält->placeholder() == "H\xC3\xB6jd i meter" &&
                      fält->placeholderToken().id() == 0,
                  t + ": placeholder — en literal når fältet",
                  fält->placeholder());
            check(fält->maxLength() == 8, t + ": maxLength 8 ⇒ 8",
                  std::to_string(fält->maxLength()));
            bld::applyProp(*f, {"maxLength", "-1"}, "preview_gate");
            check(fält->maxLength() == 8,
                  t + ": maxLength -1 ⇒ opak rad, gränsen orörd",
                  std::to_string(fält->maxLength()));
            bld::applyProp(*f, {"placeholder", "glaze::tr(2)"}, "preview_gate");
            check(fält->placeholderToken().id() == 2,
                  t + ": placeholder med glaze::tr(2) bär TOKENET",
                  fält->placeholder());
        };
        if (t == "glaze::TextInput")
            läs(static_cast<glaze::TextInput*>(f.get()));
        else
            läs(static_cast<glaze::TextArea*>(f.get()));
    }

    auto b = build("glaze::Button",
                   {{"dialogResult", "glaze::DialogResult::Cancel"}});
    if (b) {
        auto* knapp = static_cast<glaze::Button*>(b.get());
        check(knapp->dialogResult() == glaze::DialogResult::Cancel,
              "dialogResult: glaze::DialogResult::Cancel ⇒ Cancel");
        bld::applyProp(*b, {"dialogResult", "glaze::DialogResult::Kanske"},
                       "preview_gate");
        check(knapp->dialogResult() == glaze::DialogResult::Cancel,
              "dialogResult: ett okänt värde är opakt — orört");
        int klick = 0;
        b->onClick = [&klick] { ++klick; };
        check(bld::applyCall(*b, {"performClick", ""}, "preview_gate") &&
                  klick == 0,
              "performClick(): KÄND men utförs inte — onClick körs noll gånger",
              std::to_string(klick));
        check(!bld::applyCall(*b, {"performClick", "1"}, "preview_gate"),
              "performClick(1): med argument är raden opak");
    }

    // FÖNSTERANROPEN med en medlem — i pass 2, efter trädbygget, som här.
    glaze::AppWindow w("preview_gate", 400, 300);
    auto*            ok = w.root().add<glaze::Button>("OK");
    ok->name            = "okBtn";
    auto* etikett       = w.root().add<glaze::Label>("Text");
    etikett->name       = "etikett";
    check(bld::applyWindowCall(w, {"setAcceptButton", "okBtn"},
                               "preview_gate") &&
              w.acceptButton() == ok,
          "setAcceptButton(okBtn): identifieraren slås upp i trädet");
    check(bld::applyWindowCall(w, {"setCancelButton", " okBtn "},
                               "preview_gate") &&
              w.cancelButton() == ok,
          "setCancelButton( okBtn ): blanktecken runt namnet går bra");
    check(bld::applyWindowCall(w, {"setAcceptButton", "nullptr"},
                               "preview_gate") &&
              w.acceptButton() == nullptr,
          "setAcceptButton(nullptr): nollställer");
    check(!bld::applyWindowCall(w, {"setAcceptButton", "saknas"},
                                "preview_gate") &&
              w.acceptButton() == nullptr,
          "setAcceptButton(saknas): ett okänt namn är opakt");
    check(!bld::applyWindowCall(w, {"setAcceptButton", "\"okBtn\""},
                                "preview_gate") &&
              w.acceptButton() == nullptr,
          "setAcceptButton(\"okBtn\"): en strängliteral är ingen medlem — opak");
    check(!bld::applyWindowCall(w, {"setCancelButton", "etikett"},
                                "preview_gate") &&
              w.cancelButton() == ok,
          "setCancelButton(etikett): en icke-knapp är opak — orört");
}

// D1: `Label::Role::Heading` OCH `Role::Titlebar` når previewn.
// `titlebar` måste finnas i `parseColor` — annars faller verktygen tyst
// tillbaka på `Text`, alltså fel FÄRG i previewn. Med `heading` ger
// missen dessutom fel FONTSTORLEK.
void fallRoll()
{
    auto h = build("glaze::Label", {{"role", "glaze::Label::Role::Heading"}});
    if (!h)
        return;
    check(static_cast<glaze::Label*>(h.get())->role() ==
              glaze::Label::Role::Heading,
          "role: Heading (bär storleken)", "");

    auto t = build("glaze::Label", {{"role", "glaze::Label::Role::Titlebar"}});
    if (!t)
        return;
    check(static_cast<glaze::Label*>(t.get())->role() ==
              glaze::Label::Role::Titlebar,
          "role: Titlebar (fanns i C++, saknades i verktygen)", "");
}

// METODANROPEN utförs på previewträdets widgets. Statusraden
// är fallet posten handlar om — den skapas DOLD (som i fönstret) och
// `statusBar.show();` ska göra den synlig OCH dockad i botten. Utan den
// här vägen kan reader/writer vara gröna medan previewn fortfarande
// saknar raden.
void fallAnrop()
{
    auto sb = build("glaze::StatusBar", {});
    if (!sb)
        return;
    sb->hide();                        // fönstrets utgångsläge
    check(!sb->isVisible(), "statusBar: dold som default", "");
    check(bld::applyCall(*sb, {"show", ""}, "preview_gate"),
          "statusBar: show() känns igen", "");
    check(sb->isVisible(), "statusBar: synlig efter show()", "");
    check(sb->dock == glaze::Dock::Bottom, "statusBar: dockad i botten", "");

    // Anrop MED argument, widget-specifikt.
    auto pr = build("glaze::Progress", {});
    if (!pr)
        return;
    check(bld::applyCall(*pr, {"setIndeterminate", "true"}, "preview_gate"),
          "setIndeterminate(true): känns igen", "");

    // Regel 4: OKÄNT metodnamn är OPAKT — loggas, ignoreras, ALDRIG
    // fail-safe. En okänd metod är samma sak som ett okänt fält.
    auto b = build("glaze::Button", {});
    if (!b)
        return;
    check(!bld::applyCall(*b, {"blinkTwice", ""}, "preview_gate"),
          "okänt metodnamn: opakt (false), inte fail-safe", "");
    // `focus()` finns på Widget men mappas MEDVETET inte — previewn har
    // ingen fokusrymd. Samma opaka utfall, och det är rätt.
    check(!bld::applyCall(*b, {"focus", ""}, "preview_gate"),
          "focus(): känd C++-metod, opak i previewn", "");
}

// NAKNA FÖNSTERANROP utförs på fönstret. Formen måste gå att skriva i
// `.glazeui`: `setContentSize` finns för att appen aldrig ska behöva
// kopiera in `barHeight`, och filen ÄR WYSIWYG-vägen.
//
// Fönstret behöver ingen pump och ingen display: tvåfasen gör
// konstruktorn ofarlig, precis som window_gate utnyttjar.
void fallFonsteranrop()
{
    glaze::AppWindow w("preview_gate", 400, 300, gfx::Chrome::Custom);

    check(bld::applyWindowCall(w, {"setTitle", "\"Ny titel\""},
                               "preview_gate") &&
              w.title() == "Ny titel",
          "setTitle: känns igen och avciteras", w.title());

    check(bld::applyWindowCall(w, {"setMinimumSize", "320, 180"},
                               "preview_gate") &&
              w.minimumSize().w == 320.0f && w.minimumSize().h == 180.0f,
          "setMinimumSize: två heltal ur den råa argumenttexten", "");
    check(bld::applyWindowCall(w, {"setMaximumSize", "800, 600"},
                               "preview_gate") &&
              w.maximumSize().w == 800.0f,
          "setMaximumSize: känns igen", "");

    check(bld::applyWindowCall(w, {"setContentSize", "360, 200"},
                               "preview_gate"),
          "setContentSize: känns igen (uppskjuten upplösning)", "");

    // GRÄNSERNA I INNEHÅLLSMÅTT. Fönstret är Custom och stängt:
    // chromet är namnlisten, och dess höjd räknas HÄR ur stilen (avrundad
    // till hela px) — aldrig ur produktionens `chromeInsetPx`.
    const float namnlist = std::floor(w.style().barHeight + 0.5f);
    check(bld::applyWindowCall(w, {"setMinimumContentSize", "300, 150"},
                               "preview_gate") &&
              w.minimumContentSize().w == 300.0f &&
              w.minimumContentSize().h == 150.0f &&
              w.minimumSize().w == 300.0f &&
              w.minimumSize().h == 150.0f + namnlist,
          "setMinimumContentSize: innehållsmått, klientgränsen + namnlisten",
          std::to_string(w.minimumSize().h));
    check(bld::applyWindowCall(w, {"setMaximumContentSize", "900, 0"},
                               "preview_gate") &&
              w.maximumContentSize().w == 900.0f &&
              w.maximumContentSize().h == 0.0f && w.maximumSize().h == 0.0f,
          "setMaximumContentSize: 0 förblir obegränsat i båda måtten",
          std::to_string(w.maximumSize().h));
    check(bld::applyWindowCall(w, {"setFixedContentSize", "360, 140"},
                               "preview_gate") &&
              w.isFixedSize() && w.minimumContentSize().h == 140.0f &&
              w.maximumContentSize().w == 360.0f &&
              w.maximumSize().h == 140.0f + namnlist,
          "setFixedContentSize: låst i innehållsmått (isFixedSize)",
          std::to_string(w.maximumSize().h));
    check(!bld::applyWindowCall(w, {"setFixedContentSize", "360"},
                                "preview_gate") &&
              w.minimumContentSize().h == 140.0f,
          "setFixedContentSize med ett argument: opak, gränserna orörda", "");
    // Klientformen satt SIST gäller igen — och läses i innehållsmått.
    check(bld::applyWindowCall(w, {"setMinimumSize", "320, 180"},
                               "preview_gate") &&
              w.minimumSize().h == 180.0f &&
              w.minimumContentSize().h == 180.0f - namnlist && !w.isFixedSize(),
          "setMinimumSize efter innehållsformen: klientmått, sist satt gäller",
          std::to_string(w.minimumContentSize().h));

    check(bld::applyWindowCall(w, {"setMaximizeBox", "false"},
                               "preview_gate") &&
              !w.maximizeBox(),
          "setMaximizeBox(false): boolen läses ur argumenttexten", "");
    check(bld::applyWindowCall(w, {"setModal", "true"}, "preview_gate") &&
              w.isModal(),
          "setModal(true): känns igen", "");
    check(bld::applyWindowCall(w, {"setTopmost", "true"}, "preview_gate") &&
              w.topmost(),
          "setTopmost(true): känns igen (lagras före show)", "");
    // AKTIVITETSFÄLTET: förvalet är true, så `false` i
    // filen måste nå attributet för att fallet ska bli grönt.
    check(w.showInTaskbar(), "showInTaskbar: förvalet är true", "");
    check(bld::applyWindowCall(w, {"setShowInTaskbar", "false"},
                               "preview_gate") &&
              !w.showInTaskbar(),
          "setShowInTaskbar(false): känns igen (attributet, före show)", "");
    check(bld::applyWindowCall(w, {"setPopup", "true"}, "preview_gate"),
          "setPopup(true): känns igen", "");
    // POPUPEN SOM TAR TANGENTBORDET: förvalet är false, så
    // `true` i filen måste nå attributet.
    check(!w.popupTakesKeys(), "popupTakesKeys: förvalet är false", "");
    check(bld::applyWindowCall(w, {"setPopupTakesKeys", "true"},
                               "preview_gate") &&
              w.popupTakesKeys(),
          "setPopupTakesKeys(true): känns igen (attributet, före show)", "");

    // Regel 4: OKÄNT fönsteranrop är OPAKT — samma utfall som ett okänt
    // metodanrop på en widget. ALDRIG fail-safe.
    check(!bld::applyWindowCall(w, {"setSomethingNew", "true"},
                                "preview_gate"),
          "okänt fönsteranrop: opakt (false), inte fail-safe", "");
    // Riktig metod som verktygen medvetet inte utför — fönstrets `focus()`.
    check(!bld::applyWindowCall(w, {"setAnimating", "false"},
                                "preview_gate"),
          "setAnimating: känd C++-metod, opak i verktygen", "");
    // Fel argumentform på ett KÄNT namn faller också till opak — att
    // gissa hade varit värre (regel 4).
    check(!bld::applyWindowCall(w, {"setMinimumSize", "320"},
                                "preview_gate"),
          "setMinimumSize med ett argument: opak, ingen gissning", "");
}

// Regel 4: okänt fält är OPAKT — det loggas och ignoreras, aldrig
// fail-safe och aldrig en gissning.
void fallOpak()
{
    auto w = build("glaze::Button", {{"finnsInte", "42"}});
    if (!w)
        return;
    check(true, "opak: okänt fält passerar utan att kasta", "");
}

// ---------------------------------------------------------------- LISTBOX
//
// `items`: modellen lagrar raden RÅ — readern normaliserar
// `dd.setItems({"a", "b"})` till `Prop{"items", "{\"a\", \"b\"}"}` — och
// den råa texten får INTE gå vidare till `setProp("items", …)`, som splittar
// på ';' — då blir det EN skräprad med klamrar och citattecken. Vägen är
// TYPAD via `bld::parseStringList`.

void fallItemsLista()
{
    auto w = build("glaze::Dropdown", {{"items", "{\"Alfa\", \"Beta\"}"}});
    if (!w)
        return;
    auto* d = static_cast<glaze::Dropdown*>(w.get());
    check(d->items().size() == 2 && d->items()[0] == "Alfa" &&
              d->items()[1] == "Beta",
          "items: C++-listliteralen blir TVÅ rader, inte en skräprad",
          std::to_string(d->items().size()) +
              (d->items().empty() ? "" : " [" + d->items()[0] + "]"));

    auto l = build("glaze::ListBox", {{"items", "{\"Ett\", \"Två\", \"Tre\"}"}});
    if (!l)
        return;
    check(static_cast<glaze::ListBox*>(l.get())->items().size() == 3,
          "items: samma väg för ListBox", "");

    // OGILTIG INRE SYNTAX ska bli OPAK, inte tyst förvrängd.
    // C++ konkatenerar `"a" "b"` till `ab`, men det är INTE en enda literal —
    // och en parser som bara kontrollerar första och sista
    // tecknet godtar den. Resultatet blir ett element med innehållet
    // `a" "b`. En degraderad rad är värre än ingen rad alls.
    auto k = build("glaze::ListBox", {{"items", "{\"a\" \"b\"}"}});
    if (k)
        check(static_cast<glaze::ListBox*>(k.get())->items().empty(),
              "items: sammanslagen literal avvisas -> OPAK, inte förvrängd",
              std::to_string(
                  static_cast<glaze::ListBox*>(k.get())->items().size()));

    auto u = build("glaze::ListBox", {{"items", "{namn}"}});
    if (u)
        check(static_cast<glaze::ListBox*>(u.get())->items().empty(),
              "items: ett UTTRYCK avvisas -> OPAK", "");
}

// De TRE fall semikolonformatet inte kan representera.
void fallItemsForlustfri()
{
    {
        auto w = build("glaze::ListBox", {{"items", "{\"A;B\", \"C\"}"}});
        if (!w) return;
        auto* l = static_cast<glaze::ListBox*>(w.get());
        check(l->items().size() == 2 && l->items()[0] == "A;B",
              "items: SEMIKOLON inuti ett element överlever",
              l->items().empty() ? "" : l->items()[0]);
    }
    {
        auto w = build("glaze::ListBox", {{"items", "{\"A\", \"\"}"}});
        if (!w) return;
        auto* l = static_cast<glaze::ListBox*>(w.get());
        check(l->items().size() == 2 && l->items()[1].empty(),
              "items: ett TOMT element SIST överlever",
              std::to_string(l->items().size()));
    }
    {
        auto w = build("glaze::ListBox",
                       {{"items", "{\"sa\\\" hon\", \"c:\\\\tmp\"}"}});
        if (!w) return;
        auto* l = static_cast<glaze::ListBox*>(w.get());
        check(l->items().size() == 2 && l->items()[0] == "sa\" hon" &&
                  l->items()[1] == "c:\\tmp",
              "items: ESCAPES avkodas (\\\" och \\\\)",
              l->items().size() < 2 ? "" : l->items()[0] + "|" + l->items()[1]);
    }
    {
        auto w = build("glaze::ListBox", {{"items", "{}"}});
        if (!w) return;
        check(static_cast<glaze::ListBox*>(w.get())->items().empty(),
              "items: {} är en TOM lista, inte ett fel", "");
    }
    {
        // PARSEFEL ⇒ raden förblir OPAK och ignoreras. Att degradera till
        // en skräplista vore att tyst visa fel data.
        auto w = build("glaze::ListBox", {{"items", "minVektor"}});
        if (!w) return;
        check(static_cast<glaze::ListBox*>(w.get())->items().empty(),
              "items: oparsbart uttryck lämnas OPAKT (ingen skräplista)",
              std::to_string(
                  static_cast<glaze::ListBox*>(w.get())->items().size()));
    }
}

// `selectedIndex` var det ANDRA previewfelet: fältet heter så i
// `kSetterFields`, men `Dropdown::setProp` kände bara `selected` — så
// `typDd.setSelectedIndex(1);` i demons `.glazeui` blev en opak rad.
void fallSelectedIndex()
{
    auto w = build("glaze::Dropdown",
                   {{"items", "{\"a\", \"b\", \"c\"}"}, {"selectedIndex", "2"}});
    if (!w)
        return;
    auto* d = static_cast<glaze::Dropdown*>(w.get());
    check(d->selectedIndex() == 2 && d->selectedText() == "c",
          "selectedIndex: Dropdown",
          std::to_string(d->selectedIndex()));

    auto l = build("glaze::ListBox", {{"items", "{\"a\", \"b\"}"},
                                      {"selectedIndex", "1"},
                                      {"showChecks", "true"}});
    if (!l)
        return;
    auto* lb = static_cast<glaze::ListBox*>(l.get());
    check(lb->selectedIndex() == 1 && lb->showsChecks(),
          "selectedIndex + showChecks: ListBox",
          std::to_string(lb->selectedIndex()));
}

// Paletten måste känna typen (verktygslådan), och registret måste känna
// KLASSEN — det är den trädbyggarna frågar.
void fallListBoxIPaletten()
{
    const bld::PaletteEntry* pe = bld::paletteByCpp("glaze::ListBox");
    check(pe != nullptr, "paletten känner glaze::ListBox", "");
    check(glaze::widgetClassOf("listbox") == "glaze::ListBox",
          "registrets klass bakom 'listbox' är glaze::ListBox",
          glaze::widgetClassOf("listbox"));
}

} // namespace

// ----------------------------------------------------------- syskonankare
//
// Fältets värde är en C++-STRUCTLITERAL och inte ett tal eller en sträng,
// alltså den tredje värdeformen efter `anchors` och `items`. Grinden mäter
// tre saker som annars bara syns i en renderad canvas: att den kanoniska
// formen sätter BÅDA delarna, att de två KORTA formerna (som kompilerar)
// också utförs, och att en ogiltig text blir OPAK i stället för ett halvt
// ankare — att tyst placera en widget på fel ställe är värre än att låta
// bli.
void fallSyskonankare()
{
    auto w = build("glaze::Button", {{"anchorLeft", "{\"knapp7\", 8}"},
                                     {"anchorTop", "{\"rubrik\", 12.5}"}});
    if (!w)
        return;
    check(w->anchorLeft.to == "knapp7" && w->anchorLeft.gap == 8.0f,
          "syskonankare: anchorLeft id + gap",
          w->anchorLeft.to + "/" + std::to_string(w->anchorLeft.gap));
    check(w->anchorTop.to == "rubrik" && w->anchorTop.gap == 12.5f,
          "syskonankare: anchorTop tar decimaltal",
          w->anchorTop.to + "/" + std::to_string(w->anchorTop.gap));
    check(w->anchorRight.to.empty() && w->anchorBottom.to.empty(),
          "syskonankare: orörda kanter förblir tomma", "");
}

// De två korta formerna — båda är laglig C++ och måste därför UTFÖRAS
// (hård regel 8: en rad som kompilerar och som previewn inte utför är ett
// WYSIWYG-brott, inte en känd begränsning).
void fallSyskonankareKortForm()
{
    auto w = build("glaze::Button", {{"anchorLeft", "{\"knapp7\"}"},
                                     {"anchorRight", "{}"}});
    if (!w)
        return;
    check(w->anchorLeft.to == "knapp7" && w->anchorLeft.gap == 0.0f,
          "syskonankare: {\"id\"} ⇒ gap 0", w->anchorLeft.to);
    check(w->anchorRight.to.empty(), "syskonankare: {} ⇒ inget ankare",
          w->anchorRight.to);
}

// OGILTIG text ⇒ OPAK rad: fältet lämnas orört, ingenting halvsätts, och
// raden round-trippas ordagrant vid spara (regel 4). Den får ALDRIG falla
// vidare till `setProp`, vars textform är en annan ("id,gap") och som
// därmed hade loggat fel sak om samma rad.
void fallSyskonankareOpak()
{
    auto w = build("glaze::Button",
                   {{"anchorLeft", "nagotAnnat"},
                    {"anchorTop", "{knapp7, 8}"},      // id utan citat
                    {"anchorBottom", "{\"a\", \"b\"}"}});   // gap ej tal
    if (!w)
        return;
    check(w->anchorLeft.to.empty(), "opak: rå identifierare rör inte fältet",
          w->anchorLeft.to);
    check(w->anchorTop.to.empty(), "opak: id utan citattecken",
          w->anchorTop.to);
    check(w->anchorBottom.to.empty(), "opak: gap som inte är ett tal",
          w->anchorBottom.to);
}

// EMISSIONEN är den kanoniska tvåformen, och den ska gå att läsa tillbaka
// med samma värden — round-trippen i miniatyr, mätt där konverteringen
// faktiskt bor.
void fallSyskonankareEmission()
{
    const std::string t = bld::siblingAnchorText("knapp7", 8.0f);
    check(t == "{\"knapp7\", 8}", "emission: kanonisk form", t);

    std::string till;
    float       gap = 0.0f;
    check(bld::parseSiblingAnchorText(t, till, gap) && till == "knapp7" &&
              gap == 8.0f,
          "emission → parse ger samma värden", till);

    const std::string d = bld::siblingAnchorText("a", 12.5f);
    check(d == "{\"a\", 12.5}", "emission: decimaltal utan skräp", d);
}

// TALGRINDEN. `std::strtof` avkodar "nan" och
// "inf", så `parseNum` får inte konsumera hela strängen. Det är
// värre än det låter: builderns grind lovar att `.glazeui` aldrig bär text
// som inte kompilerar, previewn skulle få ett NaN-mått — som förgiftar varje
// jämförelse och varje summa det rör — och den byggda appen kan inte
// kompilera, eftersom `nan` inte är någon C++-literal.
//
// Fältet spelar ingen roll: felet sitter i `parseNum`, alltså under x/y/
// width/height/right/bottom OCH ankarnas gap. Det här mäter båda vägarna.
void fallTalgrind()
{
    float f = -1.0f;
    check(!bld::parseNum("nan", f), "tal: \"nan\" avvisas", "");
    check(!bld::parseNum("inf", f), "tal: \"inf\" avvisas", "");
    check(!bld::parseNum("-inf", f), "tal: \"-inf\" avvisas", "");
    check(!bld::parseNum("NAN", f), "tal: versal \"NAN\" avvisas", "");
    check(!bld::parseNum("1e400", f), "tal: overflow ⇒ inte ändligt", "");
    check(!bld::parseNum("abc", f), "tal: ren text avvisas", "");

    // Det som ÄR giltig C++ måste fortsätta gå igenom — en grind som
    // stänger för hårt är också en regression.
    check(bld::parseNum("8", f) && f == 8.0f, "tal: heltal", "");
    check(bld::parseNum("-3.5", f) && f == -3.5f, "tal: negativt decimal", "");
    check(bld::parseNum("12.5f", f) && f == 12.5f, "tal: f-suffix", "");
    check(bld::parseNum("1e3", f) && f == 1000.0f, "tal: exponentform", "");
    check(bld::parseNum(".5", f) && f == 0.5f, "tal: ledande punkt", "");

    // Och vägen hela vägen ut: ett NaN-gap får inte nå widgeten.
    auto w = build("glaze::Button", {{"anchorLeft", "{\"a\", nan}"},
                                     {"x", "nan"}});
    if (!w)
        return;
    check(w->anchorLeft.to.empty(), "NaN-gap ⇒ OPAK ankarrad",
          w->anchorLeft.to);
    check(w->x == 0.0f, "NaN i x ⇒ fältet orört", std::to_string(w->x));
}

// ESCAPE-DELMÄNGDEN hela vägen till widgeten.
// Avkodas `\n` till bokstaven `n` visar previewn
// `radnrad` medan den byggda appen bryter raden — och en omredigering skriver
// tillbaka den FÖRVRÄNGDA strängen.
void fallEscapeText()
{
    auto w = build("glaze::Label", {{"text", "\"rad\\nrad\""}});
    if (!w)
        return;
    auto* l = static_cast<glaze::Label*>(w.get());
    check(l->text() == "rad\nrad", "escape: \\n blir en RADBRYTNING, inte 'n'",
          l->text());

    auto t = build("glaze::Label", {{"text", "\"a\\tb\\\"c\\\"\""}});
    if (t)
        check(static_cast<glaze::Label*>(t.get())->text() == "a\tb\"c\"",
              "escape: \\t och \\\" i samma literal",
              static_cast<glaze::Label*>(t.get())->text());

    // UTF-8 oförändrad — svensk text är normalfallet.
    auto u = build("glaze::Label", {{"text", "\"Hej åäö\""}});
    if (u)
        check(static_cast<glaze::Label*>(u.get())->text() == "Hej åäö",
              "escape: UTF-8 passerar oförändrad",
              static_cast<glaze::Label*>(u.get())->text());
}

// …och det som ligger UTANFÖR delmängden blir en OPAK rad, aldrig en
// gissad avkodning. `\x41` blir `A` i en tillåtande parser,
// och emissionen skriver därefter `"A"` tillbaka — en tyst omskrivning
// av användarens rad (regel 4).
void fallEscapeOpak()
{
    // `parseCppString` avvisar ⇒ raden är OPAK. Poängen är att widgeten
    // ALDRIG får se den påhittade avkodningen "A". Textfälten går genom
    // `applyTextField` — inte RÅTEXTEN genom `setProp`, som hade visat `"\x41"`
    // med citattecknen —
    // som lämnar målet ORÖRT för allt som varken är en literal eller
    // `glaze::tr(N)` — previewn visar aldrig ett värde appen inte visar.
    // Raden bevaras i MODELLEN (roundtrip_gate), inte i widgeten.
    auto w = build("glaze::Label", {{"text", "\"\\x41\""}});
    if (!w)
        return;
    const std::string t = static_cast<glaze::Label*>(w.get())->text();
    check(t != "A", "escape: \\x41 avkodas ALDRIG till 'A'", t);
    check(t.empty(),
          "escape: \\x41-raden är opak — etiketten lämnas orörd", t);

    // Avslutande backslash: literalen är inte komplett och ska avvisas.
    std::string v;
    check(!bld::parseCppString("\"abc\\\"", v),
          "escape: avslutande backslash avvisas", v);
}

// MENYTEXTEN går genom SAMMA parser (`applyCall` → `parseCppString`), så
// delmängden gäller där också — en menyrubrik med `\n` avkodas, och en
// med `\x41` blir en tom sträng i stället för en påhittad bokstav.
void fallEscapeMeny()
{
    auto w = glaze::makeWidget("menubar");
    if (!w) {
        std::printf("  FEL  registret känner inte menubar\n");
        ++g_fails;
        ++g_cases;
        return;
    }
    auto* mb = static_cast<glaze::MenuBar*>(w.get());
    bld::applyCall(*w, {"addMenu", "\"Ar\\tkiv\""}, "preview_gate");
    check(mb->menuCount() == 1 && mb->menuTitle(0) == "Ar\tkiv",
          "meny: escapen avkodas i rubriken",
          mb->menuCount() ? mb->menuTitle(0) : std::string("(ingen)"));
}

// ------------------------------------------- ÖVERSÄTTNINGEN
//
// Regel 8 i båda riktningarna: previewn ska UTFÖRA `setText(glaze::tr(N))`
// — annars visar canvasen den råa uttryckstexten som etikett — och den
// ska ALDRIG göra mer än C++-koden, alltså sätta TOKENET och inte en
// fryst sträng.
void fallTrText()
{
    glaze::setLanguage("sv");
    {
        glaze::Button b;
        bld::applyProp(b, {"text", "glaze::tr(2)"}, "preview_gate");
        check(b.text() == "Avbryt", "tr: grenen UTFÖRS (sv)", b.text());
        check(b.textToken().id() == 2,
              "tr: TOKENET sätts, inte en fryst sträng",
              std::to_string(b.textToken().id()));
        glaze::setLanguage("fi");
        check(b.text() == "Peruuta",
              "tr: previewn följer språkbytet — den fryser inte texten",
              b.text());
        glaze::setLanguage("sv");
    }
    // TRASIG tr-rad ⇒ OPAK, och widgeten ORÖRD. `glaze::tr(kOk)` är en
    // konstant `builder_core` inte kan lösa upp, och att GISSA vore ett
    // regel 4-brott. Att uttrycket blir
    // knappens TEXT ("glaze::tr(kOk)") vore felet självt: widgeten ska vara orörd.
    //
    // SABOTAGE: låt textfälten falla igenom till den allmänna
    // värdenormaliseringen i `applyProp` (ta bort `ärTextfält`-grenen) ⇒
    // knappen visar uttrycket och de tre första fallen faller.
    {
        glaze::Button b("orörd");
        bld::applyProp(b, {"text", "glaze::tr(kOk)"}, "preview_gate");
        check(b.text() == "orörd",
              "tr: en KONSTANT i tr-anropet är opak — knappen orörd, aldrig "
              "uttrycket som text (🐛 58)", b.text());
        check(b.textToken().id() == 0,
              "tr: …och NÅGOT id sätts aldrig av en oparsbar rad");
        bld::applyProp(b, {"text", "glaze::tr(app::str::Hej)"}, "preview_gate");
        check(b.text() == "orörd",
              "tr: en kvalificerad konstant (app::str::Hej) är opak", b.text());
        bld::applyProp(b, {"text", "kNamn"}, "preview_gate");
        bld::applyProp(b, {"title", "foo::Bar"}, "preview_gate");
        check(b.text() == "orörd",
              "text: ett UTTRYCK (en konstant, ett enum-likt namn) är opakt — "
              "förut blev det texten \"kNamn\" respektive \"bar\"", b.text());
        glaze::TextInput fält;
        bld::applyProp(fält, {"placeholder", "glaze::tr(kTips)"}, "preview_gate");
        bld::applyProp(fält, {"toolTip", "kTips"}, "preview_gate");
        check(fält.placeholder().empty() && fält.toolTip().empty(),
              "placeholder/toolTip: konstanten och uttrycket är opaka",
              fält.placeholder() + " / " + fält.toolTip());
        bld::applyProp(b, {"text", "\"Literal\""}, "preview_gate");
        check(b.text() == "Literal", "text: literalen går som förut", b.text());
    }
    // KLASSNINGEN (builder_core) — den previewn och panelen delar.
    {
        std::string   lit;
        std::uint32_t id = 0;
        check(bld::classifyTextValue("glaze::tr(app::str::Hej)", lit, id) ==
                  bld::TextValueKind::TrExpr,
              "klass: glaze::tr(<konstant>) är TrExpr");
        check(bld::classifyTextValue("glaze::tr(a) + glaze::tr(b)", lit, id) ==
                  bld::TextValueKind::Other,
              "klass: två tr-anrop i ett uttryck är Other, inte TrExpr");
        check(bld::classifyTextValue("glaze::tr()", lit, id) ==
                  bld::TextValueKind::Other,
              "klass: ett tomt tr-anrop är Other");
    }
    // SETTER-FALLBACKEN — en EGEN Label-subklass emitteras i anropsform
    // (`minEtikett.setText(…)`, writern skriver om bara glaze-typerna). Där
    // får `glaze::tr(2)` inte bli TEXTEN "glaze::tr(2)".
    //
    // SABOTAGE: ta bort textgrenen ur setter-fallbacken i `applyCall` ⇒
    // etiketten visar "glaze::tr(2)" och fallet faller.
    {
        struct MinEtikett : glaze::Label {};
        MinEtikett e;
        check(bld::applyCall(e, {"setText", "glaze::tr(2)"}, "preview_gate") &&
                  e.textToken().id() == 2,
              "fallback: setText(glaze::tr(2)) på en egen Label-subklass ger "
              "TOKENET", e.text());
        e.setText("orörd");
        check(!bld::applyCall(e, {"setText", "glaze::tr(kX)"}, "preview_gate") &&
                  e.text() == "orörd",
              "fallback: en konstant i anropsform är opak och etiketten orörd",
              e.text());
    }
    // FÖNSTRETS TITEL: readern tar `setTitle(<konstant>)` som en opak
    // sats, och fönstret får INGEN titel — inte uttrycket.
    {
        glaze::AppWindow w("orörd", 100, 100, gfx::Chrome::Custom);
        check(!bld::applyWindowCall(w, {"setTitle", "glaze::tr(app::str::T)"},
                                    "preview_gate") &&
                  w.title() == "orörd",
              "fönster: setTitle(glaze::tr(<konstant>)) utförs inte", w.title());
        bld::WindowModel m;
        m.className = "F";
        m.appendWindowCall("setTitle", "glaze::tr(app::str::T)");
        check(m.title().empty(),
              "fönster: modellens title() är tom för en titel som inte är en "
              "literal (glaze-render konstruerar fönstret med den)", m.title());
    }
    // KOMPONENTEN — tooltipen på en systemfältsikon går samma regel.
    {
        bld::WindowModel m;
        m.className = "F";
        m.appendNode("glaze::TrayIcon", "ikon");
        m.setProp("ikon", "name", "\"ikon\"");
        m.setProp("ikon", "toolTip", "glaze::tr(kTips)");
        const std::vector<bld::PreviewComponent> komp =
            bld::buildComponents(m, "preview_gate");
        auto* ti = komp.empty()
                       ? nullptr
                       : dynamic_cast<glaze::TrayIcon*>(komp[0].object.get());
        check(ti && ti->toolTip().empty() && ti->toolTipToken().id() == 0,
              "komponent: en tooltip med en konstant är opak",
              ti ? ti->toolTip() : std::string("(ingen ikon)"));
    }
    // Andra widgets på samma väg.
    {
        glaze::Label l;
        bld::applyProp(l, {"text", "glaze::tr(3)"}, "preview_gate");
        check(l.textToken().id() == 3, "tr: Label bär tokenet");
        glaze::Checkbox cb;
        bld::applyProp(cb, {"text", "glaze::tr(4)"}, "preview_gate");
        check(cb.textToken().id() == 4, "tr: Checkbox bär tokenet");
    }

    // ---------------------------------------------------------------
    // EN TYP UTAN `TrText`-SÄTTARE ⇒ OPAK, widgeten ORÖRD.
    //
    // `TextInput::setText` tar `std::string`, så raden
    // `input.setText(glaze::tr(1));` KOMPILERAR INTE — den bär användarens
    // data, inte en etikett. Previewn får därför inte utföra den.
    //
    // Ett fall tillbaka på `setProp` med den UPPLÖSTA texten
    // hade visat raden som fungerande, alltså gjort previewn MER än
    // C++-vägen — precis den asymmetri regel 8 förbjuder.
    // Det förväntade här är widgetens TILLSTÅND: texten ska vara den
    // som stod där före, inte "OK".
    //
    // SABOTAGE: återinför `if (w.setProp(field, t.str())) return true;` i
    // `applyTrText` ⇒ första assertionen faller med "OK".
    {
        glaze::TextInput ti;
        ti.setText("användarens text");
        bld::applyProp(ti, {"text", "glaze::tr(1)"}, "preview_gate");
        check(ti.text() == "användarens text",
              "tr: typ UTAN TrText-sättare lämnas ORÖRD (regel 8 — raden "
              "kompilerar inte i C++ heller)", ti.text());
        check(ti.text() != "OK",
              "tr: …och den upplösta texten smyger sig ALDRIG in via "
              "setProp-vägen", ti.text());
    }
    // Kroken är VIRTUELL, så en egen widget kan delta i regel 8 genom att
    // overrida den — det är skillnaden mot en dynamic_cast-kedja,
    // som bara kan känna igen ramverkets fem typer.
    {
        struct EgenText : glaze::Widget {
            glaze::TrText t;
            void draw(const glaze::Ctx&) override {}   // ritas aldrig här
            bool setTextToken(const std::string& key,
                              const glaze::TrText& v) override
            {
                if (key != "text") return false;
                t = v; return true;
            }
        };
        EgenText e;
        bld::applyProp(e, {"text", "glaze::tr(2)"}, "preview_gate");
        check(e.t.id() == 2,
              "tr: EGEN widget kan delta via setTextToken-overriden",
              std::to_string(e.t.id()));
    }
}

// ------------------------------------------------- MENYSTIGEN
//
// En klammerform får inte avkodas med `argStr`, som bara
// accepterar strängliteraler — det ger en PÅHITTAD TOM stig medan
// grenen ändå returnerar FRAMGÅNG. Grenen är strikt OCH atomisk.
void fallMenyStigPreview()
{
    glaze::setLanguage("sv");
    auto w = glaze::makeWidget("menubar");
    if (!w) {
        std::printf("  FEL  registret känner inte menubar\n");
        ++g_fails;
        ++g_cases;
        return;
    }
    auto* mb = static_cast<glaze::MenuBar*>(w.get());

    check(bld::applyCall(*w, {"addMenu", "\"Utseende\""}, "preview_gate"),
          "meny: addMenu utförs");
    check(bld::applyCall(*w, {"addSubmenu", "\"Utseende\", \"Läge\""},
                         "preview_gate"),
          "meny: addSubmenu utförs");
    check(bld::applyCall(*w,
                         {"addRadio", "{\"Utseende\", \"Läge\"}, \"System\""},
                         "preview_gate"),
          "meny: SEGMENTERAD stig utförs");
    check(mb->itemAtPath({"Utseende", "Läge", "System"}) != nullptr,
          "meny: posten hamnade på rätt nivå");

    // TR-TEXT i en menypost.
    check(bld::applyCall(*w, {"addItem", "\"Utseende\", glaze::tr(2)"},
                         "preview_gate"),
          "meny: tr-text i en menypost utförs");
    check(mb->itemAtPath({"Utseende", glaze::tr(2)}) != nullptr,
          "meny: tr-posten matchas på ID:t");

    // TRASIG STIG ⇒ menyn ORÖRD och `false` (INTE framgång).
    const int före = mb->menuAt(0) ? int(mb->menuAt(0)->items.size()) : -1;
    check(!bld::applyCall(*w, {"addRadio", "{\"Utseende\", namn}, \"X\""},
                          "preview_gate"),
          "meny: trasig stig returnerar INTE framgång");
    check(mb->menuAt(0) && int(mb->menuAt(0)->items.size()) == före,
          "meny: …och menyn är ORÖRD");
    check(!bld::applyCall(*w, {"addRadio", "\"Utseende/Läge\", \"X\""},
                          "preview_gate"),
          "meny: den gamla /-formen är OPAK (migreringen SYNS)");
    check(mb->menuAt(0) && int(mb->menuAt(0)->items.size()) == före,
          "meny: …och den lämnar också menyn orörd");
    check(!bld::applyCall(*w, {"addItem", "\"Utseende\", namn"},
                          "preview_gate"),
          "meny: oparsbar TEXT ⇒ ingen påhittad tom rubrik");
    check(mb->menuAt(0) && int(mb->menuAt(0)->items.size()) == före,
          "meny: …och menyn är fortfarande orörd");

    // MENYPOSTENS TILLSTÅND: de tre platta mutatorerna
    // UTFÖRS och syns i menyns data — samma tillstånd som trädformen.
    const glaze::MenuItem* sys = mb->itemAtPath({"Utseende", "Läge", "System"});
    check(bld::applyCall(*w,
                         {"setItemEnabled",
                          "{\"Utseende\", \"Läge\", \"System\"}, false"},
                         "preview_gate") &&
              bld::applyCall(*w,
                             {"setItemChecked",
                              "{\"Utseende\", \"Läge\", \"System\"}, true"},
                             "preview_gate") &&
              bld::applyCall(*w, {"setItemIcon", "{\"Utseende\", \"Läge\"}, {}"},
                             "preview_gate") &&
              sys && !sys->enabled && sys->checked,
          "meny: setItemEnabled/setItemChecked/setItemIcon utförs och syns i "
          "posten");
    check(!bld::applyCall(*w, {"setItemEnabled", "{\"Utseende\"}, kanske"},
                          "preview_gate") &&
              !bld::applyCall(*w, {"setItemChecked", "{}, true"},
                              "preview_gate") &&
              !bld::applyCall(*w,
                              {"setItemIcon",
                               "{\"Utseende\", \"Läge\"}, glaze::img(\"x\")"},
                              "preview_gate") &&
              sys && !sys->enabled && sys->checked,
          "meny: ogiltig flagga, {}-stigen (menyraden har ingen rot) och en "
          "trasig bild ⇒ opaka, posten orörd");
}

// -------------------------------------------- DESIGNTAGGENS `lang=`
//
// RITBORDETS fjärde axel. Driftvalet är appkod (`setLanguage` i `onLoad`,
// exakt som `setUI`) — en genererad `setLanguage("fi");` i
// `initializeComponent` vore per-fönster-språk bakvägen. Taggen behöver
// därför ingen ny satsform, bara en nyckel.
void fallDesignSprak()
{
    glaze::setLanguage("sv");
    bld::DesignTag t = bld::decodeDesignLine("lang=fi");
    check(t.hasLang && t.lang == "fi", "designtagg: lang= avkodas", t.lang);

    // `applyDesignTag` sätter det APP-GLOBALA språket sist i kedjan.
    // Fönstret behövs inte för just den axeln, men signaturen kräver ett.
    glaze::AppWindow w("preview_gate", 100, 100);
    bld::applyDesignTag(w, t);
    check(glaze::language() == "fi", "designtagg: lang appliceras",
          glaze::language());

    // ODEKODBAR tagg ⇒ logg + axeln OSATT (samma regel som `ui=`).
    bld::DesignTag u = bld::decodeDesignLine("lang=klingon");
    check(u.hasLang && u.lang == "klingon",
          "designtagg: avkodningen validerar INTE taggen — butiken gör det");
    bld::applyDesignTag(w, u);
    check(glaze::language() == "fi",
          "designtagg: okänt språk lämnar axeln OSATT (oförändrat)",
          glaze::language());

    // Okänd nyckel bevaras (regel 4) — mätt av roundtrip_gate; här mäts
    // bara att den inte SÄTTER något.
    bld::DesignTag v = bld::decodeDesignLine("ui=Slate");
    check(!v.hasLang, "designtagg: utan lang= rörs språket inte");
    glaze::setLanguage("sv");
}

// ------------------------------------- DEMONS ÖVERSÄTTNING
//
// SVEPET som håller demot samlat: demot ska ha all text som ändras —
// appen ska demonstrera vad som är möjligt — och ett demo glider
// isär i samma sekund någon lägger till en knapp med en literal.
//
// Grinden läser demons `.glazeui` med den RIKTIGA grammatiken
// (`bld::readWindow`) i stället för att grep:a text, så den mäter
// ARGUMENTPOSITIONER och inte tecken: `addItem(stig, TEXT, "Ctrl+N")`
// har en literal på tredje platsen med rätta — genvägen är KANONISK och
// får ALDRIG översättas.
//
// VAD SOM MEDVETET INTE KRÄVS TR, med skäl:
//   * `setItems` — items är kvar `std::string`, och
//     `glaze::tr(id).str()` är dessutom ett kompileringsfel på en
//     temporär. Demons översättbara listor fylls därför i KOD
//     (`StartWindow::syncSprak`), medan EGENNAMN (Glaze · Slate ·
//     Minimalistic), API-namn (Custom · OKCancel …) och TAL (0.8 · 1.0)
//     står kvar som literaler: de heter samma sak på varje språk.
//   * `name`-rader, `setUrl`, `setGroup`, `glaze::img` och genvägar — de
//     är IDENTITETER och inte UI-text.
void fallDemoOversatt()
{
    const char* kFiler[] = {"apps/demo/src/start_window.h",
                            "apps/demo/src/second_window.h",
                            "apps/demo/src/splash_window.h",
                            // Formuläret — rubrikerna, etiketterna,
                            // kryssrutorna och knapparna är tr-id.
                            "apps/demo/src/form_dialog.h"};
    for (const char* rel : kFiler) {
        const std::string    sökväg = std::string(GLAZE_TEST_ROOT) + "/" + rel;
        bld::ReadResult      r      = bld::readWindow(sökväg);
        check(r.ok, std::string("demo: ") + rel + " läses (" + r.error + ")");
        if (!r.ok)
            continue;

        std::string kvar;   // det som fortfarande är en naken literal
        auto        krävTr = [&](const std::string& var,
                          const std::string& värde) {
            std::uint32_t id = 0;
            if (!bld::parseTrCall(värde, id))
                kvar += " " + var + "=" + värde;
        };

        for (const bld::Stmt& s : r.model.stmts) {
            if (s.kind == bld::Stmt::Kind::WindowCall && s.name == "setTitle")
                krävTr("setTitle", s.value);
            if (s.kind == bld::Stmt::Kind::Prop &&
                (s.name == "text" || s.name == "title"))
                krävTr(s.target + "." + s.name, s.value);
            if (s.kind != bld::Stmt::Kind::Call)
                continue;
            // MENYRADENS platta form: STIGEN (arg 0) och TEXTEN (arg 1).
            // Stigen prövas segment för segment — en blandad stig matchar
            // ändå aldrig, men en HELT literal stig är precis det
            // svepet finns för.
            const bool menyText =
                s.name == "addItem" || s.name == "addCheck" ||
                s.name == "addRadio" || s.name == "addSubmenu";
            if (s.name != "addMenu" && !menyText && s.name != "addSeparator")
                continue;
            std::vector<std::string> a;
            if (!bld::splitArgs(s.value, a) || a.empty())
                continue;
            if (s.name == "addMenu") {
                krävTr("addMenu", a[0]);
                continue;
            }
            // Programmenyns rot `{}` är ingen literal.
            std::string rot = a[0];
            rot.erase(std::remove_if(rot.begin(), rot.end(),
                                     [](unsigned char ch) { return std::isspace(ch); }),
                      rot.end());
            std::vector<bld::PathSeg> segs;
            if (rot == "{}") {
                // ingenting att pröva
            } else if (!bld::parseMenuPath(a[0], segs)) {
                kvar += " " + s.name + "(stig=" + a[0] + ")";
            } else {
                for (const bld::PathSeg& seg : segs)
                    if (!seg.isId)
                        kvar += " " + s.name + "(segment=" + seg.text + ")";
            }
            if (menyText && a.size() > 1)
                krävTr(s.name + "-text", a[1]);
        }
        check(kvar.empty(),
              std::string("demo: ") + rel +
                  " bär INGEN naken UI-textliteral" +
                  (kvar.empty() ? "" : " — KVAR:" + kvar));
    }
}

// ------------------------------------- STAND-INENS MÄTENHET
//
// `StandInWidget` är den enda `contentSize()`-implementationen som ligger
// UTANFÖR libglaze, och därför den enda registerloopen i `measure_gate`
// omöjligt kan nå. Den är inte harmlös heller — den räknar ett verkligt
// minimimått, så ett glömt enhetsbyte hade gett en stand-in som är dubbelt
// så stor på en HiDPI-skärm och rätt på en vanlig. Grinden hör hemma här:
// det här är GlazeBuilder-vägens testprogram.
//
// Mätaren är samma FORM som `measure_gate` använder — en `Widget`-subklass
// som lyfter den statiska, skyddade `Widget::measure` till publik. Den når
// en godtycklig `Widget&` och bryr sig inte om `final`.
struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

bool nästan(float a, float b) { return (a > b ? a - b : b - a) < 0.01f; }

void fallStandInEnhet()
{
    // Ctx utan renderer och utan font: `StandInWidget::contentSize` läser
    // bara `c.scale` och sina egna fält (mätvägen får aldrig röra `c.box`).
    auto ctxFör = [](float scale) {
        glaze::Ctx c{};
        c.scale = scale;
        c.ui    = glaze::UI::Slate;
        c.style = &glaze::defaultStyle(glaze::UI::Slate);
        c.theme = &glaze::Theme::slateDefault().light;
        return c;
    };

    // AUTO: minimimåttet ur widgetens egna konstanter.
    {
        StandInWidget w("MinEgenWidget");
        const gfx::Sizef s1 = Meter::measure(w, ctxFör(1.0f));
        const gfx::Sizef s2 = Meter::measure(w, ctxFör(2.0f));
        // LOGISKA px ⇒ SKALOBEROENDE.
        check(s1.w > 0.0f && s1.h > 0.0f, "stand-in: AUTO ger ett mått",
              std::to_string(s1.w) + "x" + std::to_string(s1.h));
        check(nästan(s2.w, s1.w) && nästan(s2.h, s1.h),
              "stand-in: AUTO-måttet är skaloberoende",
              std::to_string(s2.w) + "x" + std::to_string(s2.h));
    }

    // SATT storlek: samma väg, andra grenen av axelregeln.
    {
        StandInWidget w("MinEgenWidget");
        w.width  = 210.0f;
        w.height = 55.0f;
        const gfx::Sizef s1 = Meter::measure(w, ctxFör(1.0f));
        const gfx::Sizef s2 = Meter::measure(w, ctxFör(2.0f));
        check(nästan(s2.w, s1.w) && nästan(s2.h, s1.h),
              "stand-in: SATT mått är skaloberoende",
              std::to_string(s2.w) + "x" + std::to_string(s2.h));
    }
}

// ============================== DE ICKE-VISUELLA MEDLEMMARNA
//
// Regel 8 för en komponent: `.glazeui` bär timern och systemfältsikonen som
// medlemmar, och previewn UTFÖR allt som bara lagrar — men aldrig det som
// hade gjort något utanför ritbordet (`start()`, `show()`).

// En widget i trädet efter nodnamn — `buildWindowTree` svarar per nodindex.
glaze::Widget* nodVidget(const bld::WindowModel& m,
                         const std::vector<glaze::Widget*>& perNod,
                         const std::string& id)
{
    for (std::size_t i = 0; i < m.nodes.size() && i < perNod.size(); ++i)
        if (m.nodes[i].id == id)
            return perNod[i];
    return nullptr;
}

void fallKomponenter()
{
    const std::string sökväg =
        std::string(GLAZE_TEST_ROOT) + "/tests/fixtures/components.h";
    bld::ReadResult r = bld::readWindow(sökväg);
    check(r.ok, "komponenter: fixturen läses", r.error);
    if (!r.ok)
        return;

    const std::vector<bld::PreviewComponent> komp =
        bld::buildComponents(r.model, "preview_gate");
    glaze::Timer*    t  = nullptr;
    glaze::TrayIcon* ti = nullptr;
    for (const bld::PreviewComponent& pc : komp) {
        if (auto* x = dynamic_cast<glaze::Timer*>(pc.object.get()))
            t = x;
        if (auto* x = dynamic_cast<glaze::TrayIcon*>(pc.object.get()))
            ti = x;
    }
    check(komp.size() == 2 && t && ti,
          "komponenter: timern och ikonen skapas ur registret",
          std::to_string(komp.size()));
    if (!t || !ti)
        return;

    check(t->name == "klocka" && t->interval() == 250 && t->isSingleShot(),
          "komponenter: timerns namn, intervall och en-gångs-läge utförs",
          t->name + " " + std::to_string(t->interval()));
    check(!t->isRunning(),
          "komponenter: start() utförs INTE — ingen timer tickar i previewn");

    check(ti->name == "ikon" && ti->interval() == 200 &&
              ti->isTemplateImage() && ti->frameCount() == 2,
          "komponenter: ikonens intervall, mallbild och två bilder utförs",
          std::to_string(ti->interval()) + " / " +
              std::to_string(ti->frameCount()));
    check(ti->toolTipToken().id() == 1,
          "komponenter: tooltipen bär TOKENET (setTextToken), inte texten",
          std::to_string(ti->toolTipToken().id()));
    glaze::MenuItem* visa = ti->itemAtPath({"Visa"});
    glaze::MenuItem* läge = ti->itemAtPath({"Läge"});
    glaze::MenuItem* tyst = ti->itemAtPath({"Läge", "Tyst"});
    check(visa && läge && tyst && tyst->radio && tyst->checked,
          "komponenter: menyn med {}-roten — post, undermeny och radiopost");
    check(!ti->isVisible(),
          "komponenter: show() utförs INTE — ingen ikon i OS:ets fält");
    check(!ti->isAnimating(), "komponenter: ingen animation begärd");

    // Fönstret och widgetarna — samma modell genom trädbyggaren.
    glaze::AppWindow w("preview_gate", 100, 100, gfx::Chrome::Custom);
    const std::vector<glaze::Widget*> perNod =
        bld::buildWindowTree(w, r.model, "preview_gate");
    glaze::Widget* status = nodVidget(r.model, perNod, "status");
    glaze::Widget* knapp  = nodVidget(r.model, perNod, "knapp");
    check(status && status->cursor() == gfx::PointerShape::Wait,
          "komponenter: setCursor(Wait) utförs på widgeten");
    check(knapp && !knapp->cursor().has_value(),
          "komponenter: setCursor följt av resetCursor() ⇒ ingen satt form");
    check(nodVidget(r.model, perNod, "klocka") == nullptr &&
              nodVidget(r.model, perNod, "ikon") == nullptr,
          "komponenter: trädbyggaren skapar ingen widget för en komponent");
    check(w.topBar() == nullptr && w.chrome() == gfx::Chrome::None,
          "komponenter: setChrome(None) ⇒ ingen namnlist");
    check(w.startPosition() == glaze::StartPosition::CenterOwner,
          "komponenter: setStartPosition(CenterOwner) efter setLocation");
    check(w.windowState() == gfx::WindowState::Maximized,
          "komponenter: setWindowState(Maximized) lagras som begäran");
    check(w.useWaitCursor(), "komponenter: setUseWaitCursor(true) lagras");
    const glaze::WindowGeometry g = w.geometry();
    check(g.hasPosition && g.x == 120 && g.y == 80 && g.width == 360 &&
              g.height == 240,
          "komponenter: geometry() före öppning är begäran (plats och storlek)",
          std::to_string(g.x) + "," + std::to_string(g.y));
}

// EGNA KOMPONENTER för previewkontraktet. Namnen ligger
// utanför `glaze::` med flit — komponentoraklet räknar bara ramverkets.
int g_inertaSkapade   = 0;
int g_standInSkapade  = 0;

struct TestInert : glaze::Component {
    TestInert() { ++g_inertaSkapade; }
    std::vector<std::string> rader;
    bool setProp(const std::string& k, const std::string& v) override
    {
        if (k != "fart")
            return false;
        rader.push_back(k + "=" + v);
        return true;
    }
};

struct TestStandIn : glaze::Component {
    TestStandIn() { ++g_standInSkapade; }
};

GLAZE_REGISTER_COMPONENT("TestInert", TestInert, ::glaze::PreviewForm::Inert)
GLAZE_REGISTER_COMPONENT("TestStandIn", TestStandIn,
                         ::glaze::PreviewForm::StandIn)

void fallEgnaKomponenter()
{
    bld::WindowModel m;
    m.className = "F";
    m.appendNode("TestInert", "inert");
    m.appendNode("TestStandIn", "halv");
    m.appendNode("OkandTyp", "okand");
    m.setProp("inert", "name", "\"inert\"");
    m.setProp("inert", "fart", "3");
    m.setProp("halv", "fart", "4");
    m.setProp("okand", "fart", "5");

    const int före = g_standInSkapade;
    const std::vector<bld::PreviewComponent> komp =
        bld::buildComponents(m, "preview_gate");
    check(komp.size() == 1 && komp[0].node == 0,
          "egna: bara den Inert-registrerade skapas",
          std::to_string(komp.size()));
    auto* in = komp.empty() ? nullptr
                            : dynamic_cast<TestInert*>(komp[0].object.get());
    check(in && in->name == "inert" &&
              in->rader == std::vector<std::string>{"fart=3"},
          "egna: den inerta får sitt namn och SINA rader — inga andras");
    check(g_standInSkapade == före,
          "egna: en StandIn-registrerad komponent skapas ALDRIG (satserna "
          "bevaras opakt i modellen)");
    check(m.findProp("halv", "fart") && m.findProp("okand", "fart"),
          "egna: StandIn- och den okända typens rader står kvar i modellen");
}

// ===================================================== EGNA WIDGETAR
//
// Verktygen skapar en modellnod ur REGISTRET med nodens klassnamn
// (`bld::makeNodeWidget`) — paletten är bara verktygslådans metadata.
// Slog trädbyggarna upp paletten skulle en registrerad EGEN widget
// bli en stand-in också i ett bygge som länkade den. Grinden registrerar en
// egen widget här, i en egen namnrymd, så som en app gör.
//
// SABOTAGE: (1) `makeNodeWidget` palett-först (`paletteByCpp` → `makeWidget`)
// ⇒ Mätaren blir en stand-in och E2/E4 faller; (2) `klassnamn` i
// gui_loader.cpp utan avmangling (rå `type_info::name()`) ⇒ E1/E2 faller, och
// register ↔ palett-kontrollen i fallSetterMatris med dem.
namespace pgate {

// GRINDENS EGEN WIDGET — ett värde med en sättare och en `setProp`-nyckel,
// mallen i MANUAL `custom-widget`. Ritas aldrig här.
class Matare : public glaze::Widget {
public:
    void  setValue(float v) { value_ = v; }
    float value() const { return value_; }
    bool  setProp(const std::string& key, const std::string& value) override
    {
        if (key == "value") {
            value_ = std::strtof(value.c_str(), nullptr);
            return true;
        }
        return glaze::Widget::setProp(key, value);
    }

protected:
    void draw(const glaze::Ctx&) override {}

private:
    float value_ = 0.0f;
};

GLAZE_REGISTER_WIDGET("preview_gate_matare", Matare)

} // namespace pgate

void fallEgnaWidgetar()
{
    // E1. KLASSEN BAKOM TYPNAMNET — avmanglad, med namnrymden.
    check(glaze::widgetClassOf("button") == "glaze::Button",
          "E1 klass: 'button' är glaze::Button", glaze::widgetClassOf("button"));
    check(glaze::widgetClassOf("preview_gate_matare") == "pgate::Matare",
          "E1 klass: grindens egen är pgate::Matare (namnrymden med)",
          glaze::widgetClassOf("preview_gate_matare"));
    check(glaze::widgetClassOf("finns_inte").empty(),
          "E1 klass: ett okänt typnamn ger tom sträng");

    // E2. KLASSUPPSLAGET — exakt, aldrig en gissning på svansen.
    {
        auto b = glaze::makeWidgetOfClass("glaze::Button");
        check(b && dynamic_cast<glaze::Button*>(b.get()),
              "E2 makeWidgetOfClass: glaze::Button ger en Button");
        check(!glaze::makeWidgetOfClass("Button") &&
                  !glaze::makeWidgetOfClass("glaze::button") &&
                  !glaze::makeWidgetOfClass(""),
              "E2 makeWidgetOfClass: okvalificerat, fel skiftläge och tomt ger "
              "ingenting");
        bool standIn = true;
        auto m       = bld::makeNodeWidget("pgate::Matare", &standIn);
        check(!standIn && dynamic_cast<pgate::Matare*>(m.get()),
              "E2 makeNodeWidget: den REGISTRERADE egna widgeten skapas på "
              "riktigt, inte som stand-in");
        auto o = bld::makeNodeWidget("Matare", &standIn);
        check(standIn && dynamic_cast<StandInWidget*>(o.get()),
              "E2 makeNodeWidget: det okvalificerade namnet blir en stand-in");
        auto u  = bld::makeNodeWidget("app::Okand", &standIn);
        auto* s = dynamic_cast<StandInWidget*>(u.get());
        check(standIn && s && s->typeName() == "app::Okand",
              "E2 makeNodeWidget: en okänd klass blir en stand-in med sitt namn");
    }

    // E3. RAMVERKETS TYPER skapas genom samma väg (paletten frågas inte).
    for (std::size_t i = 0; i < bld::kPaletteCount; ++i) {
        bool standIn = true;
        auto w       = bld::makeNodeWidget(bld::kPalette[i].cppType, &standIn);
        if (standIn || !w) {
            check(false, std::string("E3 makeNodeWidget: ") +
                             bld::kPalette[i].cppType + " ur registret");
            return;
        }
    }
    check(true, "E3 makeNodeWidget: varje palettrad skapas ur registret");

    // E4. HELA VÄGEN genom trädbyggaren (glaze-render och pixelgrinden): en
    // medlem `pgate::Matare level;` med `level.setValue(0.4f);` — en ANROPSSATS,
    // för typen är ingen glaze-typ — och en okänd typ bredvid.
    {
        bld::WindowModel m;
        m.className = "F";
        m.appendNode("pgate::Matare", "level");
        m.appendNode("app::Okand", "okand");
        m.setAdoption("level", "");
        m.setAdoption("okand", "");
        m.setProp("level", "name", "\"level\"");
        m.appendCall("level", "setValue", "0.4f");
        glaze::AppWindow w("preview_gate", 100, 100, gfx::Chrome::Custom);
        const std::vector<glaze::Widget*> perNod =
            bld::buildWindowTree(w, m, "preview_gate");
        auto* lv = dynamic_cast<pgate::Matare*>(nodVidget(m, perNod, "level"));
        check(lv && std::abs(lv->value() - 0.4f) < 1e-6f,
              "E4 trädet: den egna widgeten byggs på riktigt och dess "
              "setValue(0.4f) utförs (setter-fallbacken)",
              lv ? std::to_string(lv->value()) : std::string("(stand-in)"));
        check(dynamic_cast<StandInWidget*>(nodVidget(m, perNod, "okand")) !=
                  nullptr,
              "E4 trädet: en okänd klass är en stand-in");
    }
}

// ===================================================== RADIOGRUPPEN
//
// Samma satser ska ge samma läge i APPENS ordning — den kompilerade
// `.glazeui` kör sättarna FÖRE trädbygget (adopt står sist) — och i
// PREVIEWNS — trädet först, därefter satserna genom `bld::applyProp`
// (window_tree, rebuildPreview). Det förväntade är skrivet ur regeln
// (gruppen visar den flaggade som sattes SIST), inte ur radio.cpp.
//
// SABOTAGE: (1) `isChecked()` som läser flaggan rakt ⇒ R1 faller i appens
// ordning (två ikryssade); (2) `setChecked(true)` utan ny stämpel när flaggan
// redan står ⇒ R3 faller i previewns ordning; (3) en sättare som kryssar ur
// syskonen i stället för stämpeln ⇒ R2 och R4 faller i previewns ordning;
// (4) `select()` som inte släcker syskonens flaggor ⇒ R5 faller; (5)
// `select()` som återvänder FÖRE släckningen när radion redan visas
// ⇒ R7 faller (A kommer tillbaka); (6) `select()` som fyrar också
// när det visade valet är oförändrat ⇒ R7 faller (händelserna räknas).
namespace radio_gate {

struct Sats {
    int         radio;   // 0 = A, 1 = B
    const char* fält;    // "checked" eller "group"
    const char* värde;   // råa C++-uttryckstexten, som i modellen
};

// En radio vars användarval grinden kan trycka på (klicket och tangenten är
// skyddade).
struct Tryckbar : glaze::Radio {
    using glaze::Radio::clicked;
    using glaze::Radio::keyDown;
};

void sätt(glaze::Radio& r, const Sats& s)
{
    // APPENS väg: den kompilerade sättaren.
    std::string v;
    if (std::string(s.fält) == "checked")
        r.setChecked(std::string(s.värde) == "true");
    else if (bld::parseCppString(s.värde, v))
        r.setGroup(v);
}

// Läget efter satserna i appens ordning (sättarna, därefter adopt i `adoptOrdning`)
// och i previewns (adopt, därefter `applyProp`).
std::vector<bool> app(const std::vector<Sats>& satser, bool omvänd)
{
    glaze::Panel             p;
    glaze::Radio             r[2];
    for (const Sats& s : satser)
        sätt(r[std::size_t(s.radio)], s);
    if (omvänd) {
        p.adopt(r[1]);
        p.adopt(r[0]);
    } else {
        p.adopt(r[0]);
        p.adopt(r[1]);
    }
    return {r[0].isChecked(), r[1].isChecked()};
}

std::vector<bool> preview(const std::vector<Sats>& satser)
{
    glaze::Panel             p;
    glaze::Radio             r[2];
    p.adopt(r[0]);
    p.adopt(r[1]);
    for (const Sats& s : satser)
        bld::applyProp(r[std::size_t(s.radio)], {s.fält, s.värde}, "preview_gate");
    return {r[0].isChecked(), r[1].isChecked()};
}

std::string text(const std::vector<bool>& v)
{
    return std::string(v[0] ? "A" : "-") + (v[1] ? "B" : "-");
}

} // namespace radio_gate

void fallRadioEnval()
{
    using radio_gate::Sats;
    struct Fall {
        const char*       namn;
        std::vector<Sats> satser;
        std::vector<bool> facit;
    };
    const std::vector<Fall> fall = {
        {"R1 två setChecked(true) i samma grupp — den SENASTE syns",
         {{0, "checked", "true"}, {1, "checked", "true"}},
         {false, true}},
        {"R2 true, true, false — den förra syns igen (dess flagga står kvar)",
         {{0, "checked", "true"}, {1, "checked", "true"}, {1, "checked", "false"}},
         {true, false}},
        {"R3 samma radio två gånger med ett syskon emellan — den syns",
         {{0, "checked", "true"}, {1, "checked", "true"}, {0, "checked", "true"}},
         {true, false}},
        {"R4 grupperna sätts EFTER kryssen — två grupper, båda syns",
         {{1, "checked", "true"},
          {0, "checked", "true"},
          {0, "group", "\"a\""},
          {1, "group", "\"b\""}},
         {true, true}},
    };
    for (const Fall& f : fall) {
        const std::vector<bool> a  = radio_gate::app(f.satser, false);
        const std::vector<bool> ao = radio_gate::app(f.satser, true);
        const std::vector<bool> p  = radio_gate::preview(f.satser);
        check(a == f.facit && ao == f.facit && p == f.facit,
              std::string(f.namn) + " — appens ordning, omvänd adopt och previewn",
              "app " + radio_gate::text(a) + ", omvänd " + radio_gate::text(ao) +
                  ", preview " + radio_gate::text(p) + ", facit " +
                  radio_gate::text(f.facit));
    }

    // R5. ANVÄNDARENS VAL släcker syskonens flaggor: efter ett klick på A tar
    // `setChecked(false)` på A INTE fram B igen.
    {
        glaze::Panel           p;
        radio_gate::Tryckbar   a, b;
        p.adopt(a);
        p.adopt(b);
        a.setChecked(true);
        b.setChecked(true);
        const bool före = !a.isChecked() && b.isChecked();
        a.clicked();
        const bool vald = a.isChecked() && !b.isChecked();
        a.setChecked(false);
        check(före && vald && !a.isChecked() && !b.isChecked(),
              "R5 användarvalet släcker syskonen — ingen kommer tillbaka",
              std::string(a.isChecked() ? "A" : "-") + (b.isChecked() ? "B" : "-"));
    }

    // R7. Användaren väljer den radio som REDAN visas: A och B
    // satta, B syns; ett klick på B — och Space på B — släcker ändå A:s
    // flagga, så `B.setChecked(false)` inte tar fram A. Det visade valet var
    // oförändrat, så varken onChange eller onCommit fyras.
    for (const bool medSpace : {false, true}) {
        glaze::Panel         p;
        radio_gate::Tryckbar a, b;
        p.adopt(a);
        p.adopt(b);
        int händelser = 0;
        b.onChange    = [&händelser] { ++händelser; };
        b.onCommit    = [&händelser] { ++händelser; };
        a.setChecked(true);
        b.setChecked(true);
        const bool före = !a.isChecked() && b.isChecked();
        if (medSpace)
            (void)b.keyDown(gfx::Key::Space, 0);
        else
            b.clicked();
        const bool vald = !a.isChecked() && b.isChecked();
        b.setChecked(false);
        check(före && vald && händelser == 0 && !a.isChecked() && !b.isChecked(),
              std::string("R7 användarvalet av den VISADE radion (") +
                  (medSpace ? "Space" : "klick") +
                  ") släcker syskonen utan händelser — A kommer inte tillbaka",
              std::string(a.isChecked() ? "A" : "-") + (b.isChecked() ? "B" : "-") +
                  ", händelser " + std::to_string(händelser));
    }

    // R6. HELA VÄGEN genom trädbyggaren, buggens egen form: två
    // `setChecked(true)` i samma grupp i en modell ger EN prick — B.
    {
        bld::WindowModel m;
        m.className = "F";
        m.appendNode("glaze::Radio", "radA");
        m.appendNode("glaze::Radio", "radB");
        m.setAdoption("radA", "");
        m.setAdoption("radB", "");
        m.setProp("radA", "checked", "true");
        m.setProp("radB", "checked", "true");
        glaze::AppWindow w("preview_gate", 100, 100, gfx::Chrome::Custom);
        const std::vector<glaze::Widget*> perNod =
            bld::buildWindowTree(w, m, "preview_gate");
        auto* a = dynamic_cast<glaze::Radio*>(nodVidget(m, perNod, "radA"));
        auto* b = dynamic_cast<glaze::Radio*>(nodVidget(m, perNod, "radB"));
        check(a && b && !a->isChecked() && b->isChecked(),
              "R6 trädet: två setChecked(true) ⇒ bara den senare syns");
    }
}

// FÖNSTRETS NYA ANROP — strikta tabeller,
// kvalificerade eller bara tokens, och opakt för ett okänt värde.
void fallFonsterRam()
{
    glaze::AppWindow w("preview_gate", 400, 300, gfx::Chrome::Custom);
    check(w.topBar() != nullptr, "ram: (fixturen) Custom har en namnlist");
    check(bld::applyWindowCall(w, {"setChrome", "gfx::Chrome::None"},
                               "preview_gate") &&
              w.topBar() == nullptr && w.chrome() == gfx::Chrome::None,
          "ram: setChrome(gfx::Chrome::None) ⇒ ingen namnlist");
    check(bld::applyWindowCall(w, {"setChrome", "Custom"}, "preview_gate") &&
              w.topBar() != nullptr,
          "ram: den bara token Custom godtas och tar tillbaka namnlisten");
    check(!bld::applyWindowCall(w, {"setChrome", "gfx::Chrome::Glas"},
                                "preview_gate") &&
              w.chrome() == gfx::Chrome::Custom,
          "ram: ett okänt värde är opakt och ändrar ingenting");

    check(bld::applyWindowCall(w,
                               {"setStartPosition",
                                "glaze::StartPosition::CenterScreen"},
                               "preview_gate") &&
              w.startPosition() == glaze::StartPosition::CenterScreen,
          "ram: setStartPosition(CenterScreen)");
    check(bld::applyWindowCall(w, {"setLocation", "40, 30"}, "preview_gate") &&
              w.startPosition() == glaze::StartPosition::Manual &&
              w.geometry().x == 40 && w.geometry().y == 30 &&
              w.geometry().hasPosition,
          "ram: setLocation före öppning ⇒ Manual och platsen i geometry()");
    check(bld::applyWindowCall(w, {"setWindowState", "Minimized"},
                               "preview_gate") &&
              w.windowState() == gfx::WindowState::Minimized,
          "ram: setWindowState med bar token");
    check(bld::applyWindowCall(w, {"setUseWaitCursor", "true"},
                               "preview_gate") &&
              w.useWaitCursor(),
          "ram: setUseWaitCursor(true)");

    auto b = build("glaze::Button", {{"cursor", "gfx::PointerShape::Hand"}});
    if (!b)
        return;
    check(b->cursor() == gfx::PointerShape::Hand,
          "ram: cursor-fältet (setCursor) når widgeten");
    check(bld::applyCall(*b, {"resetCursor", ""}, "preview_gate") &&
              !b->cursor().has_value(),
          "ram: resetCursor() utförs");
}

// ==================================================== SETTER-TYPMATRISEN
//
// Grinden ligger HÄR och inte i `roundtrip_gate` av
// ett tekniskt skäl: typdomänen kommer ur `registeredWidgetTypes()` (i
// **glaze**) och `kPalette` (i **builder_preview**), medan roundtrip_gate
// avsiktligt länkar BARA `builder_core` och är ren std. Att lägga den där
// hade krävt antingen en duplicerad typdomän — vilket motverkar hela
// registergrinden — eller ett glaze-beroende som bryter lagergränsen.
//
// ⚠ GRINDEN HAR TVÅ NIVÅER, och den andra är hela poängen. Låter man
// det förväntade HÄRLEDAS ur samma tabell implementationen läser blir kontrollen
// CIRKULÄR: en felklassning blir då både implementation och förväntat
// resultat, och testet blir grönt. Exakt två sådana fel (`img` bara på
// Button, `text` som generisk) kan slippa igenom ett fåtal exempelfall.
//
//   1. FULLSTÄNDIGHET — produktionens `setterFieldSpecs()` itereras, så
//      extra eller saknade poster upptäcks.
//   2. ORAKEL — den OBEROENDE matrisen nedan, skriven för hand, och
//      jämförd EXAKT mot produktionsspecarna.
//   3. Därefter typdomänens KARTESISKA produkt mot predikaten.
//
// Dupliceringen är AVSIKTLIG KRAVDATA, inte en andra produktionssanning.
// Glider de isär blir testet rött — det är hela poängen. Att i stället
// dela en header mellan test och produktion hade gjort felklassningen
// GEMENSAM och därmed osynlig.

struct Vantad {
    const char* cppType;
    const char* field;
    bool        subtypes;
};

const Vantad kVantadMatris[] = {
    {"glaze::Button",        "text",          false},
    {"glaze::Checkbox",      "text",          false},
    {"glaze::Radio",         "text",          false},
    {"glaze::Label",         "text",          false},
    {"glaze::Link",          "text",          false},
    {"glaze::TextInput",     "text",          false},
    {"glaze::ToolButton",    "text",          false},
    {"glaze::GroupBox",      "text",          false},
    {"glaze::Button",        "img",           false},
    {"glaze::Picture",       "img",           false},
    {"glaze::ToolButton",    "img",           false},
    {"glaze::Button",        "imagePos",      false},
    {"glaze::ToolButton",    "imagePos",      false},
    // DIALOGKNAPPEN: ett enum-värde — EXAKT Button.
    // Fönstrets `setAcceptButton(ok)` är ett fönsteranrop (fallDialogerOchFalt).
    {"glaze::Button",        "dialogResult",  false},
    {"glaze::Checkbox",      "checked",       false},
    // KRYSSRUTANS TRE LÄGEN — exakta.
    {"glaze::Checkbox",      "threeState",    false},
    {"glaze::Checkbox",      "checkState",    false},
    {"glaze::Radio",         "checked",       false},
    {"glaze::ToolButton",    "checked",       false},
    {"glaze::ToolButton",    "checkable",     false},
    // DEN DELADE KNAPPEN: ett bool-argument — EXAKT typen.
    // Rullgardinens poster är Call-satser (fallKnappmeny).
    {"glaze::ToolButton",    "split",         false},
    // VERKTYGSFÄLTETS BAND — exakt.
    {"glaze::ToolBar",       "band",          false},
    // VERKTYGSFÄLTET: titeln, det flytande läget och flytbarheten — exakt.
    // Platsen (två tal) är en Call-sats (fallVerktygsfaltFlytande).
    {"glaze::ToolBar",       "text",          false},
    {"glaze::ToolBar",       "floating",      false},
    {"glaze::ToolBar",       "floatable",     false},
    // ÖVERFLÖDET — exakt (en bool).
    {"glaze::ToolBar",       "overflow",      false},
    {"glaze::Label",         "role",          false},
    {"glaze::Label",         "wrap",          false},
    {"glaze::Label",         "align",         false},
    {"glaze::TextInput",     "align",         false},
    {"glaze::TextInput",     "readOnly",      false},
    // LÖSENORDET — EXAKT TextInput: TextArea har inget.
    {"glaze::TextInput",     "password",      false},
    // PLATSHÅLLAREN OCH MAXLÄNGDEN — på BÅDA fälten (TextArea
    // nedan), exakta: kompositerna vidarebefordrar dem inte.
    {"glaze::TextInput",     "placeholder",   false},
    {"glaze::TextInput",     "maxLength",     false},
    {"glaze::Dropdown",      "items",         false},
    {"glaze::ListBox",       "items",         false},
    // Dropdownens justering — samma namn som Labels och
    // TextInputs, EXAKT typen: ListBox och popupens rader har ingen.
    {"glaze::Dropdown",      "align",         false},
    {"glaze::Dropdown",      "selectedIndex", false},
    {"glaze::ListBox",       "selectedIndex", false},
    {"glaze::ListBox",       "showChecks",    false},
    // LISTBOXENS FLERVAL — exakt, listvyns bool.
    {"glaze::ListBox",       "multiSelect",   false},
    // TRÄDET: valet är en STIG. Kollektionsanropen är Call-satser.
    // Flervalet och kryssens synlighet är ETT bool-
    // argument — setter-fält, som listvyns `multiSelect` och ListBoxens
    // `showChecks`; `setChecked(stig, på)`/`setSelected`/`setNodeColor` tar
    // två argument och är Call-satser.
    {"glaze::TreeView",      "selectedPath",  false},
    {"glaze::TreeView",      "multiSelect",   false},
    {"glaze::TreeView",      "showChecks",    false},
    // TRÄDETS KOLUMNER: ColumnPanels sortering, huvud och
    // fyllkolumn — exakta som listvyns, för basen registreras inte.
    {"glaze::TreeView",      "sortColumn",    false},
    {"glaze::TreeView",      "sortAscending", false},
    {"glaze::TreeView",      "sortable",      false},
    {"glaze::TreeView",      "headerLines",   false},
    {"glaze::TreeView",      "headerVAlign",  false},
    {"glaze::TreeView",      "fillColumn",    false},
    {"glaze::Slider",        "min",           false},
    {"glaze::Scrollbar",     "min",           false},
    {"glaze::Progress",      "min",           false},
    {"glaze::SpinBox",       "min",           false},
    {"glaze::Slider",        "max",           false},
    {"glaze::Scrollbar",     "max",           false},
    {"glaze::Progress",      "max",           false},
    {"glaze::SpinBox",       "max",           false},
    {"glaze::Slider",        "value",         false},
    {"glaze::Scrollbar",     "value",         false},
    {"glaze::Progress",      "value",         false},
    {"glaze::SpinBox",       "value",         false},
    {"glaze::Slider",        "step",          false},
    {"glaze::SpinBox",       "step",          false},
    {"glaze::SpinBox",       "decimals",      false},
    {"glaze::Slider",        "vertical",      false},
    {"glaze::Separator",     "vertical",      false},
    {"glaze::Scrollbar",     "orientation",   false},
    {"glaze::Scrollbar",     "page",          false},
    {"glaze::Radio",         "group",         false},
    {"glaze::Link",          "url",           false},
    {"glaze::Picture",       "fit",           false},
    {"glaze::Panel",         "clip",          true},
    {"glaze::Panel",         "border",        true},
    {"glaze::Spacer",        "grow",          false},
    {"glaze::CaptionButton", "kind",          false},
    {"glaze::CaptionButton", "shape",         false},
    {"glaze::CaptionButton", "restoreGlyph",  false},
    {"glaze::PropertyGrid",  "nameWidth",     false},
    // LISTVYN: valet, flervalet och sorteringen — exakta, för
    // basen `Table` registreras inte och står inte i paletten. Kryssens
    // synlighet likaså.
    {"glaze::ListView",      "selectedIndex", false},
    {"glaze::ListView",      "multiSelect",   false},
    {"glaze::ListView",      "showChecks",    false},
    {"glaze::ListView",      "sortColumn",    false},
    {"glaze::ListView",      "sortAscending", false},
    {"glaze::ListView",      "sortable",      false},
    // KOLUMNHUVUDET: radantalet och justeringen i höjdled —
    // Tables, alltså på BÅDA ättlingarna och exakta som sorteringen.
    {"glaze::ListView",      "headerLines",   false},
    {"glaze::ListView",      "headerVAlign",  false},
    // DATAGRIDDEN: sorteringen och huvudet — kolumntypen och den
    // aktuella cellen är anrop med två argument.
    {"glaze::DataGrid",      "sortColumn",    false},
    {"glaze::DataGrid",      "sortAscending", false},
    {"glaze::DataGrid",      "sortable",      false},
    {"glaze::DataGrid",      "headerLines",   false},
    {"glaze::DataGrid",      "headerVAlign",  false},
    // DATAGRIDDENS RADVAL — läget och flervalet, exakta.
    {"glaze::DataGrid",      "selectionMode", false},
    {"glaze::DataGrid",      "multiSelect",   false},
    // DELAREN: fem egna fält, alla exakta — delaren är ingen Panel.
    {"glaze::Splitter",      "vertical",      false},
    {"glaze::Splitter",      "fixedPane",     false},
    {"glaze::Splitter",      "paneSize",      false},
    {"glaze::Splitter",      "firstMin",      false},
    {"glaze::Splitter",      "secondMin",     false},
    // DOCKNINGEN: panelens rubrik och fällning, värdens layout.
    // Stängknappen (`closable`, flikkontrollens ord) och ikonen
    // (`img`, Buttons) — exakta. Knappnålen (`pinned`) — exakt.
    // Det flytande läget (`floating`, `floatable`) — exakta.
    {"glaze::DockPanel",     "text",          false},
    {"glaze::DockPanel",     "collapsed",     false},
    {"glaze::DockPanel",     "pinned",        false},
    {"glaze::DockPanel",     "closable",      false},
    {"glaze::DockPanel",     "floating",      false},
    {"glaze::DockPanel",     "floatable",     false},
    {"glaze::DockPanel",     "img",           false},
    {"glaze::DockHost",      "layout",        false},
    // FLIKARNA: sidans rubrik, flikkontrollens val och dess
    // stängknappar — alla exakta. Sidan ärver Panels clip/border (oraklet
    // nedan), flikkontrollen gör det inte.
    {"glaze::TabPage",       "text",          false},
    {"glaze::TabControl",    "selectedIndex", false},
    {"glaze::TabControl",    "closable",      false},
    // FILVÄLJARFÄLTET: sökvägen och läget — exakta. Filtren är en
    // lista och därmed Call-satser (fallFilvaljarfalt nedan).
    {"glaze::FilePicker",    "path",          false},
    {"glaze::FilePicker",    "mode",          false},
    // FÄRGVÄLJAREN: färgen som hextext — EXAKT typen. Labels
    // `setColor(gfx::Color{…})` är en Call och ska förbli det.
    {"glaze::ColorPicker",   "color",         false},
    // ALFAN och KLOCKSLAGET — två bools, exakta.
    {"glaze::ColorPicker",   "allowAlpha",    false},
    {"glaze::DatePicker",    "showTime",      false},
    // DET FLERRADIGA FÄLTET: texten, skyddet och brytningen —
    // exakta. `wrap` delar namn med Labels (regel 6), och fältet ärver
    // Panels clip/border (oraklet nedan).
    {"glaze::TextArea",      "text",          false},
    {"glaze::TextArea",      "readOnly",      false},
    {"glaze::TextArea",      "wrap",          false},
    {"glaze::TextArea",      "placeholder",   false},
    {"glaze::TextArea",      "maxLength",     false},
    // MARKDOWN-VYN: dokumentet — exakt. Bilderna
    // och ankaret är Call-satser (fallMarkdown), och vyn ärver Panels
    // clip/border (oraklet nedan).
    {"glaze::MarkdownView",  "text",          false},
    // DEN FLYTTBARA PANELEN: rubriken och stängknappen — exakt.
    // Den ärver Panels clip/border (oraklet nedan).
    {"glaze::FloatingPanel", "text",          false},
    {"glaze::FloatingPanel", "closable",      false},
    // TERMINALEN: åtta exakta — programmet, utseendet och
    // historiken. Miljön och de egna färgerna är Call-satser (fallTerminal),
    // och terminalen ärver Panels clip/border (oraklet nedan).
    {"glaze::Terminal",      "program",          false},
    {"glaze::Terminal",      "arguments",        false},
    {"glaze::Terminal",      "workingDirectory", false},
    {"glaze::Terminal",      "colorMode",        false},
    {"glaze::Terminal",      "colorScheme",      false},
    {"glaze::Terminal",      "scrollback",       false},
    {"glaze::Terminal",      "altAsMeta",        false},
    {"glaze::Terminal",      "textSize",         false},
    // RITYTAN: tretton exakta — ritytan är ingen
    // Panel, så `border` är dess egen; gesternas fem flaggor och
    // etiketternas toning är bools med `setProp`-nycklar. Färgen, de
    // tvåargumentiga, kameran, lagren och formlistan är Call-satser
    // (fallCanvas nedan).
    {"glaze::Canvas",        "gridSize",      false},
    {"glaze::Canvas",        "border",        false},
    {"glaze::Canvas",        "yUp",           false},
    {"glaze::Canvas",        "zoom",          false},
    {"glaze::Canvas",        "bearing",       false},
    {"glaze::Canvas",        "pitch",         false},
    {"glaze::Canvas",        "maxPitch",      false},
    {"glaze::Canvas",        "pannable",      false},
    {"glaze::Canvas",        "zoomable",      false},
    {"glaze::Canvas",        "rotatable",     false},
    {"glaze::Canvas",        "pitchable",     false},
    {"glaze::Canvas",        "inertia",       false},
    {"glaze::Canvas",        "labelFade",     false},
    // VRIDSNÄPPET: steget och toleransen — exakta, ett tal var.
    {"glaze::Canvas",        "rotationSnap",  false},
    {"glaze::Canvas",        "rotationSnapTolerance", false},
    // I RYMDEN: provtagningen, djupet och
    // orbitens tumling — exakta, ett värde var. Kameran, ljuset, vyerna,
    // lagrens renderlägen, spannen och tickningen är Call-satser (fall3D).
    {"glaze::Canvas",        "multisample",   false},
    {"glaze::Canvas",        "depth",         false},
    {"glaze::Canvas",        "orbitTumble",   false},
    // BINDNINGARNAS LÄGE — en strängliteral, exakt.
    {"glaze::Canvas",        "inputMode",     false},
    // MINNET: ritytans minnesbudget i byte — exakt.
    {"glaze::Canvas",        "memoryBudget",  false},
    // PROGRAMIKONEN: de två bilderna och de två
    // bools — exakta. Programmenyn är Call-satser (fallProgramikon).
    {"glaze::AppIcon",       "icon",          false},
    {"glaze::AppIcon",       "smallIcon",     false},
    {"glaze::AppIcon",       "large",         false},
    {"glaze::AppIcon",       "noIcon",        false},
    // DRAG-AND-DROP: basens egenskaper — VARJE inbyggd typ.
    {"glaze::Widget",        "allowDrag",     true},
    {"glaze::Widget",        "allowDrop",     true},
    // TOOLTIPEN: basens textfält — VARJE inbyggd typ.
    {"glaze::Widget",        "toolTip",       true},
    // DE ICKE-VISUELLA MEDLEMMARNA —
    // exakta: timerns två värden och ikonens fem.
    {"glaze::Timer",         "interval",      false},
    {"glaze::Timer",         "singleShot",    false},
    {"glaze::TrayIcon",      "icon",          false},
    {"glaze::TrayIcon",      "smallIcon",     false},
    {"glaze::TrayIcon",      "templateImage", false},
    {"glaze::TrayIcon",      "toolTip",       false},
    {"glaze::TrayIcon",      "interval",      false},
    // PEKAREN: basens — VARJE inbyggd WIDGET,
    // aldrig en komponent.
    {"glaze::Widget",        "cursor",        true},
    // ETIKETT- OCH KNAPPFÄLT (skrivna OBEROENDE av
    // produktionstabellen): elva exakta. Knapparnas `color` står INTE här —
    // `setColor(std::optional)` är en anropssats (fallReq3).
    {"glaze::Label",         "elide",         false},
    {"glaze::Label",         "vAlign",        false},
    {"glaze::Label",         "buddy",         false},
    {"glaze::ListView",      "fillColumn",    false},
    {"glaze::DataGrid",      "fillColumn",    false},
    {"glaze::ListView",      "iconSize",      false},
    {"glaze::DataGrid",      "iconSize",      false},
    {"glaze::Button",        "iconSize",      false},
    {"glaze::ToolButton",    "iconSize",      false},
    {"glaze::TextArea",      "acceptsTab",    false},
    {"glaze::FilePicker",    "dialogTitle",   false},
    // Etikettens breddgrupp — exakt Label.
    {"glaze::Label",         "widthGroup",    false},
    // Etikettens gloriabredd — exakt Label — och flexradens
    // rena läge — exakt Bar (TopBar/StatusBar/ToolBar har det inte som form).
    {"glaze::Label",         "haloWidth",     false},
    {"glaze::Bar",           "plain",         false},
    // Tabellernas radrubriker — ListView och DataGrid, exakt.
    {"glaze::ListView",      "rowHeaders",    false},
    {"glaze::DataGrid",      "rowHeaders",    false},
    // Den platta ritytan — exakt Canvas (ingen Panel, som `border`).
    {"glaze::Canvas",        "flat",          false},
    // Textstorleken — de fyra fälten och listorna, exakt (inte
    // ListBoxens ättlingar genom `subtypes`: TreeView är ingen ListBox, och
    // en egen subklass behåller sin anropsform).
    {"glaze::TextInput",     "textSize",      false},
    {"glaze::TextArea",      "textSize",      false},
    {"glaze::ListBox",       "textSize",      false},
    {"glaze::Dropdown",      "textSize",      false},
};

// OBEROENDE arvsreferens för BASEN: varje inbyggd typ i domänen är en widget
// — det är vad en palettrad ÄR — UTOM de registrerade KOMPONENTERNA
//, som domänen också bär och som aldrig ärver Widget.
// Referensen för det är ramverkets komponentregister, inte arvstabellen.
// Medvetet inte `bld::isOrDerivesFrom(t, "glaze::Widget")`: en palettyp som
// saknas i arvstabellen ska fälla matrisen här, inte bli "rätt" av samma
// lucka på båda sidor.
bool ärverWidgetOberoende(const std::string& t)
{
    return t.rfind("glaze::", 0) == 0 &&
           !glaze::componentPreviewForm(t).has_value();
}

// OBEROENDE arvsfacit — Panels ättlingar, skrivna ur widget-headerna och
// INTE hämtade ur `bld::isOrDerivesFrom`. `Panel` och `Widget` är de enda
// som bär `subtypes`, så de två är hela oraklet arvet behöver.
bool ärverPanelOberoende(const std::string& t)
{
    return t == "glaze::Panel" || t == "glaze::Bar" ||
           t == "glaze::MenuBar" || t == "glaze::TopBar" ||
           t == "glaze::StatusBar" || t == "glaze::ToolBar" ||
           t == "glaze::PropertyGrid" || t == "glaze::ListBox" ||
           t == "glaze::TreeView" ||
           // TABELLEN — via den delade basen `Table : Panel`.
           t == "glaze::ListView" || t == "glaze::DataGrid" ||
           // FLIKSIDAN ärver Panel (tab_page.h).
           t == "glaze::TabPage" ||
           // DET FLERRADIGA FÄLTET ärver Panel (text_area.h).
           t == "glaze::TextArea" ||
           // MARKDOWN-VYN ärver Panel (markdown_view.h).
           t == "glaze::MarkdownView" ||
           // DEN FLYTTBARA PANELEN ärver Panel (floating_panel.h).
           t == "glaze::FloatingPanel" ||
           // TERMINALEN ärver Panel (glaze_terminal/terminal.h).
           t == "glaze::Terminal";
}

// Referens: ska (typ, fält) klassas som setter-fält?
bool vantatUtfall(const std::string& typ, const std::string& fält)
{
    for (const Vantad& v : kVantadMatris) {
        if (fält != v.field)
            continue;
        if (!v.cppType)
            return true;
        const bool basen = std::string(v.cppType) == "glaze::Widget";
        if (v.subtypes ? (basen ? ärverWidgetOberoende(typ)
                                : ärverPanelOberoende(typ))
                       : typ == v.cppType)
            return true;
    }
    return false;
}

void fallSetterMatris()
{
    // ---- 1+2. FULLSTÄNDIGHET och ORAKEL: exakt samma mängd poster.
    const std::span<const bld::SetterFieldSpec> specar = bld::setterFieldSpecs();
    check(specar.size() == std::size(kVantadMatris),
          "matris: produktionen har lika många poster som kravmatrisen",
          std::to_string(specar.size()) + " mot " +
              std::to_string(std::size(kVantadMatris)));

    for (const bld::SetterFieldSpec& s : specar) {
        bool hittad = false;
        for (const Vantad& v : kVantadMatris)
            if (std::string(s.field) == v.field && s.subtypes == v.subtypes &&
                ((!s.cppType && !v.cppType) ||
                 (s.cppType && v.cppType &&
                  std::string(s.cppType) == v.cppType)))
                hittad = true;
        check(hittad, std::string("matris: produktionsposten {") +
                          (s.cppType ? s.cppType : "*") + ", " + s.field +
                          "} står i kravmatrisen",
              s.cppType ? s.cppType : "(alla)");
    }
    for (const Vantad& v : kVantadMatris) {
        bool hittad = false;
        for (const bld::SetterFieldSpec& s : specar)
            if (std::string(s.field) == v.field && s.subtypes == v.subtypes &&
                ((!s.cppType && !v.cppType) ||
                 (s.cppType && v.cppType &&
                  std::string(s.cppType) == v.cppType)))
                hittad = true;
        check(hittad, std::string("matris: kravposten {") +
                          (v.cppType ? v.cppType : "*") + ", " + v.field +
                          "} finns i produktionen",
              v.cppType ? v.cppType : "(alla)");
    }

    // ---- REGISTER ↔ PALETT. Registret känner KLASSEN bakom varje
    //      typnamn (`widgetClassOf`), så BUILT-IN-mängden är de registrerade
    //      klasserna i namnrymden `glaze::` — komponentoraklets regel nedan —
    //      och grindens EGEN widget (`pgate::Matare`, fallEgnaWidgetar) står
    //      utanför den. Mängden är inte palettens `regType` — en
    //      andra sanning om typ ↔ klass som paletten inte bär.
    const std::vector<std::string> reg = glaze::registeredWidgetTypes();
    std::vector<std::string>       ramverkets;   // klassnamnen, `glaze::`
    for (const std::string& t : reg) {
        const std::string k = glaze::widgetClassOf(t);
        if (k.rfind("glaze::", 0) == 0)
            ramverkets.push_back(k);
    }
    for (std::size_t i = 0; i < bld::kPaletteCount; ++i) {
        const bld::PaletteEntry& pe = bld::kPalette[i];
        const auto n = std::count(ramverkets.begin(), ramverkets.end(),
                                  std::string(pe.cppType));
        check(n == 1, std::string("palett: '") + pe.cppType +
                          "' är en registrerad klass, exakt en gång",
              std::to_string(n));
        // Varje palettrads cppType måste vara KLASSAD i arvstabellen —
        // annars kan `subtypes`-flaggan tyst missa en ättling. Det här är
        // det bästa som går utan reflektion: den manuella tabellen kan
        // inte glida isär TYST.
        check(bld::isKnownType(pe.cppType),
              std::string("arv: '") + pe.cppType +
                  "' är klassad i arvstabellen",
              pe.cppType);
    }
    // Dubbelriktat: ingen registrerad RAMVERKSKLASS saknar en palettrad.
    // (Grindens egna registreringar har namn utanför `glaze::` och räknas
    // inte — de är den användartyp registret ska kunna bära.)
    for (const std::string& k : ramverkets) {
        bool iPaletten = false;
        for (std::size_t i = 0; i < bld::kPaletteCount; ++i)
            if (k == bld::kPalette[i].cppType)
                iPaletten = true;
        check(iPaletten, "register: '" + k + "' är kartlagd i paletten", k);
    }
    check(ramverkets.size() + 1 == reg.size(),
          "register: alla registrerade typer utom grindens egen "
          "(pgate::Matare) är ramverkets klasser",
          std::to_string(ramverkets.size()) + " av " + std::to_string(reg.size()));

    // ---- 3. KARTESISKA PRODUKTEN. Utan en definierad domän är "varje fel
    //      typ" inte mätbart, och en implementation kan råka testa ett
    //      handplockat negativfall per rad och missa en kollision ändå.
    //      Domänen är samtliga built-in cppType ur palettmappningen.
    std::vector<std::string> domän;
    for (std::size_t i = 0; i < bld::kPaletteCount; ++i)
        domän.push_back(bld::kPalette[i].cppType);
    // …och de registrerade KOMPONENTERNA: de står i
    // regionen som medlemmar, och deras setter-fält går samma väg.
    for (const std::string& k : glaze::registeredComponentTypes())
        domän.push_back(k);

    // KOMPONENTORAKLET: ramverkets egna är EXAKT Timer och TrayIcon, båda
    // klassade som komponenter och ingen som widget. (Grindens egna
    // testkomponenter har namn utanför `glaze::` och räknas inte här.)
    {
        std::vector<std::string> egna;
        for (const std::string& k : glaze::registeredComponentTypes())
            if (k.rfind("glaze::", 0) == 0)
                egna.push_back(k);
        check(egna == std::vector<std::string>{"glaze::Timer",
                                               "glaze::TrayIcon"},
              "komponenter: ramverkets registrerade är exakt Timer och "
              "TrayIcon",
              std::to_string(egna.size()) + " st");
        for (const std::string& k : egna)
            check(bld::isKnownType(k) &&
                      bld::isOrDerivesFrom(k, "glaze::Component") &&
                      !bld::isOrDerivesFrom(k, "glaze::Widget"),
                  "komponenter: " + k + " ärver Component och INTE Widget", k);
        check(glaze::componentPreviewForm("glaze::Timer") ==
                      glaze::PreviewForm::Inert &&
                  glaze::componentPreviewForm("glaze::TrayIcon") ==
                      glaze::PreviewForm::Inert,
              "komponenter: båda är registrerade som INERTA");
        check(!bld::isSetterField("glaze::Timer", "cursor") &&
                  !bld::isSetterField("glaze::TrayIcon", "allowDrag"),
              "komponenter: basens widgetfält träffar ingen komponent");
    }

    std::vector<std::string> fält;
    for (const Vantad& v : kVantadMatris)
        if (std::find(fält.begin(), fält.end(), std::string(v.field)) ==
            fält.end())
            fält.push_back(v.field);

    int avvikelser = 0;
    for (const std::string& f : fält)
        for (const std::string& t : domän) {
            const bool vantat = vantatUtfall(t, f);
            const bool fick   = bld::isSetterField(t, f);
            if (vantat != fick) {
                ++avvikelser;
                std::printf("  FEL  matris: %s.%s — väntade %s, fick %s\n",
                            t.c_str(), f.c_str(), vantat ? "Prop" : "Call",
                            fick ? "Prop" : "Call");
            }
            // …och samma sak genom METODVÄGEN, som readern faktiskt
            // frågar: `setterField(typ, "setX", ut)`.
            std::string ut;
            const bool viaMetod =
                bld::setterField(t, bld::setterName(f), ut) && ut == f;
            if (vantat != viaMetod) {
                ++avvikelser;
                std::printf("  FEL  matris: %s.%s via setterField() — "
                            "väntade %s\n", t.c_str(), f.c_str(),
                            vantat ? "true" : "false");
            }
        }
    g_fails += avvikelser;
    ++g_cases;
    check(avvikelser == 0,
          "matris: hela kartesiska produkten (" +
              std::to_string(fält.size()) + " fält × " +
              std::to_string(domän.size()) + " typer) stämmer med "
              "kravmatrisen",
          std::to_string(avvikelser) + " avvikelser");

    // ---- RIKTADE FALL där felen faktiskt satt (en exempelgrind hade
    //      släppt igenom två av dem).
    check(bld::isSetterField("glaze::Picture", "img"),
          "riktat: Picture.img är ett setter-fält (annars försvinner "
          "egenskapspanelens bildrad)", "");
    for (const char* t : {"glaze::Button", "glaze::Checkbox", "glaze::Radio",
                          "glaze::Label", "glaze::Link", "glaze::TextInput"})
        check(bld::isSetterField(t, "text"),
              std::string("riktat: ") + t + ".text är ett setter-fält", t);
    check(!bld::isSetterField("glaze::Panel", "text"),
          "riktat: Panel.setText är INGEN setter (typen saknar nyckeln)",
          "");
    for (const char* t : {"glaze::Panel", "glaze::Bar", "glaze::MenuBar",
                          "glaze::TopBar", "glaze::StatusBar",
                          "glaze::ToolBar", "glaze::PropertyGrid",
                          "glaze::ListBox", "glaze::TreeView",
                          "glaze::ListView", "glaze::DataGrid",
                          "glaze::TabPage"}) {
        check(bld::isSetterField(t, "clip") &&
                  bld::isSetterField(t, "border"),
              std::string("riktat: ") + t + " ÄRVER clip/border", t);
        check(bld::isOrDerivesFrom(t, "glaze::Panel"),
              std::string("riktat: ") + t + " härleds ur Panel", t);
    }
    check(!bld::isOrDerivesFrom("glaze::Button", "glaze::Panel"),
          "riktat: Button härleds INTE ur Panel", "");
    // KOLLISIONEN, mätt från båda hållen.
    check(!bld::isSetterField("glaze::ListBox", "checked"),
          "riktat: ListBox.setChecked(int,bool) blir en Call", "");
    check(bld::isSetterField("glaze::Checkbox", "checked") &&
              bld::isSetterField("glaze::Radio", "checked"),
          "riktat: Checkbox/Radio.setChecked(bool) förblir en Prop", "");
    // …och `color` är borta på varje typ UTOM färgväljaren.
    // Labels `setColor` tar en `gfx::Color` och dess `setProp("color")` ett
    // rollnamn — den kollisionen är kvar stängd. Färgväljarens värde ÄR
    // hextexten, och där är fältet en EXAKT post.
    for (const std::string& t : domän)
        check(bld::isSetterField(t, "color") == (t == "glaze::ColorPicker"),
              "riktat: color är setter-fält BARA på färgväljaren (" + t + ")",
              t);
}

// ================================================ VERKTYGSFÄLTET
//
// Regel 8 åt PREVIEW-hållet: formerna `.glazeui` bär för verktygsknappen
// ska UTFÖRAS — annars ritar canvasen en knapp utan text och utan läge
// medan den byggda appen har dem. Typmatrisen ovan mäter KLASSNINGEN (Prop
// och inte Call); det här mäter att Prop:en faktiskt NÅR widgeten, i den
// form readern levererar den (strängliteral, enum med fullt namn, bool).
void fallVerktygsfalt()
{
    auto w = build("glaze::ToolButton",
                   {{"text", "\"Spara\""},
                    {"imagePos", "glaze::ImagePos::Above"},
                    {"checkable", "true"},
                    {"checked", "true"}});
    if (!w)
        return;
    auto* tb = dynamic_cast<glaze::ToolButton*>(w.get());
    check(tb != nullptr, "verktyg: registret ger en riktig glaze::ToolButton",
          "");
    if (!tb)
        return;
    check(tb->text() == "Spara", "verktyg: text avciteras och når knappen",
          tb->text());
    check(tb->imagePos() == glaze::ImagePos::Above,
          "verktyg: imagePos-enumet når knappen", "");
    check(tb->isCheckable() && tb->isChecked(),
          "verktyg: checkable + checked når knappen", "");

    // tr-vägen går genom `setTextToken` — utan override hade raden blivit
    // OPAK (typen tar inget token) och previewn visat "Spara" kvar.
    glaze::setLanguage("sv");
    bld::applyProp(*tb, {"text", "glaze::tr(2)"}, "preview_gate");
    check(tb->textToken().id() == 2 && tb->text() == "Avbryt",
          "verktyg: glaze::tr(N) sätter TOKENET", tb->text());

    // `title` är ett alias bara på Label, Button, Checkbox, Radio, Link och
    // TextInput — verktygsknappen har det inte, så nyckeln är okänd (opak rad).
    check(!tb->setProp("title", "x"), "verktyg: inget title-alias", "");

    // Remsan: Dock::Top ur konstruktorn (WinForms ToolStrip.Dock).
    // Den bär dessutom en TEXT — titeln i det flytande fönstrets
    // rubrik — så `ToolBar1.setText(…)` är en setter (fallet nedan mäter
    // resten av det flytande läget).
    auto bar = build("glaze::ToolBar", {});
    if (!bar)
        return;
    check(dynamic_cast<glaze::ToolBar*>(bar.get()) != nullptr &&
              bar->dock == glaze::Dock::Top,
          "verktyg: ToolBar dockar Top som default", "");
    check(bld::isSetterField("glaze::ToolBar", "text"),
          "verktyg: ToolBar har en text-setter (det flytande fönstrets "
          "titel)",
          "");
    // Remsan ÄRVER Panels clip/border via Bar — klassningen och arvet.
    check(bld::isOrDerivesFrom("glaze::ToolBar", "glaze::Bar"),
          "verktyg: ToolBar härleds ur Bar", "");
}

// ================================================== GRUPPRAMEN
//
// Rubriken är gruppramens enda egna tillstånd, och den ska nå widgeten i
// BÅDA formerna `.glazeui` kan bära — literal och översättnings-id. Att
// ramen INTE är en Panel mäts här också: annars hade settertabellens
// `subtypes` tyst gett den Panels clip/border-rader, alltså setter-anrop
// typen saknar och som inte kompilerar.
void fallGruppram()
{
    auto w = build("glaze::GroupBox", {{"text", "\"Inställningar\""}});
    if (!w)
        return;
    auto* g = dynamic_cast<glaze::GroupBox*>(w.get());
    check(g != nullptr, "gruppram: registret ger en riktig glaze::GroupBox",
          "");
    if (!g)
        return;
    check(g->text() == "Inställningar",
          "gruppram: rubriken (text) avciteras och når ramen", g->text());

    glaze::setLanguage("sv");
    bld::applyProp(*g, {"text", "glaze::tr(2)"}, "preview_gate");
    check(g->textToken().id() == 2 && g->text() == "Avbryt",
          "gruppram: glaze::tr(N) sätter TOKENET", g->text());
    check(!g->setProp("title", "x"), "gruppram: inget title-alias", "");

    check(!bld::isOrDerivesFrom("glaze::GroupBox", "glaze::Panel"),
          "gruppram: härleds INTE ur Panel (ärver Widget med flit)", "");
    check(!bld::isSetterField("glaze::GroupBox", "clip") &&
              !bld::isSetterField("glaze::GroupBox", "border"),
          "gruppram: ärver INTE Panels clip/border-setters", "");
}

// =================================================== TALFÄLTET
//
// Talfältets tillstånd är TAL, och `.glazeui` bär dem som C++-LITERALER —
// alltså även formerna C++ godtar och en naiv parser inte gör (`1e3`,
// `2.5f`, `.5`). En striktare tolkning hade gjort lagliga rader opaka: ett
// regel 8-brott, inte en försiktig delmängd. Och ordningen får inte spela
// roll — satserna körs i källordning, och `setValue(150)` före
// `setMax(200)` ska ge 150.
void fallTalfalt()
{
    auto w = build("glaze::SpinBox", {{"value", "150"},
                                      {"max", "200"},
                                      {"min", "-1e3"},
                                      {"step", "2.5f"},
                                      {"decimals", "1"}});
    if (!w)
        return;
    auto* s = dynamic_cast<glaze::SpinBox*>(w.get());
    check(s != nullptr, "talfält: registret ger en riktig glaze::SpinBox", "");
    if (!s)
        return;
    check(s->value() == 150.0,
          "talfält: setValue(150) FÖRE setMax(200) ger 150 (klampning vid "
          "läsning)", std::to_string(s->value()));
    check(s->min() == -1000.0, "talfält: C++-literalen -1e3 når min",
          std::to_string(s->min()));
    check(s->step() == 2.5, "talfält: 2.5f (suffixet) når step",
          std::to_string(s->step()));
    check(s->decimals() == 1, "talfält: decimals når fältet",
          std::to_string(s->decimals()));

    // Ett ogiltigt tal ⇒ loggas och IGNORERAS — värdet står kvar, aldrig 0.
    bld::applyProp(*s, {"value", "abc"}, "preview_gate");
    check(s->value() == 150.0, "talfält: ogiltigt tal lämnar värdet orört",
          std::to_string(s->value()));
    // Kommatecken är aldrig decimaltecken i C++ — raden är inte ett tal.
    bld::applyProp(*s, {"value", "1,5"}, "preview_gate");
    check(s->value() == 150.0, "talfält: \"1,5\" är ingen C++-literal",
          std::to_string(s->value()));
    // Skräp EFTER ett giltigt tal — en parser som slutar läsa vid första
    // okända tecknet hade tagit 12 och tyst kastat resten.
    bld::applyProp(*s, {"value", "12abc"}, "preview_gate");
    check(s->value() == 150.0, "talfält: \"12abc\" avvisas helt (inte 12)",
          std::to_string(s->value()));
    // Den inledande punkten är laglig C++ (`.5`) och ska därför tolkas.
    bld::applyProp(*s, {"value", ".5"}, "preview_gate");
    check(s->value() == 0.5, "talfält: C++-literalen .5 når value",
          std::to_string(s->value()));
}

// ==================================================== DELAREN
//
// Delarens fem fält ska nå widgeten i den form readern levererar dem: en
// bool, ett enum med typens fulla prefix och tre mått som C++-LITERALER
// (`40.5f`, `1e2`). Klassningen (Prop och inte Call) mäter matrisen ovan;
// det här mäter att Prop:en faktiskt UTFÖRS — och att ett skräptal lämnar
// det gamla värdet stående i stället för att bli 0 eller 12.
void fallDelare()
{
    auto w = build("glaze::Splitter",
                   {{"vertical", "false"},
                    {"fixedPane", "glaze::Splitter::Pane::Second"},
                    {"paneSize", "220"},
                    {"firstMin", "40.5f"},
                    {"secondMin", "1e2"}});
    if (!w)
        return;
    auto* s = dynamic_cast<glaze::Splitter*>(w.get());
    check(s != nullptr, "delare: registret ger en riktig glaze::Splitter", "");
    if (!s)
        return;
    check(!s->isVertical(), "delare: vertical=false når delaren", "");
    check(s->fixedPane() == glaze::Splitter::Pane::Second,
          "delare: fixedPane-enumet (fullt prefix) når delaren", "");
    check(s->paneSize() == 220.0f, "delare: paneSize når delaren",
          std::to_string(s->paneSize()));
    check(s->firstMin() == 40.5f, "delare: literalen 40.5f når firstMin",
          std::to_string(s->firstMin()));
    check(s->secondMin() == 100.0f, "delare: literalen 1e2 når secondMin",
          std::to_string(s->secondMin()));

    bld::applyProp(*s, {"paneSize", "12abc"}, "preview_gate");
    check(s->paneSize() == 220.0f, "delare: \"12abc\" avvisas helt (inte 12)",
          std::to_string(s->paneSize()));
    bld::applyProp(*s, {"paneSize", "1,5"}, "preview_gate");
    check(s->paneSize() == 220.0f, "delare: \"1,5\" är ingen C++-literal",
          std::to_string(s->paneSize()));

    // Delaren är INGEN Panel — matrisen hade annars gett den Panels
    // clip/border-sättare, som inte kompilerar på typen.
    check(!bld::isOrDerivesFrom("glaze::Splitter", "glaze::Panel"),
          "delare: härleds INTE ur Panel", "");
    check(!bld::isSetterField("glaze::Splitter", "border"),
          "delare: ärver INTE Panels border-sättare", "");
}

// ================================================== DOCKNINGEN
//
// Panelens egna fält och värdens LAYOUTSTRÄNG ska nå widgetarna i den form
// `.glazeui` bär dem: rubriken som literal OCH översättnings-id,
// `collapsed` som bool, kanten som det RÅA layoutfältet `dock` — och
// `setLayout("…")` som en setter vars strängliteral avciteras och UTFÖRS.
// Att en trasig sträng lämnar layouten ORÖRD mäts på widgetarnas tillstånd.
void fallDockning()
{
    auto w = build("glaze::DockPanel", {{"text", "\"Verktyg\""},
                                        {"collapsed", "true"},
                                        {"dock", "glaze::Dock::Right"},
                                        {"width", "120"}});
    if (!w)
        return;
    auto* p = dynamic_cast<glaze::DockPanel*>(w.get());
    check(p != nullptr, "dockning: registret ger en riktig glaze::DockPanel",
          "");
    if (!p)
        return;
    check(p->text() == "Verktyg", "dockning: rubriken når panelen", p->text());
    check(p->isCollapsed(), "dockning: collapsed=true når panelen", "");
    check(p->dock == glaze::Dock::Right && p->width == 120.0f,
          "dockning: dock och width (råa layoutfält) når panelen", "");
    glaze::setLanguage("sv");
    bld::applyProp(*p, {"text", "glaze::tr(2)"}, "preview_gate");
    check(p->textToken().id() == 2 && p->text() == "Avbryt",
          "dockning: glaze::tr(N) sätter rubrikens TOKEN", p->text());
    check(!p->setProp("title", "x"), "dockning: inget title-alias", "");
    check(glaze::DockPanel().dock == glaze::Dock::Left,
          "dockning: en ny panel dockar Left (konstruktorn)", "");

    // Panelens former: stängknappen och ikonen som
    // setter-fält, min/max och activate() som anrop.
    const char* o = "preview_gate";
    bld::applyProp(*p, {"closable", "false"}, o);
    check(!p->isClosable(), "dockning: setClosable(false) når panelen", "");
    check(p->setProp("img", "lista/nyckel") &&
              !p->setProp("img", "utan-snedstreck"),
          "dockning: img tar \"lista/nyckel\" (Buttons form) och inget annat",
          "");
    check(bld::applyCall(*p, {"setMinimumSize", "160, 0"}, o) &&
              p->minimumSize().w == 160.0f && p->minimumSize().h == 0.0f,
          "dockning: setMinimumSize(160, 0) lagras",
          std::to_string(p->minimumSize().w));
    check(bld::applyCall(*p, {"setMaximumSize", "400.5, 300"}, o) &&
              p->maximumSize().w == 400.5f && p->maximumSize().h == 300.0f,
          "dockning: setMaximumSize(400.5, 300) lagras",
          std::to_string(p->maximumSize().w));
    check(!bld::applyCall(*p, {"setMinimumSize", "160"}, o) &&
              !bld::applyCall(*p, {"setMaximumSize", "bred, 0"}, o) &&
              p->minimumSize().w == 160.0f && p->maximumSize().w == 400.5f,
          "dockning: fel antal eller ett icke-tal ⇒ opak rad, måtten ORÖRDA",
          "");
    p->hide();
    check(bld::applyCall(*p, {"activate", ""}, o) && p->isVisible() &&
              !p->isCollapsed(),
          "dockning: activate() utan värd = öppen och utfälld", "");

    // VÄRDEN: två namngivna paneler, och layouten som en setter-rad (v2).
    auto h = build("glaze::DockHost", {});
    if (!h)
        return;
    auto* host = dynamic_cast<glaze::DockHost*>(h.get());
    check(host != nullptr, "dockning: registret ger en riktig glaze::DockHost",
          "");
    if (!host)
        return;
    auto* a = host->add<glaze::DockPanel>("A");
    auto* b = host->add<glaze::DockPanel>("B");
    a->name = "a";
    b->name = "b";
    const std::string sträng =
        "v2;s=top;c=1;p=b,1,1,1,0,0,80;s=right;c=1;p=a,0,0,1,1,150,0";
    bld::applyProp(*host, {"layout", "\"" + sträng + "\""}, o);
    check(b->dock == glaze::Dock::Top && b->height == 80.0f &&
              b->isCollapsed() && b->isVisible(),
          "dockning: setLayout når panel b (kant, höjd, infälld, öppen)", "");
    check(a->dock == glaze::Dock::Right && a->width == 150.0f &&
              !a->isCollapsed() && !a->isVisible(),
          "dockning: setLayout når panel a (kant, bredd, utfälld, stängd)", "");
    check(host->layout() == sträng,
          "dockning: layout() ger tillbaka samma sträng (ordningen med)",
          host->layout());
    // TRASIG sträng ⇒ logg och ORÖRD layout (atomiskt — ett fel i ANDRA
    // posten får inte ha tillämpat den första).
    bld::applyProp(*host,
                   {"layout", "\"v2;s=left;c=1;p=b,1,0,1,0,0,80;s=mitten;c=1;"
                              "p=a,1,0,1,0,1,1\""},
                   o);
    check(b->dock == glaze::Dock::Top && b->isCollapsed(),
          "dockning: en trasig post lämnar HELA layouten orörd", "");
    bld::applyProp(*host, {"layout", "\"v1;b=left,0,80,0,0\""}, o);
    check(b->dock == glaze::Dock::Top,
          "dockning: v1 (en äldre version) lämnar layouten orörd", "");

    // dockPanel — panelerna som IDENTIFIERARE bland barnen, kanten med prefix.
    check(bld::applyCall(*host, {"dockPanel", "a, b, glaze::Dock::Fill"}, o) &&
              a->dock == glaze::Dock::Top,
          "dockning: dockPanel(a, b, Fill) — a blir en flik hos b (Top)", "");
    check(host->layout() ==
              "v2;s=top;c=1;p=b,1,1,1,0,0,80;p=a,0,0,1,1,150,0",
          "dockning: …och layouten säger en flikgrupp (den stängda är inte "
          "aktiv)",
          host->layout());
    check(bld::applyCall(*host, {"dockPanel", "a, glaze::Dock::Bottom"}, o) &&
              a->dock == glaze::Dock::Bottom,
          "dockning: dockPanel(a, Bottom) — ytterst vid nederkanten", "");
    check(!bld::applyCall(*host, {"dockPanel", "okand, b, glaze::Dock::Fill"},
                          o) &&
              !bld::applyCall(*host, {"dockPanel", "a, glaze::Align::Left"}, o) &&
              !bld::applyCall(*host, {"dockPanel", "\"a\", glaze::Dock::Top"},
                              o) &&
              a->dock == glaze::Dock::Bottom,
          "dockning: okänt barn, fel prefix eller en strängliteral ⇒ opak, "
          "ingenting ändras", "");

    // Knappnålen som setter-fält, och den ÖPPNA overlayn som
    // previewn UTFÖR: en onålad panel som fälls ut är värdens främre barn
    // (`frontChild`), och att fästa den gör den dockad igen.
    bld::applyProp(*b, {"pinned", "false"}, o);
    check(!b->isPinned() && b->isCollapsed() &&
              host->layout().find(";p=b,1,1,0,0,") != std::string::npos,
          "dockning: setPinned(false) når panelen — onålad är infälld, och "
          "layouten skriver den så",
          host->layout());
    check(host->frontChild() == nullptr,
          "dockning: ett handtag är inget främre barn", "");
    bld::applyProp(*b, {"collapsed", "false"}, o);
    check(host->frontChild() == b && b->showsChildren(),
          "dockning: en onålad panel som fälls ut är OVERLAYN — värdens "
          "främre barn, och dess barn syns",
          "");
    bld::applyProp(*b, {"pinned", "true"}, o);
    check(b->isPinned() && !b->isCollapsed() &&
              host->frontChild() == nullptr,
          "dockning: setPinned(true) fäster overlayn — dockad, inget främre "
          "barn",
          "");

    // Det flytande läget som setter-fält och platsen som anrop.
    // Previewn UTFÖR tillståndet: panelen lämnar layouten och `layout()`
    // skriver `f=` — men trädet står inte i ett öppet fönster, så inget
    // fönster öppnas och panelen förblir värdens barn. Platsen skrivs `-` där
    // fönster saknar skärmposition (headless före init, Wayland).
    a->show();   // en ÖPPEN panel — så att det är flytet som döljer barnen
    const std::string läge =
        "v2;s=bottom;c=1;p=a,1,0,1,0,150,0;s=top;c=1;p=b,1,0,1,0,0,80";
    check(host->layout() == läge,
          "dockning: utgångsläget före de flytande fallen", host->layout());
    bld::applyProp(*a, {"floatable", "false"}, o);
    check(!a->isFloatable(), "dockning: setFloatable(false) når panelen", "");
    check(bld::applyCall(*a, {"setFloatingBounds", "40, -20, 300, 200.5"}, o) &&
              a->floatingBounds().x == 40.0f &&
              a->floatingBounds().y == -20.0f &&
              a->floatingBounds().w == 300.0f &&
              a->floatingBounds().h == 200.5f,
          "dockning: setFloatingBounds(40, -20, 300, 200.5) lagras", "");
    check(!bld::applyCall(*a, {"setFloatingBounds", "1, 2, 3"}, o) &&
              !bld::applyCall(*a, {"setFloatingBounds", "1, 2, bred, 4"}, o) &&
              a->floatingBounds().x == 40.0f &&
              a->floatingBounds().w == 300.0f,
          "dockning: setFloatingBounds med fel antal eller ett icke-tal ⇒ "
          "opak, platsen ORÖRD",
          "");
    check(host->layout() == läge,
          "dockning: platsen ensam ändrar inte layouten (panelen flyter inte)",
          host->layout());
    const bool        pos   = glaze::DockHost::isFloatDragSupported();
    const std::string flyt1 = "v2;s=top;c=1;p=b,1,0,1,0,0,80;f=a," +
                              std::string(pos ? "40,-20" : "-,-") +
                              ",300,200.5,0,bottom,150,0";
    bld::applyProp(*a, {"floating", "true"}, o);
    check(a->isFloating() && a->parent() == host && !a->showsChildren() &&
              host->layout() == flyt1,
          "dockning: setFloating(true) — panelen lämnar layouten (f=, ingen "
          "p=), inget fönster öppnas och dess barn syns inte",
          host->layout());
    bld::applyProp(*a, {"floating", "false"}, o);
    check(!a->isFloating() &&
              host->layout() == "v2;s=top;c=1;p=b,1,0,1,0,0,80;s=bottom;c=1;"
                                "p=a,1,0,1,0,150,0",
          "dockning: setFloating(false) — innersta remsa vid sin kant igen",
          host->layout());
    const std::string flyt2 =
        "v2;s=top;c=1;p=b,1,0,1,0,0,80;f=a,12.5,-4,300,200,0,right,150,0";
    check(host->setLayout(flyt2) && a->isFloating() && a->isVisible() &&
              a->dock == glaze::Dock::Right &&
              host->layout() ==
                  (pos ? flyt2
                       : std::string("v2;s=top;c=1;p=b,1,0,1,0,0,80;f=a,-,-,"
                                     "300,200,0,right,150,0")),
          "dockning: f= tur och retur — flytande, öppen, dockningskanten "
          "Right",
          host->layout());
    const std::string före = host->layout();
    check(!host->setLayout("v2;f=a,10,-,300,200,0,right,150,0") &&
              !host->setLayout("v2;f=a,-,-,300,200,0,mitten,150,0") &&
              !host->setLayout("v2;f=a,-,-,300,200,0,right,150") &&
              !host->setLayout("v2;s=top;c=1;p=a,1,0,1,0,0,0;"
                               "f=a,-,-,0,0,0,right,0,0") &&
              host->layout() == före,
          "dockning: f= med blandad plats, okänd kant, fel fältantal eller ett "
          "namn två gånger avvisas — layouten orörd",
          host->layout());
    check(host->setLayout("v2;s=top;c=1;p=b,1,0,1,0,0,80;"
                          "f=a,-,-,0,0,0,none,0,0") &&
              a->dock == glaze::Dock::None && a->isFloating(),
          "dockning: f= med huvudytans ord — också en panel utan kant kan "
          "flyta",
          "");
    a->setFloating(false);

    check(!bld::isOrDerivesFrom("glaze::DockHost", "glaze::Panel") &&
              !bld::isOrDerivesFrom("glaze::DockPanel", "glaze::Panel"),
          "dockning: varken värden eller panelen härleds ur Panel", "");
    check(!bld::isSetterField("glaze::DockHost", "text"),
          "dockning: värden har ingen text-sättare", "");
}

// ================================== FLYTANDE VERKTYGSFÄLT
//
// Regel 8 åt PREVIEW-hållet för det flytande verktygsfältet: titeln, det
// flytande läget och flytbarheten är setter-fält som NÅR fältet — och
// previewn UTFÖR det flytande läget: fältet lämnar sitt band (värdens
// `layout()` skriver `ft=` och ingen `t=`) men inget fönster öppnas, för
// trädet står inte i ett öppet fönster. Platsen `setFloatingPosition(x, y)`
// är en Call med två tal, ATOMISKT: tre tal och ett icke-tal ger en opak
// rad och lämnar platsen orörd. `ft=` går tur och retur genom
// `setLayout`/`layout()` (med `-` där fönster saknar skärmposition), och en
// `t=` för ett fält som flyter dockar det — vid vänsterkanten STÅR det, och
// den satta längden följer med till höjden (ett riktningsbyte).
//
// VAD DEN INTE MÄTER: fönstret och rubriken (dock_gate FT), att raderna
// KOMPILERAR (`calls_fixture`) och byte-identiteten (roundtrip_gate).
void fallVerktygsfaltFlytande()
{
    const char* o = "preview_gate";
    auto        h = build("glaze::DockHost", {});
    if (!h)
        return;
    auto* host = dynamic_cast<glaze::DockHost*>(h.get());
    check(host != nullptr, "flytfält: registret ger en riktig glaze::DockHost",
          "");
    if (!host)
        return;
    auto* a  = host->add<glaze::ToolBar>();
    auto* b  = host->add<glaze::ToolBar>();
    a->name  = "a";
    b->name  = "b";
    b->width = 140;   // liggande: längden
    const std::string start = "v2;t=a,top,0,0,0;t=b,top,0,1,0";
    check(host->layout() == start, "flytfält: utgångsläget — två fält i band 0",
          host->layout());

    // TITELN — med ett tr-id (tokenet bevaras) och som literal.
    glaze::setLanguage("sv");
    bld::applyProp(*b, {"text", "glaze::tr(166)"}, o);
    check(b->textToken().id() == 166 && b->text() == "Verktygsfält",
          "flytfält: glaze::tr(166) sätter titelns TOKEN", b->text());
    bld::applyProp(*b, {"text", "\"Felsök\""}, o);
    check(b->text() == "Felsök", "flytfält: en literal når titeln", b->text());
    bld::applyProp(*a, {"floatable", "false"}, o);
    check(!a->isFloatable(), "flytfält: setFloatable(false) når fältet", "");
    // ÖVERFLÖDET — ett setter-fält som när fältet, förval på.
    check(a->overflow(), "överflöde: på som förval", "");
    bld::applyProp(*a, {"overflow", "false"}, o);
    check(!a->overflow(), "överflöde: setOverflow(false) når fältet", "");
    bld::applyProp(*a, {"overflow", "true"}, o);
    check(a->overflow(), "överflöde: setOverflow(true) slår på det igen", "");
    // `performClick()` på en verktygsknapp är KÄND men utförs aldrig, som på
    // en Button (den kör användarkod).
    {
        glaze::ToolButton tk("Knapp");
        int               klick = 0;
        tk.onClick              = [&klick] { ++klick; };
        check(bld::applyCall(tk, {"performClick", ""}, o) && klick == 0,
              "verktygsknapp: performClick() KÄND men utförs inte",
              std::to_string(klick));
        check(!bld::applyCall(tk, {"performClick", "1"}, o),
              "verktygsknapp: performClick(1) med argument är opak", "");
    }

    // PLATSEN — två tal, atomiskt.
    check(bld::applyCall(*b, {"setFloatingPosition", "500, -80.5"}, o) &&
              b->floatingPosition().x == 500.0f &&
              b->floatingPosition().y == -80.5f,
          "flytfält: setFloatingPosition(500, -80.5) lagras", "");
    check(!bld::applyCall(*b, {"setFloatingPosition", "1, 2, 3"}, o) &&
              !bld::applyCall(*b, {"setFloatingPosition", "1, bred"}, o) &&
              b->floatingPosition().x == 500.0f &&
              b->floatingPosition().y == -80.5f,
          "flytfält: tre tal eller ett icke-tal ⇒ opak rad, platsen ORÖRD",
          "");
    check(host->layout() == start,
          "flytfält: platsen ensam ändrar inte layouten (fältet flyter inte)",
          host->layout());

    // DET FLYTANDE LÄGET — utfört, utan fönster.
    const bool        pos   = glaze::DockHost::isFloatDragSupported();
    const std::string plats = pos ? "500,-80.5" : "-,-";
    bld::applyProp(*b, {"floating", "true"}, o);
    check(b->isFloating() && b->parent() == host &&
              host->layout() == "v2;t=a,top,0,0,0;ft=b," + plats + ",0,top,0",
          "flytfält: setFloating(true) — fältet lämnar bandet (ft=, ingen t=), "
          "och inget fönster öppnas",
          host->layout());

    // ft= TUR OCH RETUR: dold, och minnet vänsterkanten/band 2 — det LIGGER
    // ändå, och den satta längden är fortfarande bredden.
    const std::string ft = "v2;t=a,top,0,0,0;ft=b,12.5,-4,1,left,2";
    check(host->setLayout(ft) && b->isFloating() && !b->isVisible() &&
              b->dock == glaze::Dock::Left && b->band() == 2 &&
              !b->isVertical() && b->width == 140.0f &&
              host->layout() ==
                  (pos ? ft
                       : std::string("v2;t=a,top,0,0,0;ft=b,-,-,1,left,2")),
          "flytfält: ft= tur och retur — flytande, dold, minnet Left/band 2, "
          "och fältet ligger",
          host->layout());
    const std::string före = host->layout();
    check(!host->setLayout("v2;ft=b,1,2,0,top") &&
              !host->setLayout("v2;ft=b,1,-,0,top,0") &&
              !host->setLayout("v2;t=b,top,0,0,0;ft=b,-,-,0,top,0") &&
              host->layout() == före,
          "flytfält: fel fältantal, en blandad plats och ett namn i både t= "
          "och ft= avvisas — layouten orörd",
          host->layout());

    // En t= för ett FLYTANDE fält dockar det — vid vänsterkanten STÅR det, och
    // den satta längden (140) är höjden (ett riktningsbyte).
    check(host->setLayout("v2;t=a,top,0,0,0;t=b,left,0,0,0") &&
              !b->isFloating() && b->isVisible() && b->isVertical() &&
              b->height == 140.0f && b->width == 0.0f &&
              host->layout() == "v2;t=a,top,0,0,0;t=b,left,0,0,0",
          "flytfält: en t= för ett flytande fält dockar det — det står, och "
          "längden följer med till höjden",
          host->layout());
    bld::applyProp(*b, {"floating", "true"}, o);
    check(b->isFloating() && !b->isVertical() && b->width == 140.0f &&
              b->height == 0.0f,
          "flytfält: flytande igen — det ligger, och längden är bredden", "");
    bld::applyProp(*b, {"floating", "false"}, o);
    check(!b->isFloating() && b->isVertical() && b->height == 140.0f &&
              host->layout() == "v2;t=a,top,0,0,0;t=b,left,0,0,0",
          "flytfält: setFloating(false) — sist i band band() vid dock (minnet)",
          host->layout());
    check(!bld::isSetterField("glaze::ToolBar", "floatingPosition"),
          "flytfält: platsen är ingen setter (två tal — en Call)", "");
}

// ============================================ KOLLEKTIONSVOKABULÄREN
//
// Previewn UTFÖR styckeformen och massformen —
// utan de här fallen ritas en fylld grid TOM i canvasen och i
// glaze-render medan den byggda appen visar raderna.

glaze::PropertyGrid* gridAv(std::unique_ptr<glaze::Widget>& w)
{
    return dynamic_cast<glaze::PropertyGrid*>(w.get());
}

void fallPropertyGridStyckeform()
{
    auto w = build("glaze::PropertyGrid", {});
    if (!w)
        return;
    auto* g = gridAv(w);

    // TVÅ argument: readOnly false, tom tag, tomma choices.
    bld::applyCall(*w, {"addRow", "\"Bredd\", \"120\""}, "preview_gate");
    check(g->rows().size() == 1 && g->rows()[0].name == "Bredd" &&
              g->rows()[0].value == "120" && !g->rows()[0].readOnly &&
              g->rows()[0].tag.empty() && g->rows()[0].choices.empty(),
          "addRow: två argument ⇒ readOnly false, tom tag",
          std::to_string(g->rows().size()));

    // TRE och FYRA argument — de defaultade svansarna.
    bld::applyCall(*w, {"addRow", "\"Ägare\", \"system\", true"},
                   "preview_gate");
    bld::applyCall(*w, {"addRow", "\"Id\", \"7\", false, \"nyckel\""},
                   "preview_gate");
    check(g->rows().size() == 3 && g->rows()[1].readOnly &&
              !g->rows()[2].readOnly && g->rows()[2].tag == "nyckel",
          "addRow: tre- och fyraargumentsvarianterna",
          std::to_string(g->rows().size()));

    // ETT argument — värdet tar sitt default.
    bld::applyCall(*w, {"addRow", "\"Ensam\""}, "preview_gate");
    check(g->rows().size() == 4 && g->rows()[3].value.empty(),
          "addRow: ett argument ⇒ tomt värde", "");

    // ESCAPE i radnamnet går genom SAMMA strikta parser som all annan
    // strängliteral (`parseCppString`).
    bld::applyCall(*w, {"addRow", "\"Rad\\tett\", \"x\""}, "preview_gate");
    check(g->rows().size() == 5 && g->rows()[4].name == "Rad\tett",
          "addRow: escapen avkodas i radnamnet",
          g->rows().size() == 5 ? g->rows()[4].name : std::string());

    // setRow ERSÄTTER hela raden — tag och readOnly nollas när de
    // utelämnas. Tilldelningssemantik, inte partiell uppdatering.
    bld::applyCall(*w, {"setRow", "2, \"Id\", \"9\""}, "preview_gate");
    check(g->rows()[2].value == "9" && g->rows()[2].tag.empty() &&
              !g->rows()[2].readOnly,
          "setRow: ersätter HELA raden (utelämnad tag nollas)",
          g->rows()[2].tag);

    // INDEX UTANFÖR INTERVALLET ⇒ logg + no-op. Aldrig fail-safe, aldrig
    // tyst tillväxt.
    const std::size_t före = g->rows().size();
    bld::applyCall(*w, {"setRow", "99, \"X\", \"Y\""}, "preview_gate");
    bld::applyCall(*w, {"setRow", "-1, \"X\", \"Y\""}, "preview_gate");
    check(g->rows().size() == före,
          "setRow: index utanför intervallet ⇒ no-op, listan orörd",
          std::to_string(g->rows().size()));

    // LEDANDE `+` — laglig C++ som muterar rad 0. Avvisas det blir raden
    // OPAK, alltså ett regel
    // 8-brott: appen ersätter raden, previewn lämnar den orörd.
    // Parsern har ingen siffergräns som
    // avvisar giltiga tioställiga `int`.
    check(bld::applyCall(*w, {"setRow", "+0, \"Plus\", \"Ok\""},
                         "preview_gate") &&
              g->rows()[0].name == "Plus" && g->rows()[0].value == "Ok",
          "setRow: LEDANDE + accepteras — laglig C++ ⇒ utförd rad",
          g->rows()[0].name);
    // INT_MAX: tio siffror, alltså precis det en sifferGRÄNS
    // skulle avvisa. Talet PARSAS (raden känns igen ⇒ `true`, inte opak) och
    // faller sedan på intervallkontrollen ⇒ no-op. Skillnaden mot en opak
    // rad är hela poängen: en opak rad betyder "vi förstod den inte".
    check(bld::applyCall(*w, {"setRow", "2147483647, \"X\", \"Y\""},
                         "preview_gate") &&
              g->rows().size() == före && g->rows()[0].name == "Plus",
          "setRow: tioställigt INT_MAX parsas ⇒ känd rad, out-of-range no-op",
          std::to_string(g->rows().size()));
}

// MISSLYCKAD PARSNING — och det som mäts är WIDGETENS TILLSTÅND, inte
// modellen. `applyCall` får ingen muterbar modell alls, så ett test som
// bara kollar modellen är grönt per konstruktion medan previewn lagt till
// en tom eller halvparsad rad. Det är den PARTIELLA MUTATIONEN som är
// faran, och menygrenens "tom sträng + ändå framgång" är exakt hur den
// uppstår.
void fallStyckeformAtomisk()
{
    auto w = build("glaze::PropertyGrid", {});
    if (!w)
        return;
    auto* g = gridAv(w);
    bld::applyCall(*w, {"addRow", "\"A\", \"1\""}, "preview_gate");
    const auto orört = g->rows();

    struct Fall { const char* metod; const char* args; const char* vad; };
    const Fall fall[] = {
        {"addRow", "",                       "inga argument"},
        {"addRow", "\"A\", \"1\", \"2\", \"3\", \"4\"", "för många argument"},
        {"addRow", "\"A\\x41\", \"1\"",      "ogiltig strängescape"},
        {"addRow", "A, \"1\"",               "ociterat namn"},
        {"addRow", "\"A\", \"1\", kanske",   "ogiltig bool"},
        {"setRow", "\"0\", \"A\", \"1\"",    "index som sträng"},
        {"setRow", "0.5, \"A\", \"1\"",      "index som flyttal"},
        {"setRow", "0",                      "index utan namn"},
    };
    for (const Fall& f : fall) {
        const bool utfördes = bld::applyCall(*w, {f.metod, f.args},
                                             "preview_gate");
        bool lika = g->rows().size() == orört.size();
        for (std::size_t i = 0; lika && i < orört.size(); ++i)
            lika = g->rows()[i].name == orört[i].name &&
                   g->rows()[i].value == orört[i].value &&
                   g->rows()[i].readOnly == orört[i].readOnly &&
                   g->rows()[i].tag == orört[i].tag;
        check(!utfördes && lika,
              std::string("atomiskt: ") + f.vad +
                  " ⇒ rows() EXAKT oförändrad",
              std::to_string(g->rows().size()) + " rader");
    }
}

// SETRÖWS-LITERALEN — massformen, som är den `.glazeui` faktiskt bär
// (i GlazeBuilder används setRows, eftersom man oftast populerar
// med flera rader på en gång).
void fallSetRowsParser()
{
    auto w = build("glaze::PropertyGrid", {});
    if (!w)
        return;
    auto* g = gridAv(w);

    bld::applyCall(*w,
                   {"setRows",
                    "{{\"Bredd\", \"120\"}, {\"Typ\", \"Panel\", "
                    "{\"Panel\", \"Button\"}, true, \"t\"}}"},
                   "preview_gate");
    check(g->rows().size() == 2 && g->rows()[0].name == "Bredd" &&
              g->rows()[0].value == "120" && g->rows()[1].name == "Typ" &&
              g->rows()[1].choices.size() == 2 &&
              g->rows()[1].choices[1] == "Button" &&
              g->rows()[1].readOnly && g->rows()[1].tag == "t",
          "setRows: hela Row-aggregatet avkodas (choices, readOnly, tag)",
          std::to_string(g->rows().size()));

    // ERSÄTTER allt — massformens semantik.
    bld::applyCall(*w, {"setRows", "{{\"Enda\", \"1\"}}"}, "preview_gate");
    check(g->rows().size() == 1 && g->rows()[0].name == "Enda",
          "setRows: ersätter ALLA rader", std::to_string(g->rows().size()));

    // `{}` är en TOM lista och inte ett fel — som
    // `parseStringList`.
    bld::applyCall(*w, {"setRows", "{}"}, "preview_gate");
    check(g->rows().empty(), "setRows: {} är en tom lista, inte ett fel",
          std::to_string(g->rows().size()));

    // ATOMISKT vid parsfel: tillståndet är EXAKT oförändrat.
    bld::applyCall(*w, {"setRows", "{{\"Kvar\", \"9\"}}"}, "preview_gate");
    const char* trasiga[] = {
        "{{\"A\", \"1\"}",                  // obalanserad klammer
        "{{\"A\", \"1\"}, {\"B\"}, }",      // tomt sista element
        "{{A, \"1\"}}",                     // ociterat namn
        "{{\"A\", \"1\", {}, kanske}}",     // ogiltig bool
        "{{\"A\", \"1\", \"x\"}}",          // choices som sträng
        "{{\"A\", \"1\", {}, true, \"t\", \"extra\"}}",   // sex fält
        "{\"A\", \"B\"}",                   // strängar i stället för rader
        "rader",                            // inget skal alls
    };
    for (const char* t : trasiga) {
        const bool utfördes = bld::applyCall(*w, {"setRows", t},
                                             "preview_gate");
        check(!utfördes && g->rows().size() == 1 &&
                  g->rows()[0].name == "Kvar",
              std::string("setRows: '") + t + "' ⇒ rows() oförändrad",
              std::to_string(g->rows().size()));
    }
}

void fallListStyckeform()
{
    // ListBox och Dropdown bär IDENTISK form — samma namn, samma semantik.
    auto lbW = build("glaze::ListBox", {{"items", "{\"a\", \"b\"}"}});
    auto ddW = build("glaze::Dropdown", {{"items", "{\"a\", \"b\"}"}});
    if (!lbW || !ddW)
        return;
    auto* lb = static_cast<glaze::ListBox*>(lbW.get());
    auto* dd = static_cast<glaze::Dropdown*>(ddW.get());

    bld::applyCall(*lbW, {"addItem", "\"c\""}, "preview_gate");
    bld::applyCall(*ddW, {"addItem", "\"c\""}, "preview_gate");
    check(lb->items().size() == 3 && lb->items()[2] == "c",
          "addItem: ListBox appendar", std::to_string(lb->items().size()));
    check(dd->items().size() == 3 && dd->items()[2] == "c",
          "addItem: Dropdown appendar (samma form)",
          std::to_string(dd->items().size()));

    bld::applyCall(*lbW, {"setItem", "1, \"B\""}, "preview_gate");
    bld::applyCall(*ddW, {"setItem", "1, \"B\""}, "preview_gate");
    check(lb->items()[1] == "B", "setItem: ListBox byter rad", lb->items()[1]);
    check(dd->items()[1] == "B", "setItem: Dropdown byter rad",
          dd->items()[1]);

    // Index utanför intervallet ⇒ no-op; argumentfel ⇒ oförändrat.
    const auto före = lb->items();
    for (const char* a : {"9, \"X\"", "-1, \"X\"", "1", "\"1\", \"X\"",
                          "1, X", "1.5, \"X\"", ""})
        bld::applyCall(*lbW, {"setItem", a}, "preview_gate");
    bld::applyCall(*lbW, {"addItem", "X"}, "preview_gate");
    bld::applyCall(*lbW, {"addItem", "\"a\", \"b\""}, "preview_gate");
    check(lb->items() == före,
          "setItem/addItem: ogiltiga argument ⇒ listan EXAKT oförändrad",
          std::to_string(lb->items().size()));
}

// KOLLISIONEN, previewhalvan: raden ska nå
// `applyCall` — readern får inte göra den till en
// felsemantisk `Prop{checked, "1, true"}`.
void fallListBoxChecked()
{
    auto w = build("glaze::ListBox",
                   {{"items", "{\"a\", \"b\", \"c\"}"},
                    {"showChecks", "true"}});
    if (!w)
        return;
    auto* lb = static_cast<glaze::ListBox*>(w.get());

    check(bld::applyCall(*w, {"setChecked", "1, true"}, "preview_gate") &&
              lb->isChecked(1) && !lb->isChecked(0),
          "setChecked: raden sätter krysset på rätt rad",
          lb->isChecked(1) ? "1" : "0");

    // OUT OF RANGE: ingen krasch, inget kryss (widgetens egen no-op).
    check(bld::applyCall(*w, {"setChecked", "9, true"}, "preview_gate") &&
              !lb->isChecked(9),
          "setChecked: index utanför intervallet ⇒ ingen krasch, inget "
          "kryss", "");

    // Argumentfel ⇒ oförändrat tillstånd.
    for (const char* a : {"1", "1, kanske", "\"1\", true", "1, true, 2", ""})
        check(!bld::applyCall(*w, {"setChecked", a}, "preview_gate"),
              std::string("setChecked: '") + a + "' avvisas", a);
    check(lb->isChecked(1) && !lb->isChecked(0) && !lb->isChecked(2),
          "setChecked: kryssen oförändrade efter argumentfelen", "");
}

// nameWidth — widget-änden av rörledningen.
void fallNameWidth()
{
    auto w = build("glaze::PropertyGrid", {{"nameWidth", "120"}});
    if (!w)
        return;
    check(gridAv(w)->nameWidth() == 120.0f,
          "nameWidth: applyProp når setNameWidth",
          std::to_string(gridAv(w)->nameWidth()));
}

// restoreGlyph — fältet står i
// settertabellen och settern finns, men `setProp` måste känna det, inte bara kind/shape.
void fallRestoreGlyph()
{
    auto w = build("glaze::CaptionButton",
                   {{"kind", "\"maximize\""}, {"restoreGlyph", "true"}});
    if (!w)
        return;
    auto* cb = static_cast<glaze::CaptionButton*>(w.get());
    check(cb->isRestoreGlyph(), "restoreGlyph: applyProp når settern", "");
    bld::applyProp(*w, {"restoreGlyph", "false"}, "preview_gate");
    check(!cb->isRestoreGlyph(), "restoreGlyph: false-vägen också", "");
}

// LABEL::setColor. Grenen anropar ENDAST `Label::setColor` — men den
// settern sätter `Role::Custom` SJÄLV, så färgen syns.
//
// Ett separat anrop vore TVÅ, och bara det ena går genom verktygsvägen:
// `setRole(Role::Custom)` round-trippar som `Prop{role, …}` → `applyProp`
// → `parseColor`, som känner text/dim/accent/titlebar/heading och
// `#rrggbb` men INTE ordet "custom" — appen skulle visa rött, previewn temats
// textfärg. Därför ligger paret i settern.
//
// ⚠ GRENEN GÖR INGET EXTRA. Att previewn sätter rollen vore
// ett regel 8-brott i motsatt riktning; att C++-METODEN gör det är en
// API-ändring som BÅDA vägarna ärver. Skillnaden är hela poängen.
void fallLabelSetColor()
{
    auto lika = [](gfx::Color c, int r, int g, int b, int a) {
        return c.r == r && c.g == g && c.b == b && c.a == a;
    };

    // Roll `Text` (default) → setColor sätter färgen OCH aktiverar den.
    {
        auto w = build("glaze::Label", {{"text", "\"Hej\""}});
        if (!w)
            return;
        auto* l = static_cast<glaze::Label*>(w.get());
        check(bld::applyCall(*w, {"setColor", "gfx::Color{40, 90, 180, 255}"},
                             "preview_gate") &&
                  lika(l->color(), 40, 90, 180, 255),
              "setColor: RGBA sätts ur literalen", "");
        check(l->role() == glaze::Label::Role::Custom,
              "setColor: rollen blir Custom — färgen SYNS utan andra anrop",
              std::to_string(int(l->role())));
    }

    // Roll `Dim` → samma sak. Ett explicit temaval övertrumfas av en
    // explicit färg, för det är den senare satsen som gäller.
    {
        auto w = build("glaze::Label", {{"role", "glaze::Label::Role::Dim"}});
        if (!w)
            return;
        auto* l = static_cast<glaze::Label*>(w.get());
        bld::applyCall(*w, {"setColor", "gfx::Color{1, 2, 3, 4}"},
                       "preview_gate");
        check(l->role() == glaze::Label::Role::Custom &&
                  lika(l->color(), 1, 2, 3, 4),
              "setColor: Dim → setColor ⇒ Custom (satsordningen gäller)",
              std::to_string(int(l->role())));
    }

    // SATSORDNINGEN bär semantik åt BÅDA håll — ett `setRole` EFTER en
    // färg tar tillbaka temarollen, precis som i den kompilerade appen.
    // Färgen ligger kvar i `color_` men används inte, vilket är exakt vad
    // `Role::Custom` betyder.
    {
        auto w = build("glaze::Label", {});
        auto* l = static_cast<glaze::Label*>(w.get());
        bld::applyCall(*w, {"setColor", "gfx::Color{9, 9, 9, 255}"},
                       "preview_gate");
        check(l->role() == glaze::Label::Role::Custom &&
                  lika(l->color(), 9, 9, 9, 255),
              "setColor: aktiverar sig själv från default-rollen", "");

        bld::applyProp(*w, {"role", "glaze::Label::Role::Dim"},
                       "preview_gate");
        check(l->role() == glaze::Label::Role::Dim &&
                  lika(l->color(), 9, 9, 9, 255),
              "setRole(Dim) EFTER ⇒ temarollen vinner, färgen ligger kvar",
              std::to_string(int(l->role())));

        // ÅTERAKTIVERINGEN — en FRISTÅENDE `setRole(Custom)`. Känner
        // `parseColor` inte ordet
        // "custom" ritar appen `color_` och previewn behåller `Dim`.
        // `setColor` MASKERAR fallet (den vanliga vägen sätter
        // ju rollen) men löser det inte.
        bld::applyProp(*w, {"role", "glaze::Label::Role::Custom"},
                       "preview_gate");
        check(l->role() == glaze::Label::Role::Custom &&
                  lika(l->color(), 9, 9, 9, 255),
              "FRISTÅENDE setRole(Custom) återaktiverar den satta färgen",
              std::to_string(int(l->role())));
    }

    // FÄRRE ÄN FYRA KANALER — laglig C++: `gfx::Color`
    // är ett aggregat med standardvärden, så de utelämnade kanalerna blir
    // r = g = b = 0 och a = 255, precis som i den kompilerade appen. Förväntat är
    // talen C++ ger, utskrivna — inte parserns egen tabell. Varje fall
    // börjar från en annan färg, så en kanal som INTE skrivs om syns.
    {
        auto w = build("glaze::Label", {});
        auto* l = static_cast<glaze::Label*>(w.get());
        struct Fall {
            const char* literal;
            int         r, g, b, a;
            const char* vad;
        };
        const Fall fall[] = {
            {"gfx::Color{}", 0, 0, 0, 255, "NOLL kanaler ⇒ opak svart"},
            {"gfx::Color{ }", 0, 0, 0, 255, "noll kanaler med blanksteg"},
            {"gfx::Color{51}", 51, 0, 0, 255, "EN kanal ⇒ r, resten standard"},
            {"gfx::Color{51, 102, 255}", 51, 102, 255, 255,
             "TRE kanaler ⇒ alfan 255"},
            {"gfx::Color{51, 102, 255, 128}", 51, 102, 255, 128,
             "FYRA kanaler som förut"},
            {"gfx::Color{51, 102, 255,}", 51, 102, 255, 255,
             "avslutande komma efter tre (laglig C++)"},
            {"gfx::Color{ 1 , 2 }", 1, 2, 0, 255, "två kanaler med blanksteg"},
        };
        for (const Fall& f : fall) {
            bld::applyCall(*w, {"setColor", "gfx::Color{9, 9, 9, 9}"},
                           "preview_gate");
            // Anropet och avläsningen FÖRE `check` — argumentens ordning är
            // ospecificerad, och utskriften ska visa färgen EFTER anropet.
            const bool utförd =
                bld::applyCall(*w, {"setColor", f.literal}, "preview_gate");
            const gfx::Color c = l->color();
            check(utförd && lika(c, f.r, f.g, f.b, f.a) &&
                      l->role() == glaze::Label::Role::Custom,
                  std::string("setColor: '") + f.literal + "' — " + f.vad,
                  std::string(utförd ? "utförd " : "avvisad ") +
                      std::to_string(c.r) + "," + std::to_string(c.g) + "," +
                      std::to_string(c.b) + "," + std::to_string(c.a));
        }
    }

    // NEGATIVA FALL — obligatoriska. `gfx::Color` bär BYTEkanaler, så en
    // allmän floatparser eller en okontrollerad cast hade tolkat raden
    // annorlunda än C++ gör. Fem kanaler, ett ensamt komma och ett dubbelt
    // är ogiltig C++ också.
    {
        auto w = build("glaze::Label", {});
        auto* l = static_cast<glaze::Label*>(w.get());
        bld::applyCall(*w, {"setColor", "gfx::Color{7, 7, 7, 255}"},
                       "preview_gate");
        const char* trasiga[] = {
            "gfx::Color{12.5, 0, 0, 255}",     // flyttal
            "gfx::Color{12.5}",                // fel typ: flyttal (avsmalning)
            "gfx::Color{\"röd\"}",             // fel typ: sträng
            "gfx::Color{-1, 0, 0, 255}",       // negativ kanal
            "gfx::Color{256, 0, 0, 255}",      // över 255
            "gfx::Color{0, 0, 0, 255, 1}",     // FEM kanaler
            "gfx::Color{0, 0, 0, 255, 1,}",    // fem med avslutande komma
            "gfx::Color{,}",                   // ensamt komma
            "gfx::Color{1,,}",                 // dubbelt komma
            "gfx::Color{, 1}",                 // tomt första element
            "glaze::Color{0, 0, 0, 255}",      // annat kvalificerat namn
            "gfx::Color{40 + 1, 0, 0, 255}",   // uttryck, inte literal
            "gfx::Color(0, 0, 0, 255)",        // parenteser, inte klamrar
            "temaFarg",                        // inget skal alls
            "",                                // inga argument
        };
        for (const char* t : trasiga)
            check(!bld::applyCall(*w, {"setColor", t}, "preview_gate") &&
                      lika(l->color(), 7, 7, 7, 255),
                  std::string("setColor: '") + t +
                      "' ⇒ logg + oförändrad färg", t);
    }

    // TEXTVÄGEN ger samma slutresultat, men på ETT anrop:
    // `setProp("color", "#rrggbb")` gör bara `setColor(...)`, och
    // den settern sätter färgen OCH rollen atomiskt — ordningen bor i
    // settern, så de två kan inte glida isär.
    {
        auto w = build("glaze::Label", {});
        auto* l = static_cast<glaze::Label*>(w.get());
        l->setProp("color", "#204090");
        check(l->role() == glaze::Label::Role::Custom &&
                  lika(l->color(), 0x20, 0x40, 0x90, 255),
              "setProp(color, #rrggbb): textvägen sätter BÅDE färg och "
              "Custom", std::to_string(int(l->role())));
    }
}

// ================================================ PREVIEWVANDRINGEN
//
// Felet "kroken anropas aldrig" är STRUKTURELLT — alltså
// grindbar utan en enda pixel. Pixeltest behövs bara för remsGEOMETRI och
// FÄRG.
//
// Punkt 2 (att kroken ligger UTANFÖR barnklippet) kräver mer än en
// instrumenterad Widget: `pushClip`/`popClip` är ICKE-virtuella på
// `gfx::Renderer`, så en widget-räknare kan bevisa ordningen mellan draw,
// barn och krok — men inte OBSERVERA klippstacken. `applyClip` är
// däremot virtuell, och en minimal test-renderer som loggar den ger det
// fullständiga förloppet.

struct KlippRenderer : gfx::NullRenderer {
    std::vector<std::string>* logg = nullptr;

protected:
    // Loggar BÅDE att klippstacken rörts och de RUNDADE maskernas radier
    //: ett rundat klipp som previewvandringen tappar
    // syns då som ett saknat `r=`-suffix i den ordagranna sekvensen.
    void applyClip(const gfx::Rectf* r, const gfx::RoundedClip* masks,
                   std::size_t count) override
    {
        if (!r) {
            logg->push_back("klipp-av");
            return;
        }
        std::string rad = "klipp";
        for (std::size_t i = 0; i < count; ++i) {
            char buf[32];
            std::snprintf(buf, sizeof buf, " r=%g", masks[i].radius);
            rad += buf;
        }
        logg->push_back(rad);
    }
};

struct SpårWidget : glaze::Widget {
    std::vector<std::string>* logg = nullptr;
    std::string               id;
    bool                      klipper = false;
    float                     radie   = 0.0f;   // FYSISKA px, som kontraktet

    void draw(const glaze::Ctx&) override { logg->push_back("draw " + id); }
    void drawAfterChildren(const glaze::Ctx&) override
    {
        logg->push_back("after " + id);
    }
    bool  clipsChildren() const override { return klipper; }
    float clipRadius(const glaze::Ctx&) const override { return radie; }
};

std::string ihop(const std::vector<std::string>& v)
{
    std::string s;
    for (const std::string& x : v)
        s += (s.empty() ? "" : " | ") + x;
    return s;
}

void fallPreviewVandring()
{
    std::vector<std::string> logg;
    KlippRenderer            r;
    r.logg = &logg;

    glaze::Ctx c{};
    c.r     = &r;
    c.scale = 1.0f;
    c.ui    = glaze::UI::Slate;
    c.style = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme = &glaze::Theme::slateDefault().light;
    c.area  = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};

    auto gör = [&logg](const char* id, bool klipper) {
        auto w     = std::make_unique<SpårWidget>();
        w->logg    = &logg;
        w->id      = id;
        w->klipper = klipper;
        w->width   = 100.0f;
        w->height  = 50.0f;
        return w;
    };

    // 1+2. Kroken anropas EFTER barnen och UTANFÖR widgetens eget
    //      barnklipp. Förväntad sekvens, ordagrant:
    //      draw förälder → apply clip → draw barn → apply föregående clip
    //      → after förälder.
    {
        logg.clear();
        auto f = gör("F", true);
        auto b = gör("B", false);
        f->adopt(*b);
        bld::drawPreviewTree(*f, c);
        // Barnets EGEN krok ligger INNANFÖR förälderns klipp (den är
        // barnets overlay, inte förälderns) — det är samma ordning som
        // `AppWindow::drawTree` ger. FÖRÄLDERNS krok ligger efter
        // `klipp-av`, alltså utanför sitt eget barnklipp.
        check(ihop(logg) ==
                  "draw F | klipp | draw B | after B | klipp-av | after F",
              "vandring: after-kroken körs efter barnen och utanför klippet",
              ihop(logg));
    }

    // 1b. DEN TOMMA REKTEN, som i `AppWindow::drawTree`: en förälder
    //     med bredd 0 ritar varken sig själv eller sin efter-krok — men
    //     barnet med egen yta ritas. Sabotage: spärren bort ur
    //     `drawPreviewTree` (draw F och after F i sekvensen).
    {
        logg.clear();
        auto f   = gör("F", false);
        f->width = 0.0f;
        auto b   = gör("B", false);
        f->adopt(*b);
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) == "draw B | after B",
              "vandring: en tom rekt ritas inte — barnen vandras ändå",
              ihop(logg));
    }

    // 3. DOLT DELTRÄD ritas inte alls — varken widgeten, barnen eller
    //    kroken. Vakten saknades helt i den privata kopian, så barnen till
    //    en dold Panel ritades i canvasen men inte i appen; och utan den
    //    hade den nya raden dessutom ritat overlay-remsor på en DOLD panel
    //    (`Panel::drawAfterChildren` kollar inte `visible_`).
    {
        logg.clear();
        auto f = gör("F", true);
        auto b = gör("B", false);
        f->adopt(*b);
        f->hide();
        bld::drawPreviewTree(*f, c);
        check(logg.empty(), "vandring: DOLD widget ritar varken sig själv, "
                            "barnen eller kroken", ihop(logg));
    }

    // 4. Ett DOLT BARN i en synlig förälder — föräldern ritas hela vägen,
    //    barnet inte alls.
    {
        logg.clear();
        auto f = gör("F", true);
        auto b = gör("B", false);
        f->adopt(*b);
        b->hide();
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) == "draw F | klipp | klipp-av | after F",
              "vandring: dolt BARN hoppas över, föräldern ritas klart",
              ihop(logg));
    }

    // 5. Utan klippning finns inga applyClip-anrop alls — så klippraderna
    //    ovan mäter verkligen klippstacken och inte något annat.
    {
        logg.clear();
        auto f = gör("F", false);
        auto b = gör("B", false);
        f->adopt(*b);
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) == "draw F | draw B | after B | after F",
              "vandring: utan clipsChildren rörs klippstacken aldrig",
              ihop(logg));
    }

    // 6. RUNDAT BARNKLIPP — regel 8-pariteten. Previewn måste
    //    UTFÖRA klippet, inte bara kunna uttrycka det: `drawPreviewTree`
    //    och `AppWindow::drawTree` anropar samma `Widget`-par
    //    (`clipRect`/`clipRadius`), så tappar previewvandringen radien
    //    ritar canvasen fyrkantiga hörn där appen ritar rundade — precis
    //    den klass av tyst WYSIWYG-brott previewvandringen finns för.
    //    Radien står ORDAGRANT i den förväntade sekvensen; en tappad
    //    `w.clipRadius(c)` ger "klipp" utan suffix och fäller raden.
    {
        logg.clear();
        auto f   = gör("F", true);
        auto b   = gör("B", false);
        f->radie = 12.0f;
        f->adopt(*b);
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) ==
                  "draw F | klipp r=12 | draw B | after B | klipp-av | after F",
              "vandring: RUNDAT klipp når renderaren med sin radie",
              ihop(logg));
    }

    // 7. NÄSTLAT: två rundade nivåer ⇒ BÅDA maskerna är aktiva samtidigt,
    //    yttersta först. Beviset att stacken samlar masker och inte bara
    //    bär den innersta — och att en rektangulär nivå emellan inte
    //    konsumerar en maskplats.
    {
        logg.clear();
        auto y  = gör("Y", true);   // ytterst, rundad
        auto m  = gör("M", true);   // mitten, REKTANGULÄR
        auto i  = gör("I", true);   // innerst, rundad
        auto bl = gör("BL", false);
        y->radie = 12.0f;
        i->radie = 4.0f;
        y->adopt(*m);
        m->adopt(*i);
        i->adopt(*bl);
        bld::drawPreviewTree(*y, c);
        check(ihop(logg) ==
                  "draw Y | klipp r=12 | draw M | klipp r=12 | draw I | "
                  "klipp r=12 r=4 | draw BL | after BL | klipp r=12 | "
                  "after I | klipp r=12 | after M | klipp-av | after Y",
              "vandring: nästlade masker samlas yttersta först, och en "
              "rektangulär nivå emellan tar ingen maskplats",
              ihop(logg));
    }

    // 8. DET FRÄMRE BARNET
    //    ritas SIST, oavsett barnordningen — precis som i
    //    `AppWindow::drawTree`. Dockningsvärdens öppna overlay hade annars
    //    hamnat UNDER huvudytan i previewn. Ett svar som inte är ett av
    //    barnen ignoreras, och barnordningen står orörd.
    {
        struct FramSpår : SpårWidget {
            glaze::Widget* fram = nullptr;
            glaze::Widget* frontChild() const override { return fram; }
        };
        logg.clear();
        auto f     = std::make_unique<FramSpår>();
        f->logg    = &logg;
        f->id      = "F";
        f->width   = 100.0f;
        f->height  = 50.0f;
        auto a     = gör("A", false);
        auto b     = gör("B", false);
        auto x     = gör("X", false);   // inget barn
        f->adopt(*a);
        f->adopt(*b);
        f->fram = a.get();
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) ==
                  "draw F | draw B | after B | draw A | after A | after F",
              "vandring: det främre barnet ritas SIST",
              ihop(logg));
        check(f->children().size() == 2 && f->children()[0] == a.get(),
              "vandring: …och barnordningen står orörd", "");
        logg.clear();
        f->fram = x.get();
        bld::drawPreviewTree(*f, c);
        check(ihop(logg) ==
                  "draw F | draw A | after A | draw B | after B | after F",
              "vandring: ett främre barn som inte är ett barn ignoreras",
              ihop(logg));
    }
}

// ============================================== DRAG-AND-DROP
//
// Regel 8 åt båda hållen för de två egenskaperna: `.glazeui`-formen
// (setter-anropet) normaliseras av readern till fältet, UTFÖRS av previewn
// och når widgeten — och event-kopplingarna i båda formerna blir EVENTS och
// inte opaka fältrader. Kedjan mäts HEL, från text till widgetens tillstånd,
// för det är exakt glappet mellan modell och widget som annars gömmer
// fel.
//
// VAD DEN INTE MÄTER: själva draget — tröskeln, målvalet, ordningen. Det är
// beteende i ett fönster och bor i tests/dnd_gate.cpp.
void fallDragDrop()
{
    auto zon = build("glaze::Panel", {{"allowDrop", "true"}});
    auto kalla = build("glaze::Label", {{"allowDrag", "true"}});
    if (!zon || !kalla)
        return;
    check(zon->allowDrop() && !zon->allowDrag(),
          "dnd: allowDrop når panelen (och bara den)", "");
    check(kalla->allowDrag() && !kalla->allowDrop(),
          "dnd: allowDrag når etiketten (och bara den)", "");
    bld::applyProp(*zon, {"allowDrop", "false"}, "preview_gate");
    check(!zon->allowDrop(), "dnd: allowDrop(false) släcker igen", "");

    // KEDJAN: text → reader → modell → previewns satsväg → tillstånd.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::Panel zon;\n"
        "    glaze::Panel zon2;\n"
        "    glaze::Label kalla;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    // ALLA SEX eventen i den KORTA formen — den enda som kräver att readern
    // KÄNNER namnet (`isKnownEvent`). Ett utelämnat namn blir en opak
    // fältrad, och då stämmer inte räkningen nedan. Fullformen är generisk
    // och prövas på två av dem.
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    zon.name = \"zon\";\n"
        "    zon.setAllowDrop(true);\n"
        "    zon2.name = \"zon2\";\n"
        "    kalla.name = \"kalla\";\n"
        "    kalla.setAllowDrag(true);\n"
        "    zon.onDragEnter = [this] { zonIn(); };\n"
        "    zon.onDragOver = [this] { zonOver(); };\n"
        "    zon.onDragLeave = [this] { zonUt(); };\n"
        "    zon.onDrop = [this] { zonSlapp(); };\n"
        "    kalla.onDragStart = [this] { kallaStart(); };\n"
        "    kalla.onDragEnd = [this] { kallaSlut(); };\n"
        "    zon2.onDrop = [this](glaze::Widget* s, glaze::DragArgs& e) { "
        "zon2Drop(s, e); };\n"
        "    zon2.onDragOver = [this](glaze::Widget* s, glaze::DragArgs& e) { "
        "zon2Over(s, e); };\n"
        "    root().adopt(zon);\n"
        "    root().adopt(zon2);\n"
        "    root().adopt(kalla);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "dnd: fixturen läses", err);
    if (!läst)
        return;
    int props = 0, events = 0, korta = 0;
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Prop &&
            (s.name == "allowDrop" || s.name == "allowDrag"))
            ++props;
        if (s.kind == bld::Stmt::Kind::Event) {
            ++events;
            if (s.shortForm)
                ++korta;
        }
    }
    check(props == 2, "dnd: setter-anropen normaliseras till fält (Prop)",
          std::to_string(props));
    check(events == 8 && korta == 6,
          "dnd: alla sex eventen blir EVENTS i den korta formen (och två i "
          "den fulla)",
          std::to_string(events) + "/" + std::to_string(korta));

    auto z2 = build("glaze::Panel", {});
    auto k2 = build("glaze::Label", {});
    if (!z2 || !k2)
        return;
    for (const bld::Stmt& s : m.stmts) {
        if (s.target == "zon")
            bld::applyStmt(*z2, s, "preview_gate");
        else if (s.target == "kalla")
            bld::applyStmt(*k2, s, "preview_gate");
    }
    check(z2->allowDrop() && k2->allowDrag(),
          "dnd: previewns satsväg UTFÖR båda egenskaperna", "");

    // En ANVÄNDARTYP som ärver en glaze-typ behåller sin anropsform (den är
    // inte klassad), och previewns setter-fallback ska ändå utföra den.
    check(!bld::isSetterField("MinPanel", "allowDrop"),
          "dnd: okänd användartyp behåller Call-formen", "");
    auto z3 = build("glaze::Panel", {});
    if (!z3)
        return;
    check(bld::applyCall(*z3, {"setAllowDrop", "true"}, "preview_gate") &&
              z3->allowDrop(),
          "dnd: setter-fallbacken utför Call-formen", "");
}

// =================================== COMMIT, FOKUS OCH MARKERING
//
// Regel 8 åt båda hållen. EVENTEN: `onCommit`, `onFocus`, `onFocusLost` — och
// `onCheck` — ska vara KOPPLINGAR i båda formerna, inte
// opaka fältrader; den korta formen kräver att readern KÄNNER namnet. Previewn
// kör aldrig användarkod, så det som mäts är klassningen. MARKERINGEN:
// `selectAll()`/`setSelection(b, e)` ska UTFÖRAS på de fem typer som bär den
// (TextInput, TextArea och kompositerna) — mätt på widgetens markering, som
// sätts fast den inte syns utan fokus — och en trasig rad ska bli OPAK med
// markeringen orörd (atomiskt), aldrig en halv utförd rad.
void fallCommitFokusMarkering()
{
    // KEDJAN: text → reader → modell. Alla fyra namnen i den KORTA formen och
    // tre av dem i den fulla (med sina args-typer).
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::TextInput falt;\n"
        "    glaze::ListBox lista;\n"
        "    glaze::DataGrid tabell;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    falt.name = \"falt\";\n"
        "    falt.selectAll();\n"
        "    falt.setSelection(1, 3);\n"
        "    lista.name = \"lista\";\n"
        "    tabell.name = \"tabell\";\n"
        "    falt.onCommit = [this] { klart(); };\n"
        "    falt.onFocus = [this] { in(); };\n"
        "    falt.onFocusLost = [this] { ut(); };\n"
        "    lista.onCheck = [this] { kryss(); };\n"
        "    falt.onFocus = [this](glaze::Widget* s, glaze::FocusArgs& e) { "
        "in2(s, e); };\n"
        "    lista.onCommit = [this](glaze::Widget* s, glaze::ChangeArgs& e) { "
        "val(s, e); };\n"
        "    tabell.onCommit = [this](glaze::Widget* s, glaze::CellChangeArgs& "
        "e) { cell(s, e); };\n"
        "    root().adopt(falt);\n"
        "    root().adopt(lista);\n"
        "    root().adopt(tabell);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "commit/fokus: fixturen läses", err);
    if (!läst)
        return;
    int events = 0, korta = 0, opaka = 0, anrop = 0;
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Event) {
            ++events;
            korta += s.shortForm ? 1 : 0;
        }
        if (s.kind == bld::Stmt::Kind::Prop && s.name.rfind("on", 0) == 0)
            ++opaka;
        if (s.kind == bld::Stmt::Kind::Call)
            ++anrop;
    }
    check(events == 7 && korta == 4 && opaka == 0,
          "commit/fokus: onCommit, onFocus, onFocusLost och onCheck blir "
          "EVENTS — fyra korta, tre fulla, ingen opak",
          std::to_string(events) + "/" + std::to_string(korta) + "/" +
              std::to_string(opaka));
    check(anrop == 2, "commit/fokus: selectAll och setSelection är Call-satser",
          std::to_string(anrop));

    // PREVIEWN UTFÖR markeringen på varje typ som bär den.
    const char* const typer[] = {"glaze::TextInput", "glaze::TextArea",
                                 "glaze::SpinBox", "glaze::DatePicker",
                                 "glaze::FilePicker"};
    for (const char* typ : typer) {
        auto w = build(typ, {});
        if (!w)
            continue;
        // Varje typ får en text på mer än tre byte, i fältets eget format.
        if (auto* f = dynamic_cast<glaze::TextInput*>(w.get()))
            f->setText("abcdef");
        if (auto* a = dynamic_cast<glaze::TextArea*>(w.get()))
            a->setText("abcdef");
        if (auto* s = dynamic_cast<glaze::SpinBox*>(w.get())) {
            s->setMax(100000);   // standardintervallet slutar på 100
            s->setValue(12345);
        }
        if (auto* d = dynamic_cast<glaze::DatePicker*>(w.get()))
            d->setDate(2026, 10, 11);
        if (auto* p = dynamic_cast<glaze::FilePicker*>(w.get()))
            p->setPath("abcdef");

        // Markeringen och textens längd läses ur FÄLTET — kompositerna för
        // den vidare dit.
        std::size_t b = 99, e = 99, n = 0;
        const auto  läs = [&] {
            if (auto* a = dynamic_cast<glaze::TextArea*>(w.get())) {
                b = a->selBegin();
                e = a->selEnd();
                n = a->text().size();
                return true;
            }
            glaze::TextInput* f = dynamic_cast<glaze::TextInput*>(w.get());
            for (glaze::Widget* barn : w->children())
                if (!f)
                    f = dynamic_cast<glaze::TextInput*>(barn);
            if (!f)
                return false;
            b = f->selBegin();
            e = f->selEnd();
            n = f->text().size();
            return true;
        };
        const auto spann = [&] {
            return std::to_string(b) + ".." + std::to_string(e) + " av " +
                   std::to_string(n);
        };
        const bool ok1 = bld::applyCall(*w, {"setSelection", "1, 3"},
                                        "preview_gate");
        check(ok1 && läs() && b == 1 && e == 3,
              std::string("markering: ") + typ +
                  " — setSelection(1, 3) är KÄND och UTFÖRS", spann());
        const bool ok2 = bld::applyCall(*w, {"selectAll", ""}, "preview_gate");
        check(ok2 && läs() && n > 3 && b == 0 && e == n,
              std::string("markering: ") + typ + " — selectAll() utförs",
              spann());
        // ATOMISKT: fel antal eller ett argument som inte är ett heltal ⇒
        // opak rad och markeringen ORÖRD.
        const bool ok3 = bld::applyCall(*w, {"setSelection", "1"},
                                        "preview_gate");
        const bool ok4 = bld::applyCall(*w, {"setSelection", "1, x"},
                                        "preview_gate");
        const bool ok5 = bld::applyCall(*w, {"selectAll", "1"}, "preview_gate");
        check(!ok3 && !ok4 && !ok5 && läs() && b == 0 && e == n,
              std::string("markering: ") + typ +
                  " — trasiga rader blir OPAKA och lämnar markeringen orörd",
              spann());
    }
}

// =================================== INMATNINGEN
//
// Regel 8 för eventen: tangenterna på VARJE widget (`onKeyDown`, som
// inte bara finns på ritytan, och `onKeyUp`) — på ett TextArea, ett
// fält som inte är ritytan — och ritytans avbrott `onMouseCancel` och
// `onKeyUp` på en `glaze::Canvas`, alla
// i BÅDA formerna. Den korta formen kräver att readern KÄNNER namnet
// (`isKnownEvent`); utan det blir raden en opak fältrad som round-trippar
// lika byte-exakt. Previewn kör aldrig användarkod, så det som mäts är
// klassningen — och att satsvägen tar emot raderna utan att röra widgeten.
void fallInmatningEvents()
{
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::TextArea meddelande;\n"
        "    glaze::Canvas karta;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    meddelande.name = \"meddelande\";\n"
        "    karta.name = \"karta\";\n"
        "    meddelande.onKeyDown = [this] { ner(); };\n"
        "    meddelande.onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& "
        "e) { ner2(s, e); };\n"
        "    meddelande.onKeyUp = [this] { upp(); };\n"
        "    meddelande.onKeyUp = [this](glaze::Widget* s, glaze::KeyArgs& e) "
        "{ upp2(s, e); };\n"
        "    karta.onMouseCancel = [this] { avbryt(); };\n"
        "    karta.onMouseCancel = [this](glaze::Widget* s, "
        "glaze::CanvasMouseArgs& e) { avbryt2(s, e); };\n"
        "    karta.onKeyUp = [this] { kartaUpp(); };\n"
        "    karta.onKeyUp = [this](glaze::Widget* s, glaze::KeyArgs& e) { "
        "kartaUpp2(s, e); };\n"
        "    root().adopt(meddelande);\n"
        "    root().adopt(karta);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "inmatning: fixturen läses", err);
    if (!läst)
        return;
    int         events = 0, korta = 0, opaka = 0;
    std::string args;
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Event) {
            ++events;
            korta += s.shortForm ? 1 : 0;
            if (!s.shortForm)
                args += " " + s.args;
        }
        if (s.kind == bld::Stmt::Kind::Prop && s.name.rfind("on", 0) == 0)
            ++opaka;
    }
    check(events == 8 && korta == 4 && opaka == 0,
          "inmatning: onKeyDown/onKeyUp på TextArea och onMouseCancel/onKeyUp "
          "på ritytan blir EVENTS — fyra korta, fyra fulla, ingen opak",
          std::to_string(events) + "/" + std::to_string(korta) + "/" +
              std::to_string(opaka));
    check(args == " glaze::KeyArgs glaze::KeyArgs glaze::CanvasMouseArgs "
                  "glaze::KeyArgs",
          "inmatning: de fulla formerna bär sina args-typer", args);

    // SATSVÄGEN: kopplingarna tas emot (ingen opak logg) och utförs inte —
    // widgetarna är orörda (ingen handler satt i previewn).
    auto ta = build("glaze::TextArea", {});
    auto ca = build("glaze::Canvas", {});
    if (!ta || !ca)
        return;
    for (const bld::Stmt& s : m.stmts) {
        if (s.target == "meddelande")
            bld::applyStmt(*ta, s, "preview_gate");
        else if (s.target == "karta")
            bld::applyStmt(*ca, s, "preview_gate");
    }
    auto* kc = dynamic_cast<glaze::Canvas*>(ca.get());
    check(!ta->onKeyDown && !ta->onKeyUp && kc && !kc->onMouseCancel &&
              !kc->onKeyUp,
          "inmatning: previewn kopplar aldrig — handlerfälten står tomma", "");
}

// =========================== APPENS MUS PÅ VARJE WIDGET
//
// Regel 8 för basens tre mushandlers på en vanlig widget — en KNAPP, inte
// ritytan — i BÅDA formerna. Samma mätning som inmatningens: klassningen
// (den korta formen kräver att readern känner namnet) och att satsvägen tar
// emot kopplingarna utan att sätta något (previewn kör ingen användarkod).
void fallAppMusEvents()
{
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::Button knapp;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    knapp.name = \"knapp\";\n"
        "    knapp.onMouseDown = [this] { ner(); };\n"
        "    knapp.onMouseDown = [this](glaze::Widget* s, glaze::MouseArgs& e) "
        "{ ner2(s, e); };\n"
        "    knapp.onMouseUp = [this] { upp(); };\n"
        "    knapp.onMouseUp = [this](glaze::Widget* s, glaze::MouseArgs& e) "
        "{ upp2(s, e); };\n"
        "    knapp.onMouseCancel = [this] { avbryt(); };\n"
        "    knapp.onMouseCancel = [this](glaze::Widget* s, glaze::MouseArgs& "
        "e) { avbryt2(s, e); };\n"
        "    root().adopt(knapp);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "appens mus: fixturen läses", err);
    if (!läst)
        return;
    int         events = 0, korta = 0, opaka = 0;
    std::string args;
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Event) {
            ++events;
            korta += s.shortForm ? 1 : 0;
            if (!s.shortForm)
                args += " " + s.args;
        }
        if (s.kind == bld::Stmt::Kind::Prop && s.name.rfind("on", 0) == 0)
            ++opaka;
    }
    check(events == 6 && korta == 3 && opaka == 0,
          "appens mus: onMouseDown/onMouseUp/onMouseCancel på en knapp blir "
          "EVENTS — tre korta, tre fulla, ingen opak",
          std::to_string(events) + "/" + std::to_string(korta) + "/" +
              std::to_string(opaka));
    check(args == " glaze::MouseArgs glaze::MouseArgs glaze::MouseArgs",
          "appens mus: de fulla formerna bär glaze::MouseArgs", args);

    auto kn = build("glaze::Button", {});
    if (!kn)
        return;
    for (const bld::Stmt& s : m.stmts)
        if (s.target == "knapp")
            bld::applyStmt(*kn, s, "preview_gate");
    check(!kn->onMouseDown && !kn->onMouseUp && !kn->onMouseCancel,
          "appens mus: previewn kopplar aldrig — handlerfälten står tomma", "");
}

// ======================================================= TRÄDET
//
// Regel 8 åt PREVIEW-hållet: trädets former i `.glazeui` ska UTFÖRAS, och
// mätas på WIDGETENS tillstånd — inte på modellen, som är grön per
// konstruktion. Kollektionsanropen (`addNode`/`setNode`/`setNodes`/
// `removeNode`/`moveNode`/`setExpanded`/`setAllowDropInto`) är Call-satser;
// valet (`setSelectedPath`) är ett setter-fält med en STIG som värde.
//
// ATOMICITETEN mäts på samma sätt som PropertyGrids: en trasig rad ska ge
// `false` OCH ett orört träd — aldrig en halv nod eller en påhittad text.
void fallTrad()
{
    auto w = build("glaze::TreeView", {});
    if (!w)
        return;
    auto* tv = dynamic_cast<glaze::TreeView*>(w.get());
    check(tv != nullptr, "träd: registret ger en riktig glaze::TreeView", "");
    if (!tv)
        return;

    auto anrop = [&](const char* metod, const char* args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };

    // ---- addNode: förälderns STIG först, därefter texten, valfri bild.
    check(anrop("addNode", "{}, \"Dokument\""), "träd: addNode({}, …) utförs");
    check(anrop("addNode", "{0}, \"Rapport\", {}"),
          "träd: addNode med {}-bild utförs");
    check(anrop("addNode", "{0}, \"Bild\""), "träd: andra barnet");
    check(anrop("addNode", "{}, \"Musik\""), "träd: andra toppnoden");
    check(tv->nodeCount({}) == 2 && tv->nodeCount({0}) == 2 &&
              tv->nodeText({0, 1}) == "Bild",
          "träd: noderna hamnade på rätt nivå",
          std::to_string(tv->nodeCount({})) + "/" +
              std::to_string(tv->nodeCount({0})));

    // ---- tr-texten — TOKENET sätts, inte en fryst sträng.
    glaze::setLanguage("sv");
    check(anrop("addNode", "{1}, glaze::tr(2)"), "träd: tr-text utförs");
    check(tv->nodeTextToken({1, 0}).id() == 2 && tv->nodeText({1, 0}) == "Avbryt",
          "träd: tr-noden bär id:t", tv->nodeText({1, 0}));

    // ---- setNode ersätter nodens EGNA fält — barnen står kvar.
    check(anrop("setNode", "{0}, \"Dokument (2)\""), "träd: setNode utförs");
    check(tv->nodeText({0}) == "Dokument (2)" && tv->nodeCount({0}) == 2,
          "träd: setNode byter texten och lämnar barnen", tv->nodeText({0}));

    // ---- setExpanded + valet (setter-fältet, TYPAT).
    check(anrop("setExpanded", "{0}, true") && tv->isExpanded({0}),
          "träd: setExpanded({0}, true) fäller ut");
    bld::applyProp(*w, {"selectedPath", "{0, 1}"}, "preview_gate");
    check(tv->selectedPath() == glaze::TreePath{0, 1},
          "träd: selectedPath-raden når valet");
    bld::applyProp(*w, {"selectedPath", "{-1}"}, "preview_gate");
    check(tv->selectedPath() == glaze::TreePath{0, 1},
          "träd: en ogiltig stig är OPAK — valet ORÖRT");

    // ---- moveNode i FÖRE-flytten-koordinater, och valet FÖLJER noden.
    check(anrop("moveNode", "{0, 1}, {}, 0"), "träd: moveNode utförs");
    check(tv->nodeText({0}) == "Bild" && tv->nodeCount({1}) == 1 &&
              tv->selectedPath() == glaze::TreePath{0},
          "träd: noden flyttade till toppen, och valet följde med",
          tv->nodeText({0}));
    check(anrop("removeNode", "{0}") && tv->nodeCount({}) == 2 &&
              tv->selectedPath().empty(),
          "träd: removeNode tar bort noden och släpper valet");

    // ---- setNodes — den REKURSIVA aggregatliteralen.
    check(anrop("setNodes",
                "{{\"Rot\", {{\"A\"}, {\"B\", {}, {}, true}}, {}, true}, "
                "{glaze::tr(1)}}"),
          "träd: setNodes med nästlad literal utförs");
    check(tv->nodeCount({}) == 2 && tv->nodeCount({0}) == 2 &&
              tv->isExpanded({0}) && tv->isExpanded({0, 1}) &&
              !tv->isExpanded({0, 0}) && tv->nodeTextToken({1}).id() == 1,
          "träd: setNodes gav struktur, utfällning och tr-token");

    // ---- AVSLUTANDE KOMMA: giltig C++ på yttersta
    //      och nästlade nivån — utförs, inte opak. Ett ENSAMT komma är fel.
    check(anrop("setNodes", "{{\"X\", {{\"Y\"},}},}") && tv->nodeCount({}) == 1 &&
              tv->nodeCount({0}) == 1,
          "träd: setNodes med avslutande komma på båda nivåerna utförs");
    check(!anrop("setNodes", "{,}"), "träd: ett ensamt komma ⇒ opak");
    check(anrop("setNodes",
                "{{\"Rot\", {{\"A\"}, {\"B\", {}, {}, true}}, {}, true}, "
                "{glaze::tr(1)}}"),
          "träd: (setNodes återställt för fallen nedan)");

    // ---- ATOMICITETEN: trasiga rader ⇒ false och ett ORÖRT träd.
    const int före = tv->nodeCount({});
    check(!anrop("addNode", "{0, x}, \"a\""), "träd: trasig stig ⇒ opak");
    check(!anrop("addNode", "{}, namn"), "träd: oparsbar text ⇒ opak");
    check(!anrop("addNode", "{}"), "träd: för få argument ⇒ opak");
    check(!anrop("setNodes", "{{\"a\", {{namn}}}}"),
          "träd: trasig NÄSTLAD nod ⇒ hela setNodes opak");
    check(!anrop("setExpanded", "{0}, kanske"), "träd: ogiltig flagga ⇒ opak");
    check(!anrop("moveNode", "{0}, {}"), "träd: moveNode utan index ⇒ opak");
    check(tv->nodeCount({}) == före && tv->nodeText({0}) == "Rot",
          "träd: …och trädet är ORÖRT efter alla trasiga rader",
          std::to_string(tv->nodeCount({})));

    // ---- SLÄPPREGELN: den platta satsen och massformens femte
    //      fält ger SAMMA tillstånd på widgeten.
    check(anrop("setAllowDropInto", "{0, 1}, false") &&
              !tv->allowDropInto({0, 1}) && tv->allowDropInto({0, 0}) &&
              tv->allowDropInto({0}),
          "träd: setAllowDropInto({0, 1}, false) gör just den noden till ett "
          "löv för släppet");
    check(!anrop("setAllowDropInto", "{0, 0}, kanske") &&
              !anrop("setAllowDropInto", "{0, 0}") &&
              tv->allowDropInto({0, 0}),
          "träd: setAllowDropInto med ogiltig flagga eller utan flagga ⇒ "
          "opak, noden orörd");
    check(anrop("setNodes",
                "{{\"Mapp\", {{\"fil.txt\", {}, {}, false, false}}, {}, true}}") &&
              tv->allowDropInto({0}) && !tv->allowDropInto({0, 0}) &&
              tv->isExpanded({0}),
          "träd: massformens FEMTE fält är samma regel");
    // ---- FLERVAL, KRYSS OCH FÄRG: massformens fält 6
    //      och 7 — positionellt, DESIGNERAT och med typnamnet — ger samma
    //      tillstånd som de platta satserna, och ett ÅTTONDE fält är ingen
    //      TreeNode.
    check(anrop("setNodes",
                "{{\"a\", {}, {}, false, false, true, gfx::Color{200, 40, 40}}, "
                "{.text = \"b\", .checked = true}, "
                "glaze::TreeNode{\"c\", {}, {}, false, true, false, "
                "std::nullopt}}") &&
              tv->nodeCount({}) == 3 && tv->isChecked({0}) &&
              tv->nodeColor({0}) == gfx::Color{200, 40, 40} &&
              tv->isChecked({1}) && tv->nodeText({1}) == "b" &&
              !tv->nodeColor({1}) && !tv->isChecked({2}) &&
              tv->nodeText({2}) == "c",
          "träd: sju fält positionellt, designerat och typat — kryss och "
          "färg");
    check(!anrop("setNodes",
                 "{{\"x\", {}, {}, false, false, true, {}, {}, 1}}") &&
              tv->nodeText({0}) == "a",
          "träd: nio fält är ingen TreeNode ⇒ hela setNodes opak");
    check(!anrop("setNodes", "{{\"x\", {}, {}, false, false, true, {}, 1}}") &&
              tv->nodeText({0}) == "a",
          "träd: ett åttonde fält som inte är en strängliste-literal ⇒ opak");
    check(!anrop("setNodes", "{{.checked = true, .text = \"x\"}}") &&
              tv->nodeText({0}) == "a",
          "träd: designerare i fel ordning kompilerar inte ⇒ opak");
    check(!anrop("setNodes",
                 "{{\"x\", {}, {}, false, false, false, {200, 0, 0}}}") &&
              tv->nodeText({0}) == "a",
          "träd: en bar {200, 0, 0} som färg kompilerar inte ⇒ opak");

    check(anrop("setChecked", "{1}, false") && !tv->isChecked({1}) &&
              anrop("setChecked", "{2}, true") && tv->isChecked({2}),
          "träd: setChecked(stig, på) utförs (två argument, Call)");
    check(anrop("setNodeColor", "{2}, gfx::Color{0, 140, 0}") &&
              tv->nodeColor({2}) == gfx::Color{0, 140, 0} &&
              anrop("setNodeColor", "{0}, std::nullopt") && !tv->nodeColor({0}) &&
              anrop("setNodeColor", "{2}, {}") && !tv->nodeColor({2}),
          "träd: setNodeColor med gfx::Color{…}, std::nullopt och {}");
    check(!anrop("setNodeColor", "{0}, {200, 0, 0}") && !tv->nodeColor({0}),
          "träd: setNodeColor med en bar {200, 0, 0} är OPAK och lämnar "
          "trädet orört (raden kompilerar inte)");
    check(anrop("addNode", "{}, \"d\", {}, gfx::Color{1, 2, 3}") &&
              tv->nodeColor({3}) == gfx::Color{1, 2, 3} &&
              anrop("setNode", "{3}, \"d2\", {}, std::nullopt") &&
              !tv->nodeColor({3}) && tv->nodeText({3}) == "d2",
          "träd: addNode/setNode med fjärde argumentet (färgen)");

    // ---- KOLUMNERNA: ColumnPanels gren tar trädets kolumner,
    //      cellerna är femte argumentet, åttonde fältet och `setCell`, och
    //      sorteringen är setter-fält — mätt på widgetens tillstånd.
    check(anrop("setColumns", "{{\"Namn\", 120}, {\"ms\", 60, "
                              "glaze::Align::Right}}") &&
              tv->columnCount() == 2 && tv->columns()[1].width == 60.0f &&
              anrop("addColumn", "\"Anrop\"") && tv->columnCount() == 3,
          "träd: setColumns/addColumn genom ColumnPanels gren");
    check(anrop("setNode", "{3}, \"d2\", {}, std::nullopt, {\"7\", \"2\"}") &&
              tv->cell({3}, 1) == "7" && tv->cell({3}, 2) == "2" &&
              anrop("addNode", "{}, \"e\", {}, {}, {\"9\"}") &&
              tv->cell({4}, 1) == "9",
          "träd: cellerna som femte argument till setNode/addNode");
    check(anrop("setCell", "{4}, 2, \"5\"") && tv->cell({4}, 2) == "5" &&
              !anrop("setCell", "{4}, 2") && !anrop("setCell", "{4}, x, \"5\""),
          "träd: setCell(stig, kolumn, text) — fel form ⇒ opak");
    check(!anrop("addNode", "{}, \"f\", {}, {}, 9") && tv->nodeCount({}) == 5,
          "träd: celler som inte är en strängliste-literal ⇒ opak");
    bld::applyProp(*w, {"sortColumn", "1"}, "preview_gate");
    bld::applyProp(*w, {"sortAscending", "false"}, "preview_gate");
    check(tv->sortColumn() == 1 && !tv->isSortAscending() &&
              bld::isSetterField("glaze::TreeView", "sortColumn") &&
              bld::isSetterField("glaze::TreeView", "fillColumn"),
          "träd: sorteringens setter-fält når trädet");
    bld::applyProp(*w, {"sortColumn", "-1"}, "preview_gate");
    // Trädet tillbaka som de följande fallen väntar sig det: fyra toppnoder.
    check(anrop("setColumns", "{}") && tv->columnCount() == 0 &&
              anrop("removeNode", "{4}") && tv->nodeCount({}) == 4,
          "träd: setColumns({}) tar bort kolumnerna");
    bld::applyProp(*w, {"multiSelect", "true"}, "preview_gate");
    bld::applyProp(*w, {"showChecks", "true"}, "preview_gate");
    check(tv->isMultiSelect() && tv->showsChecks(),
          "träd: setter-fälten multiSelect och showChecks når trädet");
    check(anrop("setSelected", "{0}, true") && anrop("setSelected", "{2}, true") &&
              tv->selectedPaths().size() == 2,
          "träd: setSelected(stig, på) i flerval");
    check(anrop("selectAll", "") && tv->selectedPaths().size() == 4 &&
              anrop("clearSelection", "") && tv->selectedPaths().empty() &&
              tv->currentPath().empty(),
          "träd: selectAll() väljer de synliga, clearSelection() släpper allt");
    check(!anrop("selectAll", "1") && !anrop("setSelected", "{0}") &&
              tv->selectedPaths().empty(),
          "träd: fel argumentantal ⇒ opak, valet orört");

    // ---- FLYTTEN AV FLERA: `moveNodes(stigar,
    //      förälder, index)` med STIGLISTAN (`argPathList`) — indexet i
    //      FÖRE-flytten-koordinater och noderna i trädordning. `{0}` som lista
    //      är talet 0 och kompilerar inte, och ett argument för lite eller ett
    //      saknat komma är OPAKT. En stig utanför trädet är widgetens egen
    //      logg + no-op, som `setItem(99, …)`.
    check(anrop("setNodes", "{{\"A\", {{\"A1\"}, {\"A2\"}}}, {\"B\"}, {\"C\"}}"),
          "träd: (setNodes för flytten av flera)");
    check(anrop("moveNodes", "{{0, 1}, {2}}, {1}, 0") &&
              tv->nodeCount({}) == 2 && tv->nodeText({0}) == "A" &&
              tv->nodeCount({0}) == 1 && tv->nodeText({1}) == "B" &&
              tv->nodeCount({1}) == 2 && tv->nodeText({1, 0}) == "A2" &&
              tv->nodeText({1, 1}) == "C",
          "träd: moveNodes({{0, 1}, {2}}, {1}, 0) flyttar A2 och C in i B, i "
          "trädordning",
          std::to_string(tv->nodeCount({})) + " toppnoder");
    check(!anrop("moveNodes", "{0}, {1}, 0") &&
              !anrop("moveNodes", "{{0}}, {1}") &&
              !anrop("moveNodes", "{{0}} {1}, 0") && tv->nodeCount({}) == 2 &&
              tv->nodeCount({0}) == 1 && tv->nodeCount({1}) == 2,
          "träd: moveNodes med {0} som lista, utan index eller utan komma ⇒ "
          "opak, och trädet är ORÖRT");
    check(anrop("moveNodes", "{{1, 0},}, {}, 0") && tv->nodeCount({}) == 3 &&
              tv->nodeText({0}) == "A2",
          "träd: ett avslutande komma i stiglistan är giltig C++ och utförs");
    check(!anrop("moveNodes", "{,}, {}, 0") && tv->nodeCount({}) == 3,
          "träd: ett ENSAMT komma i stiglistan ⇒ opak");
    check(anrop("moveNodes", "{{9}}, {}, 0") && tv->nodeCount({}) == 3 &&
              tv->nodeText({0}) == "A2",
          "träd: en stig utanför trädet utförs som widgetens logg + no-op");

    // ---- klassningen: valet är en Prop, kollektionen Call-satser.
    check(bld::isSetterField("glaze::TreeView", "selectedPath"),
          "träd: selectedPath är ett setter-fält");
    check(bld::isSetterField("glaze::TreeView", "multiSelect") &&
              bld::isSetterField("glaze::TreeView", "showChecks") &&
              !bld::isSetterField("glaze::TreeView", "checked") &&
              !bld::isSetterField("glaze::TreeView", "nodeColor"),
          "träd: multiSelect/showChecks är setter-fält, setChecked/"
          "setNodeColor Call-satser");
    check(!bld::isSetterField("glaze::TreeView", "nodes") &&
              !bld::isSetterField("glaze::TreeView", "expanded"),
          "träd: setNodes/setExpanded är Call-satser, inga setter-fält");
    check(bld::isOrDerivesFrom("glaze::TreeView", "glaze::Panel") &&
              bld::isSetterField("glaze::TreeView", "border"),
          "träd: TreeView härleds ur Panel och ärver border");
    glaze::setLanguage("sv");
}

// LISTVYN — kollektionsvokabulären, sorteringen och valet ska
// UTFÖRAS av previewn (regel 8): utan grenarna ritades en fylld listvy TOM
// i canvasen och i glaze-render medan appen visade raderna. Mätt på
// WIDGETENS tillstånd, aldrig på modellen.
void fallListvy()
{
    auto w = build("glaze::ListView", {});
    if (!w)
        return;
    auto* lv = dynamic_cast<glaze::ListView*>(w.get());
    check(lv != nullptr, "listvy: registret ger en riktig glaze::ListView", "");
    if (!lv)
        return;
    auto anrop = [&](const char* metod, const char* args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };

    // ---- KOLUMNERNA — rubrik (literal eller tr), bredd, justering.
    glaze::setLanguage("sv");
    check(anrop("addColumn", "\"Namn\", 120") &&
              anrop("addColumn", "\"Storlek\", 70, glaze::Align::Right") &&
              anrop("addColumn", "glaze::tr(2)"),
          "listvy: addColumn utförs i alla tre formerna");
    check(lv->columnCount() == 3 && lv->columns()[0].width == 120.0f &&
              lv->columns()[1].align == glaze::Align::Right &&
              lv->columns()[2].text.id() == 2 &&
              lv->columns()[2].text.str() == "Avbryt",
          "listvy: bredd, justering och översättnings-TOKEN når kolumnerna");
    check(anrop("setColumn", "2, \"Typ\", 80") &&
              lv->columns()[2].text.str() == "Typ" &&
              lv->columns()[2].text.id() == 0 &&
              lv->columns()[2].width == 80.0f,
          "listvy: setColumn ersätter kolumnen");
    check(anrop("setColumnWidth", "0, 150.5") &&
              lv->columnWidth(0) == 150.5f,
          "listvy: setColumnWidth");
    check(anrop("setColumns", "{{\"A\", 50}, {\"B\", 60, "
                              "glaze::Align::Center}, {glaze::tr(1)}}") &&
              lv->columnCount() == 3 &&
              lv->columns()[1].align == glaze::Align::Center &&
              lv->columns()[2].text.id() == 1,
          "listvy: setColumns — aggregatliteralen i fältordning");

    // ---- RADERNA — celler som listliteral, valfri bild (`{}` = ingen).
    check(anrop("addRow", "{\"a.txt\", \"12 kB\"}") &&
              anrop("addRow", "{\"b.png\", \"3 MB\"}, "
                              "glaze::img(\"toolbar\", \"ok\")") &&
              anrop("addRow", "{\"c\"}, {}"),
          "listvy: addRow utförs (utan bild, med bild, med {})");
    check(lv->rowCount() == 3 && lv->cell(1, 1) == "3 MB" &&
              lv->cell(2, 1).empty(),
          "listvy: cellerna nådde raderna");
    check(anrop("setRow", "1, {\"B.png\", \"4 MB\"}") &&
              lv->cell(1, 0) == "B.png",
          "listvy: setRow ersätter raden");
    check(anrop("setCell", "0, 1, \"13 kB\"") && lv->cell(0, 1) == "13 kB",
          "listvy: setCell");
    check(anrop("setRows", "{{{\"x\", \"1\"}}, {{\"y\", \"2\"}, "
                           "glaze::img(\"toolbar\", \"info\")}, {}}") &&
              lv->rowCount() == 3 && lv->cell(1, 0) == "y" &&
              lv->cell(2, 0).empty(),
          "listvy: setRows — radliteralen, och {} är en tom rad");

    // ---- SORTERINGEN — nycklar som Call, kolumn och riktning som FÄLT.
    check(anrop("setSortKey", "0, 1, 12288") &&
              anrop("setSortKey", "1, 1, 1.5e3"),
          "listvy: setSortKey utförs (heltal och exponentform)");
    bld::applyProp(*w, {"sortColumn", "1"}, "preview_gate");
    bld::applyProp(*w, {"sortAscending", "false"}, "preview_gate");
    std::vector<int> ord;
    for (int p = 0; p < lv->rowCount(); ++p)
        ord.push_back(lv->rowAtDisplay(p));
    // Fallande: raden UTAN nyckel först (stigande hade lagt den sist), sedan
    // nycklarna 12288 och 1500 — previewn sorterar exakt som appen.
    check(lv->sortColumn() == 1 && !lv->isSortAscending() &&
              ord == std::vector<int>{2, 0, 1},
          "listvy: sortColumn/sortAscending sorterar previewn på NYCKLARNA");

    // ---- VALET — flerval som fält, en rad i taget som Call.
    bld::applyProp(*w, {"multiSelect", "true"}, "preview_gate");
    check(lv->isMultiSelect() && anrop("setSelected", "0, true") &&
              anrop("setSelected", "2, true") &&
              lv->selectedRows().size() == 2,
          "listvy: multiSelect + setSelected väljer två rader");
    check(anrop("clearSelection", "") && lv->selectedRows().empty() &&
              anrop("selectAll", "") && lv->selectedRows().size() == 3,
          "listvy: clearSelection och selectAll utförs");
    bld::applyProp(*w, {"selectedIndex", "1"}, "preview_gate");
    check(lv->selectedIndex() == 1 && lv->selectedRows().size() == 1,
          "listvy: selectedIndex-raden väljer BARA raden");

    // ---- ATOMICITETEN: trasiga rader ⇒ false och en ORÖRD tabell.
    check(!anrop("addRow", "\"inte en lista\""), "listvy: addRow utan lista ⇒ opak");
    check(!anrop("addRow", "{\"a\"}, glaze::img(\"x\")"),
          "listvy: trasig bild ⇒ opak");
    check(!anrop("addColumn", "\"A\", 10, glaze::Dock::Right"),
          "listvy: FEL enum (Dock för Align) ⇒ opak");
    check(!anrop("setColumns", "{{\"A\", bred}}"),
          "listvy: oparsbar bredd ⇒ hela setColumns opak");
    check(!anrop("setRows", "{{{\"a\"}}, {\"b\"}}"),
          "listvy: en rad utan inre klammer ⇒ hela setRows opak");
    check(!anrop("setSortKey", "0, 1, nan"), "listvy: nan är ingen literal ⇒ opak");
    check(!anrop("setCell", "0, 1"), "listvy: setCell utan text ⇒ opak");
    check(!anrop("setSelected", "0, kanske"), "listvy: ogiltig flagga ⇒ opak");
    check(lv->rowCount() == 3 && lv->columnCount() == 3 &&
              lv->cell(0, 0) == "x",
          "listvy: …och tabellen är ORÖRD efter alla trasiga rader");

    // ---- RADENS TEXTFÄRG: `setRowColor`, tredje
    //      argumentet till addRow/setRow och radaggregatets tredje fält —
    //      positionellt, designerat och typat. En bar `{200, 0, 0}` är opak.
    const gfx::Color röd{200, 0, 0}, grön{0, 140, 0};
    check(anrop("setRowColor", "1, gfx::Color{200, 0, 0}") &&
              lv->rowColor(1) == röd && !lv->rowColor(0),
          "listvy: setRowColor(1, gfx::Color{…}) når raden");
    check(anrop("setRowColor", "1, {}") && !lv->rowColor(1) &&
              anrop("setRowColor", "1, std::nullopt") && !lv->rowColor(1),
          "listvy: setRowColor med {} och std::nullopt nollar");
    check(!anrop("setRowColor", "1, {200, 0, 0}") && !lv->rowColor(1),
          "listvy: setRowColor med en bar {200, 0, 0} är OPAK (kompilerar "
          "inte)");
    check(anrop("addRow", "{\"z\"}, {}, gfx::Color{0, 140, 0}") &&
              lv->rowCount() == 4 && lv->rowColor(3) == grön &&
              anrop("setRow", "3, {\"Z\"}, {}") && !lv->rowColor(3),
          "listvy: addRow med färg, setRow utan färg NOLLAR den (tilldelning)");
    check(anrop("setRows", "{{{\"a\"}, {}, gfx::Color{200, 0, 0}}, {{\"b\"}}, "
                           "{.cells = {\"c\"}, .color = gfx::Color{0, 140, 0}}, "
                           "glaze::ListView::Row{{\"d\"}}}") &&
              lv->rowCount() == 4 && lv->rowColor(0) == röd &&
              !lv->rowColor(1) && lv->rowColor(2) == grön &&
              lv->cell(2, 0) == "c" && lv->cell(3, 0) == "d",
          "listvy: setRows med tre fält — positionellt, designerat, typat");
    check(!anrop("setRows", "{{{\"a\"}, {}, {}, 1}}") && lv->rowCount() == 4,
          "listvy: fyra fält är ingen rad ⇒ hela setRows opak");
    check(!bld::isSetterField("glaze::ListView", "rowColor"),
          "listvy: setRowColor är en Call-sats");

    // ---- CELLENS FÄRGER: `setCellColor(rad, kolumn, bakgrund,
    //      text)` — två valfria färger i `setRowColor`s tre former, atomiskt.
    check(anrop("setCellColor",
                "1, 0, gfx::Color{200, 0, 0}, gfx::Color{0, 140, 0}") &&
              lv->cellBackground(1, 0) == röd &&
              lv->cellTextColor(1, 0) == grön && !lv->cellBackground(0, 0),
          "listvy: setCellColor(rad, kolumn, bakgrund, text) når just cellen");
    check(anrop("setCellColor", "1, 0, std::nullopt, {}") &&
              !lv->cellBackground(1, 0) && !lv->cellTextColor(1, 0),
          "listvy: setCellColor med std::nullopt och {} nollar båda");
    check(!anrop("setCellColor", "1, 0, gfx::Color{1, 2, 3}") &&
              !anrop("setCellColor", "1, 0, {1, 2, 3}, {}") &&
              !anrop("setCellColor", "x, 0, {}, {}") &&
              !anrop("setCellColor",
                     "1, 0, {}, {}, gfx::Color{1, 2, 3}"),
          "listvy: fel antal argument, en bar {r, g, b} eller ett index som "
          "inte är ett tal ⇒ opak");
    check(!bld::isSetterField("glaze::ListView", "cellColor"),
          "listvy: setCellColor är en Call-sats");

    // ---- RADRUBRIKERNA: ETT bool-argument, setter-fält på båda.
    bld::applyProp(*w, {"rowHeaders", "true"}, "preview_gate");
    check(lv->rowHeaders() && bld::isSetterField("glaze::ListView", "rowHeaders") &&
              bld::isSetterField("glaze::DataGrid", "rowHeaders") &&
              !bld::isSetterField("glaze::TreeView", "rowHeaders"),
          "listvy: rowHeaders är ett setter-fält på ListView och DataGrid, "
          "inte på trädet");
    bld::applyProp(*w, {"rowHeaders", "false"}, "preview_gate");
    check(!lv->rowHeaders(), "listvy: rowHeaders = false släcker radrubrikerna");

    // ---- BREDD EFTER INNEHÅLLET: konstanten skriven med någon av
    //      typerna som ärver kolumnen, och talet −1, i alla tre
    //      kolumnformerna — och ett okänt namn är opakt.
    {
        auto w2 = build("glaze::ListView", {});
        auto* l2 = dynamic_cast<glaze::ListView*>(w2.get());
        check(l2 != nullptr, "listvy: en andra listvy för bredderna");
        if (l2) {
            const float a = glaze::ColumnPanel::Column::autoWidth;
            auto anrop2 = [&](const char* metod, const char* args) {
                return bld::applyCall(*w2, {metod, args}, "preview_gate");
            };
            check(anrop2("setColumns",
                         "{{\"A\", glaze::Table::Column::autoWidth}, "
                         "{\"B\", -1}, {\"C\", 40}}") &&
                      l2->columnWidth(0) == a && l2->columnWidth(1) == a &&
                      l2->columnWidth(2) == 40.0f,
                  "listvy: setColumns — autoWidth som konstant och som −1");
            check(anrop2("setColumnWidth", "2, glaze::ListView::Column::autoWidth") &&
                      l2->columnWidth(2) == a &&
                      anrop2("setColumnWidth", "2, 55") &&
                      l2->columnWidth(2) == 55.0f &&
                      anrop2("setColumnWidth",
                             "0, glaze::DataGrid::Column::autoWidth") &&
                      anrop2("setColumnWidth",
                             "0, glaze::ColumnPanel::Column::autoWidth") &&
                      anrop2("setColumnWidth",
                             "0, glaze::TreeView::Column::autoWidth"),
                  "listvy: setColumnWidth tar konstanten genom varje typ som "
                  "ärver kolumnen");
            check(anrop2("addColumn", "\"D\", glaze::Table::Column::autoWidth") &&
                      anrop2("setColumn",
                             "3, \"E\", glaze::Table::Column::autoWidth, "
                             "glaze::Align::Right") &&
                      l2->columnCount() == 4 && l2->columnWidth(3) == a,
                  "listvy: addColumn och setColumn tar konstanten");
            check(!anrop2("setColumnWidth", "0, glaze::Table::Column::bredd") &&
                      !anrop2("setColumnWidth",
                              "0, glaze::Dock::Column::autoWidth") &&
                      !anrop2("setColumnWidth",
                              "0, Column::autoWidth"),
                  "listvy: ett annat namn än konstanten på en känd typ ⇒ opak");
        }
    }

    // ---- KRYSSEN: synligheten är ett setter-fält och
    //      `setChecked(rad, på)` en Call-sats med två argument — ListBoxens
    //      form. Fel antal eller fel typ är OPAKT och lämnar kryssen orörda.
    //      Mätt på widgetens tillstånd.
    bld::applyProp(*w, {"showChecks", "true"}, "preview_gate");
    check(lv->showsChecks(), "listvy: setter-fältet showChecks når listvyn");
    check(anrop("setChecked", "1, true") && lv->isChecked(1) &&
              !lv->isChecked(0),
          "listvy: setChecked(1, true) kryssar raden");
    check(!anrop("setChecked", "1") && !anrop("setChecked", "\"x\", true") &&
              !anrop("setChecked", "1, ja") &&
              lv->checkedRows() == std::vector<int>{1},
          "listvy: setChecked utan flagga, med en sträng som rad eller en "
          "okänd flagga ⇒ opak, kryssen ORÖRDA");
    check(anrop("setChecked", "9, true") && lv->checkedRows().size() == 1,
          "listvy: en rad utanför utförs som widgetens logg + no-op");
    check(!bld::isSetterField("glaze::ListView", "checked") &&
              bld::isSetterField("glaze::ListView", "showChecks"),
          "listvy: showChecks är ett setter-fält, setChecked en Call-sats");

    // ---- KLASSNINGEN: egenskaperna är Prop, kollektionen Call-satser.
    check(bld::isSetterField("glaze::ListView", "sortColumn") &&
              bld::isSetterField("glaze::ListView", "selectedIndex") &&
              bld::isSetterField("glaze::ListView", "multiSelect"),
          "listvy: sortering och val är setter-fält");
    check(!bld::isSetterField("glaze::ListView", "rows") &&
              !bld::isSetterField("glaze::ListView", "columns") &&
              !bld::isSetterField("glaze::ListView", "cell") &&
              !bld::isSetterField("glaze::ListView", "selected"),
          "listvy: kollektionen och setSelected är Call-satser");
    check(bld::isOrDerivesFrom("glaze::ListView", "glaze::Table") &&
              bld::isOrDerivesFrom("glaze::ListView", "glaze::Panel"),
          "listvy: ListView härleds ur Table och Panel");

    // ---- KOLUMNHUVUDET — rubriken på flera rader och dess EGEN
    //      justering: aggregatets FJÄRDE fält, styckeformens fjärde argument
    //      och huvudets två setter-fält. Mätt på widgetens tillstånd.
    check(anrop("setColumns",
                "{{\"SS-EN 50341\\nStandard\\nload cases\", 90, "
                "glaze::Align::Right, glaze::Align::Center}, {\"B\", 60, "
                "glaze::Align::Right}, {\"C\", 40, glaze::Align::Left, {}}, "
                "{\"D\", 40, glaze::Align::Center, std::nullopt}}") &&
              lv->columnCount() == 4 &&
              lv->columns()[0].text.str() ==
                  "SS-EN 50341\nStandard\nload cases" &&
              lv->columns()[0].align == glaze::Align::Right &&
              lv->columns()[0].headerAlign == glaze::Align::Center &&
              !lv->columns()[1].headerAlign && !lv->columns()[2].headerAlign &&
              !lv->columns()[3].headerAlign &&
              lv->columns()[3].align == glaze::Align::Center,
          "listvy: setColumns med FJÄRDE fältet — \\n blir en radbrytning, "
          "och utelämnad, {} och std::nullopt betyder \"som cellerna\"");
    check(anrop("addColumn",
                "\"E\", 50, glaze::Align::Left, glaze::Align::Right") &&
              lv->columnCount() == 5 &&
              lv->columns()[4].headerAlign == glaze::Align::Right,
          "listvy: addColumn med rubrikens justering");
    check(anrop("setColumn",
                "4, \"F\\nG\", 55, glaze::Align::Center, glaze::Align::Left") &&
              lv->columnCount() == 5 &&
              lv->columns()[4].text.str() == "F\nG" &&
              lv->columns()[4].headerAlign == glaze::Align::Left &&
              lv->columns()[4].align == glaze::Align::Center &&
              lv->columns()[4].width == 55.0f,
          "listvy: setColumn med rubrikens justering");
    check(anrop("setColumn", "4, \"F\", 55, glaze::Align::Center") &&
              lv->columnCount() == 5 && !lv->columns()[4].headerAlign,
          "listvy: setColumn utan fjärde argument ERSÄTTER — rubriken följer "
          "cellerna igen");
    bld::applyProp(*w, {"headerLines", "4"}, "preview_gate");
    bld::applyProp(*w, {"headerVAlign", "glaze::VAlign::Bottom"},
                   "preview_gate");
    check(lv->headerLines() == 4 &&
              lv->headerVAlign() == glaze::VAlign::Bottom,
          "listvy: setHeaderLines/setHeaderVAlign utförs (setter-fälten)");
    bld::applyProp(*w, {"headerLines", "0"}, "preview_gate");
    check(lv->headerLines() == 0, "listvy: setHeaderLines(0) är AUTO");
    bld::applyProp(*w, {"headerLines", "kanske"}, "preview_gate");
    bld::applyProp(*w, {"headerLines", "-2"}, "preview_gate");
    bld::applyProp(*w, {"headerLines", "2.5"}, "preview_gate");
    bld::applyProp(*w, {"headerVAlign", "glaze::VAlign::Kanske"},
                   "preview_gate");
    check(lv->headerLines() == 0 &&
              lv->headerVAlign() == glaze::VAlign::Bottom,
          "listvy: ett ogiltigt radantal (text, negativt, decimaltal) och en "
          "okänd höjdjustering lämnar huvudet orört");

    // ATOMICITETEN för de nya formerna: trasigt ⇒ false och ORÖRDA kolumner.
    check(!anrop("setColumns",
                 "{{\"A\", 50, glaze::Align::Left, glaze::VAlign::Top}}"),
          "listvy: FEL enum på fjärde platsen (VAlign för Align) ⇒ hela "
          "setColumns opak");
    check(!anrop("setColumns",
                 "{{\"A\", 50, glaze::Align::Left, glaze::Align::Left, 1}}"),
          "listvy: fem fält är ingen Column ⇒ hela setColumns opak");
    check(!anrop("addColumn",
                 "\"A\", 50, glaze::Align::Left, glaze::Align::Left, 1"),
          "listvy: addColumn med fem argument ⇒ opak");
    check(!anrop("setColumn", "0, \"A\", 50, glaze::Align::Left, kanske"),
          "listvy: oparsbar rubrikjustering ⇒ opak");
    check(lv->columnCount() == 5 &&
              lv->columns()[0].headerAlign == glaze::Align::Center &&
              lv->columns()[0].text.str() ==
                  "SS-EN 50341\nStandard\nload cases",
          "listvy: …och kolumnerna är ORÖRDA efter de trasiga raderna");
    check(bld::isSetterField("glaze::ListView", "headerLines") &&
              bld::isSetterField("glaze::ListView", "headerVAlign") &&
              bld::isSetterField("glaze::DataGrid", "headerLines") &&
              bld::isSetterField("glaze::DataGrid", "headerVAlign") &&
              !bld::isSetterField("glaze::ListView", "headerAlign"),
          "listvy: huvudets radantal och höjdjustering är setter-fält på båda "
          "— rubrikens sidjustering bor i kolumnen och är inget fält");
}

// DATAGRIDDEN — tabellens vokabulär är DELAD med listvyn (samma
// gren i `applyCall`, mätt i `fallListvy`), så här mäts det som är
// datagridens: kolumntypen, valen och den aktuella cellen ska UTFÖRAS av
// previewn — och kolumntypen hör till INDEXET, så satsordningen avgör inte.
// Mätt på WIDGETENS tillstånd, aldrig på modellen.
void fallDatagrid()
{
    auto w = build("glaze::DataGrid", {});
    if (!w)
        return;
    auto* dg = dynamic_cast<glaze::DataGrid*>(w.get());
    check(dg != nullptr, "datagrid: registret ger en riktig glaze::DataGrid", "");
    if (!dg)
        return;
    auto anrop = [&](const char* metod, const char* args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };

    // Editorerna är DOLDA barn — previewn ska inte rita ett fält i en cell.
    bool doldaBarn = !w->children().empty();
    for (const glaze::Widget* b : w->children())
        doldaBarn = doldaBarn && !b->isVisible();
    check(doldaBarn, "datagrid: editorerna finns och är dolda i vila");

    // ---- KOLUMNTYPEN FÖRE kolumnerna — satsordningen ska inte avgöra.
    check(anrop("setColumnEditor", "1, glaze::DataGrid::Editor::Choice") &&
              anrop("setColumnChoices", "1, {\"Ny\", \"Pågår\", \"Klar\"}") &&
              anrop("setColumnEditor", "2, glaze::DataGrid::Editor::None"),
          "datagrid: setColumnEditor/setColumnChoices utförs före kolumnerna");
    check(anrop("setColumns", "{{\"Uppgift\", 140}, {\"Status\", 90}, "
                              "{\"Id\", 40, glaze::Align::Right}}") &&
              anrop("addRow", "{\"Skriv grind\", \"Pågår\", \"7\"}") &&
              anrop("addRow", "{\"Rita\", \"Ny\", \"3\"}"),
          "datagrid: tabellens vokabulär utförs på datagriden");
    check(dg->columnEditor(0) == glaze::DataGrid::Editor::Text &&
              dg->columnEditor(1) == glaze::DataGrid::Editor::Choice &&
              dg->columnEditor(2) == glaze::DataGrid::Editor::None &&
              dg->columnChoices(1) ==
                  std::vector<std::string>{"Ny", "Pågår", "Klar"},
          "datagrid: kolumntypen och valen ÖVERLEVDE setColumns (indexets)");
    check(anrop("setColumnEditor", "1, glaze::DataGrid::Editor::Text") &&
              dg->columnEditor(1) == glaze::DataGrid::Editor::Text &&
              anrop("setColumnEditor", "1, glaze::DataGrid::Editor::Choice") &&
              dg->columnEditor(1) == glaze::DataGrid::Editor::Choice,
          "datagrid: kolumntypen går att byta fram och tillbaka");

    // ---- DEN AKTUELLA CELLEN — två heltal; (−1, −1) = ingen.
    check(anrop("setCurrentCell", "1, 2") && dg->currentRow() == 1 &&
              dg->currentColumn() == 2,
          "datagrid: setCurrentCell sätter rad OCH kolumn");
    check(anrop("setCurrentCell", "5, 0") && dg->currentRow() == 1 &&
              dg->currentColumn() == 2,
          "datagrid: setCurrentCell utanför ⇒ widgetens logg + no-op");
    check(anrop("setCurrentCell", "-1, -1") && dg->currentRow() == -1 &&
              dg->currentColumn() == -1,
          "datagrid: setCurrentCell(-1, -1) nollar den aktuella cellen");

    // ---- SORTERINGEN är Tables setter-fält även här.
    bld::applyProp(*w, {"sortColumn", "2"}, "preview_gate");
    check(dg->sortColumn() == 2 && dg->rowAtDisplay(0) == 1,
          "datagrid: sortColumn sorterar previewn (3 före 7)");

    // ---- KOLUMNHUVUDET — Tables setter-fält även här, och
    //      kolumnens fjärde fält i datagridens massform.
    bld::applyProp(*w, {"headerLines", "3"}, "preview_gate");
    bld::applyProp(*w, {"headerVAlign", "glaze::VAlign::Top"}, "preview_gate");
    check(dg->headerLines() == 3 && dg->headerVAlign() == glaze::VAlign::Top,
          "datagrid: setHeaderLines/setHeaderVAlign utförs");
    check(anrop("setColumn", "2, \"Id\\n(nr)\", 40, glaze::Align::Right, "
                             "glaze::Align::Center") &&
              dg->columnCount() == 3 &&
              dg->columns()[2].headerAlign == glaze::Align::Center &&
              dg->columnEditor(2) == glaze::DataGrid::Editor::None,
          "datagrid: setColumn med rubrikens justering — kolumntypen står");

    // ---- ATOMICITETEN: trasiga rader ⇒ false och en ORÖRD datagrid.
    check(!anrop("setColumnEditor", "0, glaze::Align::Left"),
          "datagrid: FEL enum (Align för Editor) ⇒ opak");
    check(!anrop("setColumnEditor", "0, glaze::DataGrid::Editor::Kanske"),
          "datagrid: okänd editor ⇒ opak");
    check(!anrop("setColumnEditor", "0"), "datagrid: setColumnEditor utan typ ⇒ opak");
    check(!anrop("setColumnChoices", "1, \"inte en lista\""),
          "datagrid: val utan lista ⇒ opak");
    check(!anrop("setCurrentCell", "1"), "datagrid: setCurrentCell med ett tal ⇒ opak");
    check(!anrop("setCurrentCell", "1, x"), "datagrid: oparsbar kolumn ⇒ opak");
    check(dg->columnEditor(0) == glaze::DataGrid::Editor::Text &&
              dg->columnChoices(1).size() == 3 && dg->currentRow() == -1,
          "datagrid: …och datagriden är ORÖRD efter alla trasiga rader");

    // ---- KLASSNINGEN: sorteringen är Prop, kolumntypen och cellen Call.
    check(bld::isSetterField("glaze::DataGrid", "sortColumn") &&
              bld::isSetterField("glaze::DataGrid", "sortAscending") &&
              bld::isSetterField("glaze::DataGrid", "sortable"),
          "datagrid: sorteringen är setter-fält");
    check(!bld::isSetterField("glaze::DataGrid", "columnEditor") &&
              !bld::isSetterField("glaze::DataGrid", "columnChoices") &&
              !bld::isSetterField("glaze::DataGrid", "currentCell") &&
              !bld::isSetterField("glaze::DataGrid", "selectedIndex"),
          "datagrid: kolumntypen och cellen är Call-satser, inget listval");
    check(bld::isOrDerivesFrom("glaze::DataGrid", "glaze::Table") &&
              bld::isOrDerivesFrom("glaze::DataGrid", "glaze::Panel") &&
              !bld::isOrDerivesFrom("glaze::DataGrid", "glaze::ListView"),
          "datagrid: DataGrid härleds ur Table och Panel, INTE ur ListView");
}

// =============================================== FILVÄLJARFÄLTET
//
// Regel 8: sökvägen och läget är SETTER-fält som previewn UTFÖR, och filtren
// en LISTA i kollektionsvokabulären (`addFilter`/`setFilter`/`setFilters`),
// atomisk som tabellens. Mätt på WIDGETENS tillstånd, inte på modellen.
void fallFilvaljarfalt()
{
    glaze::setLanguage("sv");
    auto w = build("glaze::FilePicker",
                   {{"path", "\"C:/Användare/rapport.pdf\""},
                    {"mode", "glaze::FileDialogMode::Save"}});
    if (!w)
        return;
    auto* fp = dynamic_cast<glaze::FilePicker*>(w.get());
    check(fp != nullptr, "filväljarfält: registret ger en riktig glaze::FilePicker",
          "");
    if (!fp)
        return;
    check(fp->path() == "C:/Användare/rapport.pdf",
          "filväljarfält: setPath utförs (åäö orörda)", fp->path());
    check(fp->mode() == glaze::FileDialogMode::Save,
          "filväljarfält: setMode(glaze::FileDialogMode::Save) utförs", "");
    bld::applyProp(*w, {"mode", "glaze::FileDialogMode::Folder"}, "preview_gate");
    check(fp->mode() == glaze::FileDialogMode::Folder,
          "filväljarfält: mappläget", "");

    auto anrop = [&](const char* metod, const char* args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };
    check(anrop("addFilter", "\"Bilder\", \"*.png;*.jpg\"") &&
              anrop("addFilter", "glaze::tr(43), \"*.*\"") &&
              fp->filters().size() == 2 &&
              fp->filters()[0].name.str() == "Bilder" &&
              fp->filters()[0].patterns == "*.png;*.jpg" &&
              fp->filters()[1].name.id() == 43,
          "filväljarfält: addFilter — literal och glaze::tr(N) (tokenet "
          "bevaras)");
    check(anrop("setFilter", "0, \"PNG\", \"*.png\"") &&
              fp->filters()[0].name.str() == "PNG" &&
              fp->filters()[0].patterns == "*.png",
          "filväljarfält: setFilter ersätter posten");
    check(anrop("setFilter", "7, \"X\", \"*.x\"") && fp->filters().size() == 2,
          "filväljarfält: setFilter utanför ⇒ widgetens logg + no-op");
    check(anrop("setFilters", "{{\"Text\", \"*.txt\"}, {glaze::tr(43), \"*.*\"}}") &&
              fp->filters().size() == 2 && fp->filters()[0].patterns == "*.txt" &&
              fp->filters()[1].name.id() == 43,
          "filväljarfält: setFilters — massformen i FileFilters fältordning");
    check(anrop("setFilters", "{}") && fp->filters().empty(),
          "filväljarfält: setFilters({}) tömmer");

    // ATOMICITETEN — trasiga rader ⇒ false och ett ORÖRT fält.
    anrop("addFilter", "\"Text\", \"*.txt\"");
    check(!anrop("addFilter", "\"Bara namn\"") &&
              !anrop("setFilter", "x, \"A\", \"*.a\"") &&
              !anrop("setFilters", "{{\"Utan mönster\"}}") &&
              !anrop("setFilters", "{{\"A\", \"*.a\", \"tredje\"}}") &&
              !anrop("addFilter", "glaze::str::FileAllFiles, \"*.*\""),
          "filväljarfält: fel antal fält, oparsbart index och en KONSTANT ⇒ "
          "opak");
    check(fp->filters().size() == 1 && fp->filters()[0].patterns == "*.txt",
          "filväljarfält: …och filtren är ORÖRDA efter de trasiga raderna");

    // KLASSNINGEN: sökvägen och läget är Prop, filtren Call; arvet är
    // Widgets (inga clip/border).
    check(bld::isSetterField("glaze::FilePicker", "path") &&
              bld::isSetterField("glaze::FilePicker", "mode") &&
              !bld::isSetterField("glaze::FilePicker", "filter") &&
              !bld::isSetterField("glaze::FilePicker", "filters") &&
              !bld::isSetterField("glaze::FilePicker", "text"),
          "filväljarfält: path/mode är setter-fält, filtren och text är det "
          "inte");
    check(bld::isOrDerivesFrom("glaze::FilePicker", "glaze::Widget") &&
              !bld::isOrDerivesFrom("glaze::FilePicker", "glaze::Panel"),
          "filväljarfält: ärver Widget, inte Panel");
}

// =================================================== FÄRGVÄLJAREN
//
// Regel 8: färgen är ett SETTER-fält vars `.glazeui`-form är HEXTEXTEN
// (`farg.setColor("#3366FF");`), och kodens `gfx::Color{…}` står bredvid —
// previewn ska UTFÖRA båda och ge exakt det tillstånd C++ ger (opakt). En
// text som inte är en färg lämnar färgväljaren ORÖRD. Mätt på WIDGETENS
// tillstånd, och hela kedjan (text → reader → modell → satsväg) en gång.
//
// VAD DEN INTE MÄTER: popupen, tangenterna och användarvägen — de bor i
// tests/colorpicker_gate.cpp.
void fallFargvaljare()
{
    auto lika = [](gfx::Color c, int r, int g, int b) {
        return c.r == r && c.g == g && c.b == b && c.a == 255;
    };
    auto w = build("glaze::ColorPicker", {{"color", "\"#3366FF\""}});
    if (!w)
        return;
    auto* cp = dynamic_cast<glaze::ColorPicker*>(w.get());
    check(cp != nullptr, "färgväljare: registret ger en riktig glaze::ColorPicker",
          "");
    if (!cp)
        return;
    check(lika(cp->color(), 0x33, 0x66, 0xff),
          "färgväljare: setColor(\"#3366FF\") utförs", cp->colorText());
    bld::applyProp(*w, {"color", "\"#a0b1c2\""}, "preview_gate");
    check(lika(cp->color(), 0xa0, 0xb1, 0xc2) && cp->colorText() == "#A0B1C2",
          "färgväljare: gemener tolkas, textformen skrivs med versaler",
          cp->colorText());
    bld::applyProp(*w, {"color", "\"102030\""}, "preview_gate");
    check(lika(cp->color(), 0x10, 0x20, 0x30),
          "färgväljare: `#` är valfritt", cp->colorText());
    bld::applyProp(*w, {"color", "gfx::Color{76, 175, 80, 255}"},
                   "preview_gate");
    check(lika(cp->color(), 76, 175, 80),
          "färgväljare: setColor(gfx::Color{…}) utförs — kodens form",
          cp->colorText());
    bld::applyProp(*w, {"color", "gfx::Color{51, 102, 255}"}, "preview_gate");
    check(lika(cp->color(), 51, 102, 255),
          "färgväljare: TRE kanaler utförs (laglig C++, samma "
          "parser som Labels)", cp->colorText());
    bld::applyProp(*w, {"color", "gfx::Color{1, 2, 3, 128}"}, "preview_gate");
    check(lika(cp->color(), 1, 2, 3),
          "färgväljare: alfan blir 255 — värdet är OPAKT, i previewn som i C++",
          std::to_string(int(cp->color().a)));

    // OGILTIGT ⇒ ORÖRT (och loggat av widgeten).
    for (const char* fel : {"\"#12345\"", "\"#1234567\"", "\"#GG0000\"",
                            "\"röd\"", "\"\"", "kMinFarg",
                            "gfx::Color{256, 0, 0, 255}"}) {
        bld::applyProp(*w, {"color", fel}, "preview_gate");
        check(lika(cp->color(), 1, 2, 3),
              std::string("färgväljare: ") + fel + " lämnar färgen orörd",
              cp->colorText());
    }

    // En ANVÄNDARWIDGET som ärver färgväljaren emitteras som ett CALL —
    // setter-fallbacken ska bära båda formerna dit (som för items).
    {
        struct MinFarg : glaze::ColorPicker {};
        MinFarg m;
        check(bld::applyCall(m, {"setColor", "gfx::Color{9, 8, 7, 255}"},
                             "preview_gate") &&
                  lika(m.color(), 9, 8, 7),
              "färgväljare: ärvd typ — gfx::Color-formen via Call", "");
        check(bld::applyCall(m, {"setColor", "\"#0A0B0C\""}, "preview_gate") &&
                  lika(m.color(), 10, 11, 12),
              "färgväljare: ärvd typ — hextexten via Call", "");
    }

    // HELA KEDJAN: text → reader → modell → satsvägen.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::ColorPicker farg;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    farg.name = \"farg\";\n"
        "    farg.setColor(\"#FF9800\");\n"
        "    root().adopt(farg);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "färgväljare: fixturen läses", err);
    if (läst) {
        auto f = build("glaze::ColorPicker", {});
        if (!f)
            return;
        int props = 0;
        for (const bld::Stmt& s : m.stmts) {
            if (s.kind == bld::Stmt::Kind::Prop && s.name == "color")
                ++props;
            if (s.target == "farg")
                bld::applyStmt(*f, s, "preview_gate");
        }
        auto* fp = dynamic_cast<glaze::ColorPicker*>(f.get());
        check(props == 1, "färgväljare: setColor normaliseras till Prop{color}",
              std::to_string(props));
        check(fp && lika(fp->color(), 0xff, 0x98, 0x00),
              "färgväljare: satsvägen sätter färgen", fp ? fp->colorText() : "");
    }

    // KLASSNINGEN: färgen är setter-fält bara här; arvet är Widgets.
    check(bld::isSetterField("glaze::ColorPicker", "color") &&
              !bld::isSetterField("glaze::Label", "color") &&
              !bld::isSetterField("glaze::ColorPicker", "text"),
          "färgväljare: color är setter-fält, Labels color och text är det inte");
    check(bld::isOrDerivesFrom("glaze::ColorPicker", "glaze::Widget") &&
              !bld::isOrDerivesFrom("glaze::ColorPicker", "glaze::Panel"),
          "färgväljare: ärver Widget, inte Panel");
}

// ================================================== DATUMVÄLJAREN
//
// Regel 8: datumet och gränserna är CALL-satser i den platta formen — tre
// heltal — och previewn ska UTFÖRA dem i källordning och ge det tillstånd C++
// ger: klampat vid läsning, så gränser före ett datum utanför dem ger det
// klampade. En TRASIG rad (fel antal, inte heltal) är OPAK och lämnar
// datumväljaren ORÖRD; ett datum som INTE FINNS är en väl formad rad som
// widgeten själv avvisar. Mätt på WIDGETENS tillstånd.
//
// VAD DEN INTE MÄTER: popupen, tangenterna och fältets tolkning — de bor i
// tests/datepicker_gate.cpp.
void fallDatumvaljare()
{
    using std::chrono::year_month_day;
    auto lika = [](year_month_day d, int y, unsigned m, unsigned dag) {
        return static_cast<int>(d.year()) == y &&
               static_cast<unsigned>(d.month()) == m &&
               static_cast<unsigned>(d.day()) == dag;
    };
    auto w = build("glaze::DatePicker", {});
    if (!w)
        return;
    auto* dp = dynamic_cast<glaze::DatePicker*>(w.get());
    check(dp != nullptr, "datumväljare: registret ger en riktig glaze::DatePicker",
          "");
    if (!dp)
        return;
    auto anrop = [&](const char* metod, const char* args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };
    check(anrop("setDate", "2026, 10, 8") && lika(dp->date(), 2026, 10, 8) &&
              dp->text() == "2026-10-08",
          "datumväljare: setDate(2026, 10, 8) utförs — fältet visar ISO",
          dp->text());
    check(anrop("setMinDate", "2026, 10, 1") &&
              anrop("setMaxDate", "2026, 10, 31") &&
              anrop("setDate", "2026, 12, 24") &&
              lika(dp->date(), 2026, 10, 31),
          "datumväljare: gränserna före ett datum utanför ⇒ det KLAMPADE",
          dp->text());
    check(anrop("setMaxDate", "+2026, 12, 31") && lika(dp->date(), 2026, 12, 24),
          "datumväljare: en vidgad gräns släpper fram det RÅA datumet (klampat "
          "vid läsning)", dp->text());
    check(anrop("setDate", "2026, 2, 30") && lika(dp->date(), 2026, 12, 24),
          "datumväljare: ett datum som inte finns — raden utförs, widgeten "
          "avvisar, datumet orört", dp->text());
    check(!anrop("setDate", "2026, 10") && !anrop("setDate", "2026, 10, x") &&
              !anrop("setDate", "2026, 10, 8, 1") &&
              !anrop("setDate", "std::chrono::year_month_day{}") &&
              !anrop("setMinDate", "\"2026-01-01\"") &&
              lika(dp->date(), 2026, 12, 24),
          "datumväljare: fel antal, inte heltal och kodens nästlade form ⇒ "
          "OPAK, datumet orört", dp->text());

    // En ANVÄNDARWIDGET som ärver datumväljaren — samma typade gren.
    {
        struct MittDatum : glaze::DatePicker {};
        MittDatum m;
        check(bld::applyCall(m, {"setDate", "2000, 2, 29"}, "preview_gate") &&
                  lika(m.date(), 2000, 2, 29),
              "datumväljare: ärvd typ — samma gren (skottdag)", m.text());
    }

    // HELA KEDJAN: text → reader → modell (Call-satser, källordning) →
    // satsvägen.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::DatePicker datum;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    datum.name = \"datum\";\n"
        "    datum.setDate(2030, 6, 15);\n"
        "    datum.setMaxDate(2029, 12, 31);\n"
        "    root().adopt(datum);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "datumväljare: fixturen läses", err);
    if (läst) {
        auto f = build("glaze::DatePicker", {});
        if (!f)
            return;
        int calls = 0;
        for (const bld::Stmt& s : m.stmts) {
            if (s.kind == bld::Stmt::Kind::Call && s.target == "datum")
                ++calls;
            if (s.target == "datum")
                bld::applyStmt(*f, s, "preview_gate");
        }
        auto* fp = dynamic_cast<glaze::DatePicker*>(f.get());
        check(calls == 2, "datumväljare: setDate/setMaxDate är Call-satser",
              std::to_string(calls));
        check(fp && lika(fp->date(), 2029, 12, 31),
              "datumväljare: satsvägen — datumet FÖRE gränsen ger det klampade",
              fp ? fp->text() : "");
    }

    // KLASSNINGEN: inga setter-fält; arvet är Widgets.
    check(!bld::isSetterField("glaze::DatePicker", "date") &&
              !bld::isSetterField("glaze::DatePicker", "minDate") &&
              !bld::isSetterField("glaze::DatePicker", "text"),
          "datumväljare: datumet och gränserna är Call-satser, inga setter-fält");
    check(bld::isOrDerivesFrom("glaze::DatePicker", "glaze::Widget") &&
              !bld::isOrDerivesFrom("glaze::DatePicker", "glaze::Panel"),
          "datumväljare: ärver Widget, inte Panel");
}

// ============================================ DET FLERRADIGA FÄLTET
//
// Regel 8: texten, skyddet och brytningen är SETTER-fält, och texten FÅR bära
// radbrytningar — `\n` är en vanlig escape i strängliteralen, som readern och
// writern redan bär åt båda hållen (`parseCppString`/`cppString`). Previewn
// ska UTFÖRA raderna och ge det tillstånd C++ ger: en verklig radbrytning i
// texten, och radsluten normaliserade (`\r\n` blir `\n`, som i `setText`).
// Ett översättnings-id är OPAKT — användarens text är ingen etikett
// (TextInputs regel). Mätt på WIDGETENS tillstånd, och hela kedjan (text →
// reader → modell → satsväg) en gång.
//
// VAD DEN INTE MÄTER: tangenterna, musen, rullningen och layouten — de bor i
// tests/textarea_gate.cpp; den byte-exakta round-trippen av en text med
// radbrytningar mäts av roundtrip_gate på fixturen (grupp 24).
void fallFlerradigt()
{
    auto w = build("glaze::TextArea", {{"text", "\"Rad 1\\nRad 2 \xC3\xA5\xC3\xA4\xC3\xB6\""},
                                       {"readOnly", "true"},
                                       {"wrap", "false"}});
    if (!w)
        return;
    auto* ta = dynamic_cast<glaze::TextArea*>(w.get());
    check(ta != nullptr, "flerradigt: registret ger en riktig glaze::TextArea",
          "");
    if (!ta)
        return;
    check(ta->text() == "Rad 1\nRad 2 \xC3\xA5\xC3\xA4\xC3\xB6",
          "flerradigt: setText med \\n utförs — en VERKLIG radbrytning",
          ta->text());
    check(ta->isReadOnly() && !ta->wrap(),
          "flerradigt: setReadOnly(true) och setWrap(false) utförs", "");
    bld::applyProp(*w, {"text", "\"a\\r\\nb\\rc\""}, "preview_gate");
    check(ta->text() == "a\nb\nc",
          "flerradigt: \\r\\n och \\r normaliseras till \\n, som i C++",
          ta->text());
    bld::applyProp(*w, {"text", "glaze::tr(1042)"}, "preview_gate");
    check(ta->text() == "a\nb\nc",
          "flerradigt: glaze::tr(…) är OPAKT — användarens text är ingen "
          "etikett",
          ta->text());

    // HELA KEDJAN: text → reader → modell → satsvägen.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::TextArea notes;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    notes.name = \"notes\";\n"
        "    notes.setText(\"Inköp:\\n- mjölk\\n- \\\"bröd\\\"\");\n"
        "    notes.setReadOnly(true);\n"
        "    notes.setWrap(false);\n"
        "    root().adopt(notes);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "flerradigt: fixturen läses", err);
    if (läst) {
        auto f = build("glaze::TextArea", {});
        if (!f)
            return;
        int props = 0;
        for (const bld::Stmt& s : m.stmts) {
            if (s.kind == bld::Stmt::Kind::Prop &&
                (s.name == "text" || s.name == "readOnly" || s.name == "wrap"))
                ++props;
            if (s.target == "notes")
                bld::applyStmt(*f, s, "preview_gate");
        }
        auto* fp = dynamic_cast<glaze::TextArea*>(f.get());
        check(props == 3,
              "flerradigt: setText/setReadOnly/setWrap normaliseras till Prop",
              std::to_string(props));
        check(fp && fp->text() == "Ink\xC3\xB6p:\n- mj\xC3\xB6lk\n- \"br\xC3\xB6" "d\"" &&
                  fp->isReadOnly() && !fp->wrap(),
              "flerradigt: satsvägen ger texten med radbrytningar och "
              "citattecken, skyddet och brytningen",
              fp ? fp->text() : "");
    }

    // KLASSNINGEN: tre setter-fält; arvet är Panels (clip/border följer med).
    check(bld::isSetterField("glaze::TextArea", "text") &&
              bld::isSetterField("glaze::TextArea", "readOnly") &&
              bld::isSetterField("glaze::TextArea", "wrap") &&
              bld::isSetterField("glaze::TextArea", "border"),
          "flerradigt: text/readOnly/wrap är setter-fält, och Panels border");
    check(bld::isOrDerivesFrom("glaze::TextArea", "glaze::Panel"),
          "flerradigt: ärver Panel");
}

// ===================================================== TOOLTIPEN
//
// `setToolTip` är basens textfält: setter-fältet `toolTip` på VARJE typ, och
// både literalen och översättnings-id:t ska nå widgeten. Id:t går genom
// `Widget::setTextToken` — och textwidgetarnas overrides MÅSTE kedja till
// basen, annars blir `knapp.setToolTip(glaze::tr(5));` opak just på de
// widgets som har en egen text. Det är det andra blocket nedan som mäter det.
void fallTooltip()
{
    glaze::setLanguage("sv");
    for (const char* typ :
         {"glaze::Button", "glaze::Panel", "glaze::ToolButton",
          "glaze::TreeView", "glaze::Label", "glaze::GroupBox",
          "glaze::TabPage"}) {
        auto w = build(typ, {{"toolTip", "\"Spara filen\""}});
        if (!w)
            continue;
        check(w->toolTip() == "Spara filen",
              std::string("tooltip: literalen når ") + typ, w->toolTip());
        bld::applyProp(*w, {"toolTip", "glaze::tr(2)"}, "preview_gate");
        check(w->toolTipToken().id() == 2 && w->toolTip() == "Avbryt",
              std::string("tooltip: glaze::tr(N) sätter TOKENET på ") + typ +
                  " (kedjad setTextToken)",
              w->toolTip());
    }
    // Den egna texten och tooltipen är SKILDA fält — tr på det ena rör
    // inte det andra.
    {
        glaze::Button b("Knapp");
        bld::applyProp(b, {"toolTip", "glaze::tr(3)"}, "preview_gate");
        check(b.text() == "Knapp" && b.toolTipToken().id() == 3,
              "tooltip: knappens text står kvar när tooltipen får ett id",
              b.text());
    }
    check(bld::isSetterField("glaze::Spacer", "toolTip") &&
              bld::isSetterField("glaze::TreeView", "toolTip") &&
              !bld::isSetterField("MinEgenTyp", "toolTip"),
          "tooltip: setter-fält på varje glaze-typ, inte på en okänd typ");
}

// ====================================================== FLIKARNA
//
// Regel 8 åt PREVIEW-hållet, hela KEDJAN: text → reader → modell → previewns
// satsväg → widgetarnas tillstånd. Flikarna är det första fallet där
// TRÄDBYGGET bär semantik — sidorna är barn, och vilken sida som syns följer
// av `setSelectedIndex` OCH adopt-raderna tillsammans — så fallet kör hela
// satslistan i KÄLLORDNING, adopt-raderna med, precis som trädbyggarna gör.
// `setSelectedIndex(1);` står i widgetens block, FÖRE trädbygget: valet ska
// gälla när den andra sidan kommer (satsordningen ska inte avgöra).
//
// VAD DEN INTE MÄTER: beteendet i ett fönster (tryck, tangenter, fokus) —
// det bor i tests/tab_gate.cpp.
void fallFlikar()
{
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::TabControl flikar;\n"
        "    glaze::TabPage sidaA;\n"
        "    glaze::TabPage sidaB;\n"
        "    glaze::Button knapp;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    flikar.name = \"flikar\";\n"
        "    flikar.setSelectedIndex(1);\n"
        "    flikar.setClosable(true);\n"
        "    sidaA.name = \"sidaA\";\n"
        "    sidaA.setText(\"Allmänt\");\n"
        "    sidaB.name = \"sidaB\";\n"
        "    sidaB.setText(glaze::tr(2));\n"
        "    sidaB.setBorder(true);\n"
        "    knapp.name = \"knapp\";\n"
        "    knapp.setText(\"Spara\");\n"
        "    flikar.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { "
        "flikBytt(s, e); };\n"
        "    flikar.onTabClose = [this] { stang(); };\n"
        "    root().adopt(flikar);\n"
        "    flikar.adopt(sidaA);\n"
        "    flikar.adopt(sidaB);\n"
        "    sidaB.adopt(knapp);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "flikar: fixturen läses", err);
    if (!läst)
        return;

    // KLASSNINGEN: valet, stängknapparna och sidans rubrik är setter-FÄLT
    // (Prop), sidans ram Panels; båda eventen är KOPPLINGAR — den korta
    // formen bara därför att readern känner `onTabClose`.
    int props = 0, events = 0, korta = 0, adopt = 0;
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Prop &&
            (s.name == "selectedIndex" || s.name == "closable" ||
             s.name == "text" || s.name == "border"))
            ++props;
        if (s.kind == bld::Stmt::Kind::Event) {
            ++events;
            if (s.shortForm)
                ++korta;
        }
        if (s.kind == bld::Stmt::Kind::Adopt)
            ++adopt;
    }
    check(props == 6, "flikar: valet, stängbarheten, rubrikerna och ramen "
                      "normaliseras till fält (Prop)", std::to_string(props));
    check(events == 2 && korta == 1,
          "flikar: onChange (full form) och onTabClose (kort form) blir EVENTS",
          std::to_string(events) + "/" + std::to_string(korta));
    check(adopt == 4, "flikar: trädbyggets fyra adopt-rader läses",
          std::to_string(adopt));

    // PREVIEWN: widgetarna ur registret, satserna i KÄLLORDNING.
    auto f  = build("glaze::TabControl", {});
    auto pa = build("glaze::TabPage", {});
    auto pb = build("glaze::TabPage", {});
    auto kn = build("glaze::Button", {});
    if (!f || !pa || !pb || !kn)
        return;
    auto* tc = dynamic_cast<glaze::TabControl*>(f.get());
    auto* a  = dynamic_cast<glaze::TabPage*>(pa.get());
    auto* b  = dynamic_cast<glaze::TabPage*>(pb.get());
    check(tc && a && b, "flikar: registret ger riktiga TabControl/TabPage", "");
    if (!tc || !a || !b)
        return;
    glaze::setLanguage("sv");
    auto widget = [&](const std::string& id) -> glaze::Widget* {
        return id == "flikar" ? f.get()
             : id == "sidaA"  ? pa.get()
             : id == "sidaB"  ? pb.get()
             : id == "knapp"  ? kn.get()
                              : nullptr;
    };
    for (const bld::Stmt& s : m.stmts) {
        if (s.kind == bld::Stmt::Kind::Adopt) {
            glaze::Widget* barn = widget(s.target);
            glaze::Widget* far  = widget(s.name);
            if (barn && far)
                far->adopt(*barn);   // lånad — previewträdets form
            continue;
        }
        if (glaze::Widget* w = widget(s.target))
            bld::applyStmt(*w, s, "preview_gate");
    }
    check(tc->pageCount() == 2 && tc->page(0) == a && tc->page(1) == b,
          "flikar: sidorna är flikkontrollens barn i adopt-ordning", "");
    check(tc->selectedIndex() == 1 && tc->selectedPage() == b,
          "flikar: setSelectedIndex FÖRE trädbygget gäller när sidan kommer",
          std::to_string(tc->selectedIndex()));
    check(!a->isVisible() && b->isVisible() && kn->isVisible(),
          "flikar: den valda sidan syns, den andra är dold", "");
    check(tc->isClosable(), "flikar: setClosable(true) når flikkontrollen", "");
    check(a->text() == "Allmänt" && b->textToken().id() == 2 &&
              b->text() == "Avbryt",
          "flikar: sidans rubrik — literalen OCH glaze::tr(N) (tokenet)",
          b->text());
    check(b->isBorder(), "flikar: sidan är en Panel — setBorder når den", "");
    check(!tc->setProp("title", "x") && !a->setProp("title", "x"),
          "flikar: inget title-alias på någon av dem", "");

    // Borttagningen genom trädets eget API — valet FÖLJER sidan.
    check(f->unadopt(*pa) && tc->pageCount() == 1 && tc->selectedIndex() == 0 &&
              tc->selectedPage() == b && pa->isVisible(),
          "flikar: unadopt av en sida FÖRE den valda — valet följer sidan, och "
          "den borttagna lämnar synlig", std::to_string(tc->selectedIndex()));

    // Klassningen mot arvstabellen: sidan är en Panel, flikkontrollen inte.
    check(bld::isOrDerivesFrom("glaze::TabPage", "glaze::Panel") &&
              !bld::isOrDerivesFrom("glaze::TabControl", "glaze::Panel"),
          "flikar: TabPage härleds ur Panel, TabControl inte", "");
    check(!bld::isSetterField("glaze::TabControl", "text") &&
              !bld::isSetterField("glaze::TabControl", "border"),
          "flikar: flikkontrollen har ingen text- och ingen ram-sättare", "");
}

// ================================================ RITYTAN
//
// Regel 8: kameran, lagren och FORMLISTAN i `.glazeui` ska UTFÖRAS av
// previewn och ge exakt det tillstånd C++-raden ger. Mätt på WIDGETENS
// tillstånd (formerna läses tillbaka med `Canvas::shape`), aldrig på
// modellen. Varje form i literaldelmängden (apply_props.cpp) — alla
// fabriker, Stroke/TextStyle/MarkerStyle designerade och positionella, Paint
// som färg och gradient, mått i px/mm/world, enumprefixen, `glaze::img`,
// `glaze::tr`, en väg med och utan typnamnet — och de ATOMISKA avslagen: fel
// fältordning, okänt fält,
// blandad form, fel enumtyp, en kontur som bar klammerlista till en optional,
// en tom punktlista till polygon, ett oktalt tal, ett avslutande komma i ett
// anrop och en formlista vars SISTA element är fel — allt lämnar ritytan
// orörd. Sist hela kedjan: text → reader (den korta `onPaint`-kopplingen är
// en koppling, inte en opak rad) → modell → satsvägen.
// BILD OCH TEXT: bilden på FYRA FRIA HÖRN med fem och sex argument
// (fyra och sju avvisas), TEXTDELARNA designerade och positionella med
// `glaze::tr`, färg, fet, kursiv, `std::nullopt` och `{}`, `{"abc"}` som
// vanlig text — och avslagen `{}`, `{{"abc"}}`, `{{}}`, fel designerarordning,
// okänt fält och ett fel i den SISTA delen. Sabotage som ska fälla dem:
// `{}` godtagen, ordningskontrollen
// bort, `{{"abc"}}` godtagen som delar, `{"abc"}` som delar.
// Lagrets snap (`setLayerSnap(lager, på)`) av och på, och en rad
// utan giltig flagga opak. Sabotage: grenen bort ur apply_props
// (raden blir opak — fallet och fallCallsRityta fäller), flaggan ignorerad i
// `Canvas::setLayerSnap` (`layerSnap` svarar fortfarande sant).
//
// INTE HÄR: att formerna RITAS rätt (scene_pixel_gate, painter_gate) och att
// raderna kompilerar (calls_fixture) och round-trippar (roundtrip_gate).
void fallCanvas()
{
    auto w = build("glaze::Canvas", {});
    if (!w)
        return;
    auto* c = dynamic_cast<glaze::Canvas*>(w.get());
    check(c != nullptr, "rityta: registret ger en riktig glaze::Canvas", "");
    if (!c)
        return;
    const auto anrop = [&](const char* metod, const std::string& args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };
    const auto prop = [&](const char* fält, const char* värde) {
        bld::applyProp(*w, {fält, värde}, "preview_gate");
    };
    const auto lika = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };

    // ---- SETTER-FÄLTEN (applyProp, som readern normaliserar dem till).
    prop("zoom", "2.5");
    prop("bearing", "15");
    prop("pitch", "30");
    prop("maxPitch", "50");
    prop("gridSize", "20");
    prop("border", "false");
    prop("yUp", "true");
    prop("pannable", "true");
    prop("zoomable", "true");
    prop("rotatable", "true");
    prop("pitchable", "true");
    prop("inertia", "false");
    prop("labelFade", "true");
    check(c->zoom() == 2.5 && c->bearing() == 15.0f && c->pitch() == 30.0f &&
              c->maxPitch() == 50.0f && c->gridSize() == 20.0 && !c->border() &&
              c->yUp() && c->pannable() && c->zoomable() && c->rotatable() &&
              c->pitchable() && !c->inertia() && c->labelFade(),
          "rityta: setter-fälten — kamerans tal, rutnätet, kanten, y, gesterna "
          "och toningen", "");
    // VRIDSNÄPPET: förvalet är bara norr inom 7°, och ett värde
    // utanför spannet lämnar det gällande orört (sättaren loggar).
    const float snäppFöre = c->rotationSnap(), tolFöre = c->rotationSnapTolerance();
    prop("rotationSnap", "90");
    prop("rotationSnapTolerance", "4.5");
    const bool snäppSatt =
        c->rotationSnap() == 90.0f && c->rotationSnapTolerance() == 4.5f;
    prop("rotationSnap", "400");
    prop("rotationSnapTolerance", "-1");
    check(snäppFöre == 360.0f && tolFöre == 7.0f && snäppSatt &&
              c->rotationSnap() == 90.0f && c->rotationSnapTolerance() == 4.5f,
          "rityta: vridsnäppets setter-fält — förvalet norr/7°, 90 och 4,5 "
          "sätts, 400 och −1 avvisas", "");

    // ---- KAMERAN och YTAN.
    check(anrop("setCenter", "674000, 6580000.5") &&
              c->center().x == 674000.0 && c->center().y == 6580000.5,
          "rityta: setCenter(x, y) i double", "");
    check(anrop("setZoomRange", "0.5, 40") && c->minZoom() == 0.5 &&
              c->maxZoom() == 40.0,
          "rityta: setZoomRange(min, max)", "");
    check(anrop("setBackground", "gfx::Color{240, 240, 230}") &&
              c->hasBackground(),
          "rityta: setBackground(gfx::Color{…})", "");
    check(anrop("setCamera",
                "glaze::Camera{.center = {674100, 6580050}, .zoom = 3, "
                ".bearing = 10, .pitch = 20}") &&
              c->center().x == 674100.0 && c->center().y == 6580050.0 &&
              c->zoom() == 3.0 && c->bearing() == 10.0f && c->pitch() == 20.0f,
          "rityta: setCamera(glaze::Camera{…}) designerad", "");
    // Med 3D-kameran är `setCamera` ÖVERLAGRAD: en bar
    // POSITIONELL lista är tvetydig och kompilerar inte (avvisas, fall3D mäter
    // det), medan en bar DESIGNERAD lista väljer den typ som har namnen.
    check(anrop("setCamera", "{.center = {1, 2}, .zoom = 4}") &&
              c->center().x == 1.0 && c->zoom() == 4.0 && !c->is3D(),
          "rityta: setCamera({.center …}) — bar designerad lista är kartkameran",
          "");
    check(anrop("setMapScale", "2000") && anrop("setMapScale", "500, 0.5") &&
              anrop("zoomToRect", "glaze::Rect{0, 0, 100, 50}, glaze::mm(5)") &&
              anrop("zoomToRect", "{0, 0, 10, 10}"),
          "rityta: setMapScale och zoomToRect utförs (väntar på fönstret)", "");

    // ---- LAGREN — kollektionsvokabulären.
    check(anrop("addLayer", "\"Vägar\"") && anrop("addLayer", "glaze::tr(2)") &&
              c->layerCount() == 3 && c->layerName(1) == "Vägar",
          "rityta: addLayer med literal och tr-token",
          std::to_string(c->layerCount()));
    check(anrop("setLayer", "2, \"Hus\"") && c->layerName(2) == "Hus" &&
              anrop("setLayerVisible", "1, false") && !c->layerVisible(1) &&
              anrop("setLayerOpacity", "1, 0.5") && c->layerOpacity(1) == 0.5f &&
              anrop("setLayerCollision", "2, false") && !c->layerCollision(2) &&
              anrop("setLayerDrawOrder", "2, glaze::DrawOrder::ByKind") &&
              c->layerDrawOrder(2) == glaze::DrawOrder::ByKind &&
              anrop("setLayerZoomRange", "1, 0.5, 8"),
          "rityta: setLayer och lagrens egenskaper", "");
    // HÖRNEN OCH KANTERNA i träffprovet: på från start,
    // `setLayerSnap` slår av och på, och en rad utan giltig flagga är opak och
    // lämnar lagret orört.
    const bool snapFöre = c->layerSnap(1);
    const bool snapAv   = anrop("setLayerSnap", "1, false") && !c->layerSnap(1);
    check(snapFöre && snapAv && !anrop("setLayerSnap", "1") &&
              !anrop("setLayerSnap", "1, kanske") && !c->layerSnap(1) &&
              anrop("setLayerSnap", "1, true") && c->layerSnap(1),
          "rityta: setLayerSnap(lager, på) — på från start, av och på igen; "
          "en rad utan giltig flagga är opak",
          "");
    check(anrop("removeLayer", "2") && c->layerCount() == 2,
          "rityta: removeLayer", std::to_string(c->layerCount()));

    // ---- FORMERNA — varje fabrik, mätt på formen ritytan fick.
    const auto senaste = [&](int lager) -> const glaze::Shape* {
        return c->shape(lager, c->shapeCount(lager) - 1);
    };
    check(anrop("addShape",
                "1, glaze::Shape::polyline({{674000, 6580000}, {674120, "
                "6580040}}, glaze::Stroke{.paint = gfx::Color{200, 60, 40}, "
                ".width = glaze::mm(0.7), .cap = glaze::LineCap::Square, "
                ".join = glaze::LineJoin::Miter, .miterLimit = 6, .dash = {4, "
                "2}, .dashUnit = glaze::Unit::Mm, .dashOffset = 1.5, "
                ".hitInside = true})"),
          "rityta: addShape(lager, polyline med hel Stroke)", "");
    if (const glaze::Shape* s = senaste(1)) {
        const glaze::Stroke& st = s->stroke();
        check(s->kind() == glaze::Shape::Kind::Lines &&
                  s->contours().size() == 1 &&
                  s->contours()[0].points.size() == 2 &&
                  s->contours()[0].points[1].x == 674120.0 &&
                  lika(st.paint.color(), gfx::Color{200, 60, 40, 255}) &&
                  st.width.value == static_cast<float>(0.7) &&
                  st.width.unit == glaze::Unit::Mm &&
                  st.cap == glaze::LineCap::Square &&
                  st.join == glaze::LineJoin::Miter && st.miterLimit == 6.0f &&
                  st.dash == std::vector<float>{4.0f, 2.0f} &&
                  st.dashUnit == glaze::Unit::Mm && st.dashOffset == 1.5f &&
                  st.hitInside,
              "rityta: polylinjens punkter (double) och varje Stroke-fält", "");
    }
    check(anrop("addShape",
                "glaze::Shape::line({0, 0}, {10, 5}, {.paint = gfx::Color{0, "
                "0, 255}, .width = 3})") &&
              senaste(0) && senaste(0)->stroke().width.value == 3.0f &&
              senaste(0)->stroke().width.unit == glaze::Unit::Px,
          "rityta: line med bar designerad Stroke och naket tal (px)", "");
    check(anrop("addShape",
                "glaze::Shape::arc({50, 50}, 20, 0, 90, glaze::Stroke{gfx::Color{"
                "0, 128, 0}, glaze::world(2)})") &&
              senaste(0) && senaste(0)->stroke().width.unit == glaze::Unit::World &&
              senaste(0)->contours()[0].points.size() > 2,
          "rityta: arc med POSITIONELL Stroke och världsbredd", "");
    check(anrop("addShape",
                "glaze::Shape::polygon({{0, 0}, {40, 0}, {40, 30}}, "
                "gfx::Color{255, 0, 0}, glaze::Stroke{.width = 2})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Area &&
              senaste(0)->contours().size() == 1 && senaste(0)->hasStroke(),
          "rityta: polygon med punkter, färg och kontur", "");
    check(anrop("addShape",
                "glaze::Shape::polygon({{{0, 0}, {40, 0}, {40, 40}, {0, 40}}, "
                "{{10, 10}, {20, 10}, {20, 20}}}, glaze::Paint::linear({0, 0}, "
                "{40, 0}, {{0, gfx::Color{255, 0, 0}}, {1, gfx::Color{0, 0, "
                "255}}}), std::nullopt)") &&
              senaste(0) && senaste(0)->contours().size() == 2 &&
              senaste(0)->fill().kind() == gfx::PaintKind::Linear &&
              senaste(0)->fill().stops().size() == 2 &&
              senaste(0)->fill().to().x == 40.0 && !senaste(0)->hasStroke(),
          "rityta: polygon med RINGAR (hål), linjär gradient och std::nullopt",
          "");
    check(anrop("addShape",
                "glaze::Shape::rect(glaze::Rect{5, 5, 20, 10}, "
                "glaze::Paint::radial({15, 10}, 0, 12, {{.offset = 0, .color = "
                "gfx::Color{255, 255, 255}}, {.offset = 1, .color = "
                "gfx::Color{0, 0, 0}}}))") &&
              senaste(0) && senaste(0)->fill().kind() == gfx::PaintKind::Radial &&
              senaste(0)->fill().r1() == 12.0,
          "rityta: rect med radiell gradient och designerade stopp", "");
    check(anrop("addShape",
                "glaze::Shape::ellipse({100, 100}, 30, 15, gfx::Color{0, 255, "
                "0, 128})") &&
              anrop("addShape",
                    "glaze::Shape::circle({100, 100}, 8, gfx::Color{}, "
                    "glaze::Stroke{.paint = gfx::Color{255, 255, 255}})") &&
              senaste(0) && senaste(0)->hasStroke(),
          "rityta: ellipse och circle (färg med 0 kanaler)", "");
    check(anrop("addShape",
                "glaze::Shape::path(glaze::Path{{glaze::PathVerb::Move, {0, 0}}, "
                "{glaze::PathVerb::Line, {40, 0}}, {glaze::PathVerb::Cubic, {60, "
                "0}, {60, 30}, {40, 30}}, {glaze::PathVerb::Close}}, "
                "gfx::Color{0, 0, 0})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Area &&
              anrop("addShape",
                    "glaze::Shape::path(glaze::Path{{glaze::PathVerb::Move, {0, "
                    "0}}, {glaze::PathVerb::Quad, {10, 20}, {20, 0}}}, "
                    "{.width = 2})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Lines,
          "rityta: path som yta (Cubic, Close) och som kontur (Quad)", "");
    check(anrop("addShape",
                "glaze::Shape::path({{glaze::PathVerb::Move, {0, 0}}, "
                "{glaze::PathVerb::Line, {10, 10}}}, {.width = 1})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Lines &&
              senaste(0)->contours().size() == 1 &&
              senaste(0)->contours()[0].points.size() == 2,
          "rityta: path med bar klammerlista — parametern har typen", "");
    check(anrop("addShape",
                "glaze::Shape::text({674060, 6580030}, glaze::tr(2), "
                "glaze::TextStyle{.size = glaze::mm(2.5), .family = "
                "gfx::FontFamily::Mono, .bold = true, .halo = gfx::Color{255, "
                "255, 255}, .anchor = glaze::TextAnchor::Bottom, .angle = 30, "
                ".rotation = glaze::Alignment::Map, .align = glaze::Align::Left, "
                ".priority = 3})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Text &&
              senaste(0)->text().id() == 2 &&
              senaste(0)->textStyle().size.unit == glaze::Unit::Mm &&
              senaste(0)->textStyle().family == gfx::FontFamily::Mono &&
              senaste(0)->textStyle().bold &&
              senaste(0)->textStyle().anchor == glaze::TextAnchor::Bottom &&
              senaste(0)->textStyle().angle == 30.0f &&
              senaste(0)->textStyle().rotation == glaze::Alignment::Map &&
              senaste(0)->textStyle().align == glaze::Align::Left &&
              senaste(0)->textStyle().priority == 3,
          "rityta: text med tr-token och TextStyle-fälten", "");
    check(anrop("addShape",
                "glaze::Shape::text({1, 2}, \"Storgatan\", {.size = 12, "
                ".anchor = gfx::TextAnchor::Top})") &&
              senaste(0) && senaste(0)->text().str() == "Storgatan" &&
              senaste(0)->textStyle().anchor == glaze::TextAnchor::Top,
          "rityta: text med literal och gfx::-prefixet på ankaret", "");
    check(anrop("addShape",
                "glaze::Shape::marker({5, 5}, glaze::MarkerStyle{.shape = "
                "glaze::MarkerShape::Square, .size = glaze::mm(3), .fill = "
                "gfx::Color{255, 0, 0}, .anchor = glaze::TextAnchor::Bottom, "
                ".img = glaze::img(\"toolbar\", \"info\"), .collide = true})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Markers &&
              senaste(0)->markerStyle().shape == glaze::MarkerShape::Square &&
              senaste(0)->markerStyle().anchor == glaze::TextAnchor::Bottom &&
              senaste(0)->markerStyle().img.kind() ==
                  glaze::ImageSource::Kind::Asset &&
              senaste(0)->markerStyle().collide,
          "rityta: marker med MarkerStyle — ankare och bild", "");

    // ---- TAKET, golvet OCH ZOOMEN: fälten SIST i
    // deklarationsordning; fel ordning och ett okänt namn ger en opak rad.
    check(anrop("addShape",
                "glaze::Shape::text({1, 2}, \"Anteckning\", glaze::TextStyle{"
                ".size = glaze::world(6), .wrapWidth = glaze::world(100), "
                ".maxLines = 3, .maxHeight = glaze::world(40), .minSize = "
                "glaze::mm(2), .maxSize = glaze::mm(6), .minZoom = 0.5, "
                ".maxZoom = 20})") &&
              senaste(0) && senaste(0)->textStyle().maxLines == 3 &&
              senaste(0)->textStyle().maxHeight.value == 40.0f &&
              senaste(0)->textStyle().maxHeight.unit == glaze::Unit::World &&
              senaste(0)->textStyle().minSize.value == 2.0f &&
              senaste(0)->textStyle().minSize.unit == glaze::Unit::Mm &&
              senaste(0)->textStyle().maxSize.value == 6.0f &&
              senaste(0)->textStyle().minZoom == 0.5 &&
              senaste(0)->textStyle().maxZoom == 20.0,
          "rityta: TextStyle med taket, golvet och zoomen", "");
    check(anrop("addShape",
                "glaze::Shape::marker({5, 5}, glaze::MarkerStyle{.size = "
                "glaze::world(3), .minSize = glaze::px(4), .maxSize = "
                "glaze::px(24), .minZoom = 1.5, .maxZoom = 9})") &&
              senaste(0) && senaste(0)->markerStyle().minSize.value == 4.0f &&
              senaste(0)->markerStyle().maxSize.unit == glaze::Unit::Px &&
              senaste(0)->markerStyle().maxSize.value == 24.0f &&
              senaste(0)->markerStyle().minZoom == 1.5 &&
              senaste(0)->markerStyle().maxZoom == 9.0,
          "rityta: MarkerStyle med golvet, taket och zoomen", "");
    check(!anrop("addShape",
                 "glaze::Shape::text({1, 2}, \"x\", glaze::TextStyle{.minSize = "
                 "glaze::mm(2), .maxLines = 3})") &&
              !anrop("addShape",
                     "glaze::Shape::text({1, 2}, \"x\", glaze::TextStyle{"
                     ".maxRader = 3})"),
          "rityta: taket i fel ordning och ett okänt fält är opaka", "");
    check(anrop("addShape",
                "glaze::Shape::points({{1, 1}, {2, 2}, {3, 3}}, {.size = 6, "
                ".img = {}})") &&
              senaste(0) && senaste(0)->points().size() == 3 &&
              senaste(0)->markerStyle().img.kind() == glaze::ImageSource::Kind::None,
          "rityta: points — ett punktmoln, `.img = {}` är INGEN bild", "");
    check(anrop("addShape",
                "glaze::Shape::image({0, 0, 256, 256}, glaze::img(\"toolbar\", "
                "\"info\"), {.opacity = 0.75})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Image &&
              senaste(0)->rect().w == 256.0 &&
              senaste(0)->imageStyle().opacity == 0.75f &&
              senaste(0)->imageStyle().saturation == 1.0f,
          "rityta: image med glaze::img och opacitet", "");

    // ---- BILDENS FÄRGJUSTERING: ImageStyle med typnamn, designerat
    // och positionellt; fabrikens normering (under 0 är 0) gäller också
    // previewn; fel ordning och ett okänt fält är opaka (listan nedan).
    check(anrop("addShape",
                "glaze::Shape::image({0, 0, 64, 64}, {}, glaze::ImageStyle{"
                ".opacity = 0.5, .saturation = 0, .contrast = 1.25, "
                ".brightness = 0.75})") &&
              senaste(0) && senaste(0)->imageStyle().opacity == 0.5f &&
              senaste(0)->imageStyle().saturation == 0.0f &&
              senaste(0)->imageStyle().contrast == 1.25f &&
              senaste(0)->imageStyle().brightness == 0.75f,
          "rityta: image med ImageStyle — opacitet, mättnad, kontrast, ljushet",
          "");
    check(anrop("addShape",
                "glaze::Shape::image({0, 0}, {100, 10}, {90, 90}, {10, 80}, {}, "
                "{1, 2, -1})") &&
              senaste(0) && senaste(0)->isImageQuad() &&
              senaste(0)->imageStyle().opacity == 1.0f &&
              senaste(0)->imageStyle().saturation == 2.0f &&
              senaste(0)->imageStyle().contrast == 0.0f &&
              senaste(0)->imageStyle().brightness == 1.0f,
          "rityta: image på fyra hörn med positionell ImageStyle — kontrasten "
          "−1 normeras till 0, som fabriken gör",
          "");

    // ---- BILDEN PÅ FYRA FRIA HÖRN och TEXTDELARNA.
    // Överlagringen avgörs av antalet argument (5–6 mot 2–3), och delarna av
    // klammerlistans form — {"abc"} är TEXTEN, som i C++.
    check(anrop("addShape",
                "glaze::Shape::image({0, 0}, {100, 10}, {90, 90}, {10, 80}, "
                "glaze::img(\"toolbar\", \"info\"))") &&
              senaste(0) && senaste(0)->isImageQuad() &&
              senaste(0)->points()[2].x == 90.0 &&
              senaste(0)->points()[3].y == 80.0 &&
              senaste(0)->imageStyle().opacity == 1.0f &&
              senaste(0)->image().kind() == glaze::ImageSource::Kind::Asset,
          "rityta: image på fyra hörn med fem argument (ringordning)", "");
    check(anrop("addShape",
                "glaze::Shape::image(glaze::Point{0, 0}, {100, 10}, {90, 90}, "
                "{10, 80}, {}, {.opacity = 0.5})") &&
              senaste(0) && senaste(0)->isImageQuad() &&
              senaste(0)->imageStyle().opacity == 0.5f &&
              senaste(0)->image().kind() == glaze::ImageSource::Kind::None,
          "rityta: image på fyra hörn med sex argument och {} som källa", "");
    if (anrop("addShape",
              "glaze::Shape::text({1, 2}, {glaze::TextSpan{.text = \"Stor\"}, "
              "{.text = glaze::tr(2), .color = gfx::Color{200, 0, 0}, .bold = "
              "true, .italic = false}}, glaze::TextStyle{.size = glaze::mm(2.5)})") &&
        senaste(0) && senaste(0)->spans().size() == 2) {
        const std::vector<glaze::TextSpan>& d = senaste(0)->spans();
        check(senaste(0)->kind() == glaze::Shape::Kind::Text &&
                  d[0].text.str() == "Stor" && !d[0].color && !d[0].bold &&
                  d[1].text.id() == 2 && d[1].color &&
                  lika(*d[1].color, gfx::Color{200, 0, 0, 255}) && d[1].bold &&
                  *d[1].bold && d[1].italic && !*d[1].italic &&
                  senaste(0)->textStyle().size.unit == glaze::Unit::Mm,
              "rityta: text i delar — designerade, tr-token, färg, fet och kursiv",
              "");
    } else {
        check(false, "rityta: text i delar — designerade, tr-token, färg, fet "
                     "och kursiv", "raden utfördes inte");
    }
    check(anrop("addShape",
                "glaze::Shape::text3({1, 2, 3}, {{\"a\", std::nullopt, true}, "
                "{\"b\", gfx::Color{0, 0, 255}, std::nullopt, {}}})") &&
              senaste(0) && senaste(0)->kind() == glaze::Shape::Kind::Text3 &&
              senaste(0)->spans().size() == 2 && !senaste(0)->spans()[0].color &&
              senaste(0)->spans()[0].bold && *senaste(0)->spans()[0].bold &&
              senaste(0)->spans()[1].color && !senaste(0)->spans()[1].bold &&
              !senaste(0)->spans()[1].italic,
          "rityta: text3 i delar — positionella, std::nullopt och {}", "");
    check(anrop("addShape", "glaze::Shape::text({1, 2}, {\"abc\"})") &&
              senaste(0) && senaste(0)->spans().empty() &&
              senaste(0)->text().str() == "abc",
          "rityta: {\"abc\"} är TEXTEN — C++ väljer TrText", "");

    // ---- setShape / setShapes / removeShape, med och utan lager.
    const int i0 = c->shapeCount(0);
    check(anrop("setShape",
                "0, glaze::Shape::circle({0, 0}, 1, gfx::Color{1, 2, 3})") &&
              c->shapeCount(0) == i0 && c->shape(0, 0) &&
              c->shape(0, 0)->kind() == glaze::Shape::Kind::Area,
          "rityta: setShape(index, form) ersätter på plats", "");
    check(anrop("setShape",
                "1, 0, glaze::Shape::marker({0, 0})") && c->shape(1, 0) &&
              c->shape(1, 0)->kind() == glaze::Shape::Kind::Markers,
          "rityta: setShape(lager, index, form)", "");
    check(anrop("setShapes",
                "1, {glaze::Shape::marker({0, 0}), glaze::Shape::marker({1, 1}), "
                "glaze::Shape::line({0, 0}, {1, 1})}") &&
              c->shapeCount(1) == 3,
          "rityta: setShapes(lager, {…})", std::to_string(c->shapeCount(1)));
    check(anrop("removeShape", "1, 0") && c->shapeCount(1) == 2 &&
              anrop("removeShape", "0") && c->shapeCount(0) == i0 - 1,
          "rityta: removeShape med och utan lager", "");

    // ---- DE ATOMISKA AVSLAGEN — ritytan orörd.
    const int    före0 = c->shapeCount(0), före1 = c->shapeCount(1);
    const auto   orörd = [&] {
        return c->shapeCount(0) == före0 && c->shapeCount(1) == före1;
    };
    const char* fel[] = {
        // fel fältordning (C++20 kräver deklarationsordningen)
        "glaze::Shape::line({0, 0}, {1, 1}, glaze::Stroke{.width = 2, "
        ".paint = gfx::Color{}})",
        // okänt fält
        "glaze::Shape::line({0, 0}, {1, 1}, {.colour = gfx::Color{}})",
        // blandat designerat och positionellt
        "glaze::Shape::line({0, 0}, {1, 1}, {gfx::Color{}, .width = 2})",
        // fel enumtyp (gfx::LineCap är en annan typ än glaze::LineCap)
        "glaze::Shape::line({0, 0}, {1, 1}, {.cap = gfx::LineCap::Round})",
        // en kontur som bar klammerlista till std::optional<Stroke>
        "glaze::Shape::polygon({{0, 0}, {1, 0}, {1, 1}}, gfx::Color{}, "
        "{.width = 2})",
        // en tom punktlista är tvetydig mellan polygonens överlagringar
        "glaze::Shape::polygon({}, gfx::Color{})",
        // ett oktalt tal läses inte som tio
        "glaze::Shape::circle({010, 0}, 1, gfx::Color{})",
        // en kanal utanför 0–255
        "glaze::Shape::circle({0, 0}, 1, gfx::Color{256})",
        // ett avslutande komma i ett ANROP är inte C++
        "glaze::Shape::circle({0, 0}, 1, gfx::Color{},)",
        // okänd fabrik
        "glaze::Shape::star({0, 0}, 5)",
        // för många argument
        "glaze::Shape::marker({0, 0}, {}, 1)",
        // BILDEN: fyra argument och sju — ingen överlagring tar dem
        "glaze::Shape::image({0, 0}, {1, 0}, {1, 1}, {0, 1})",
        "glaze::Shape::image({0, 0}, {1, 0}, {1, 1}, {0, 1}, {}, 1, 2)",
        // BILDENS STIL: ett bart tal är ingen ImageStyle, fälten i
        // fel ordning och ett okänt fält kompilerar inte
        "glaze::Shape::image({0, 0, 1, 1}, {}, 0.5)",
        "glaze::Shape::image({0, 0, 1, 1}, {}, {.contrast = 2, .saturation = 0})",
        "glaze::Shape::image({0, 0, 1, 1}, {}, {.gamma = 2})",
        // TEXTDELARNA: `{}`, `{{"abc"}}` och `{{}}` är tvetydiga mellan
        // strängen och delarna och kompilerar inte
        "glaze::Shape::text({0, 0}, {})",
        "glaze::Shape::text({0, 0}, {{\"abc\"}})",
        "glaze::Shape::text({0, 0}, {{}})",
        // en del med fältnamnen i fel ordning, och ett okänt fält
        "glaze::Shape::text({0, 0}, {{.color = gfx::Color{}, .text = \"a\"}})",
        "glaze::Shape::text({0, 0}, {{.text = \"a\", .farg = gfx::Color{}}})"};
    int orörda = 0;
    for (const char* f : fel)
        orörda += (!anrop("addShape", f) && orörd()) ? 1 : 0;
    const int antalFel = static_cast<int>(sizeof fel / sizeof fel[0]);
    check(orörda == antalFel,
          "rityta: " + std::to_string(antalFel) +
              " felaktiga former avvisas — OPAKA, ritytan orörd",
          std::to_string(orörda));
    // ATOMICITETEN: lagret parsas, formen inte — inget läggs till; en lista
    // vars SISTA form är fel — lagret behåller sina gamla former; en ersättning
    // vars form inte avkodas lämnar den gamla formen.
    const glaze::Shape::Kind slag0 =
        c->shape(0, 0) ? c->shape(0, 0)->kind() : glaze::Shape::Kind::None;
    check(!anrop("addShape", "1, glaze::Shape::circle({0, 0})") && orörd() &&
              !anrop("setShapes",
                     "1, {glaze::Shape::marker({0, 0}), glaze::Shape::star()}") &&
              orörd() &&
              !anrop("setShape", "0, glaze::Shape::circle({0, 0}, x, gfx::Color{})") &&
              c->shape(0, 0) && c->shape(0, 0)->kind() == slag0 &&
              slag0 != glaze::Shape::Kind::Area &&
              !anrop("addShape", "1, glaze::Shape::text({0, 0}, {{.text = \"a\"}, "
                                 "{.text = \"b\", .bold = kanske}})") &&
              orörd(),
          "rityta: ett fel i SISTA argumentet, sista formen eller sista "
          "textdelen muterar ingenting", "");
    const glaze::Point mitt = c->center();
    check(!anrop("setCenter", "1, x") && !anrop("setCenter", "1") &&
              !anrop("setLayerDrawOrder", "0, glaze::DrawOrder::Foo") &&
              !anrop("addLayer", "\"a\",") && !anrop("setBackground", "\"#FF0000\"") &&
              !anrop("setCamera", "glaze::Camera{.zoom = 2, .center = {0, 0}}") &&
              c->center().x == mitt.x && c->layerCount() == 2,
          "rityta: kamerans och lagrens felaktiga rader är opaka", "");

    // ---- HELA KEDJAN: text → reader → modell → satsvägen. Den KORTA
    // `onPaint`-kopplingen är ett event (isKnownEvent), inte en opak rad.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::Canvas karta;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    karta.name = \"karta\";\n"
        "    karta.setYUp(true);\n"
        "    karta.setPannable(true);\n"
        "    karta.setCenter(674000, 6580000);\n"
        "    karta.addLayer(\"Vägar\");\n"
        "    karta.addShape(1, glaze::Shape::polyline({{674000, 6580000}, "
        "{674120, 6580040}}, glaze::Stroke{.paint = gfx::Color{200, 60, 40}, "
        ".width = glaze::mm(0.7)}));\n"
        "    karta.addShape(glaze::Shape::text({674060, 6580030}, glaze::tr(2), "
        "glaze::TextStyle{.size = glaze::mm(2.5), .bold = true}));\n"
        "\n"
        "    karta.onPaint = [this] { rita(); };\n"
        "    karta.onCameraChange = [this](glaze::Widget* s, glaze::ChangeArgs& "
        "e) { flyttad(s, e); };\n"
        "\n"
        "    root().adopt(karta);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "rityta: fixturen läses", err);
    if (läst) {
        auto k = build("glaze::Canvas", {});
        if (!k)
            return;
        int props = 0, calls = 0, events = 0;
        for (const bld::Stmt& s : m.stmts) {
            if (s.target != "karta")
                continue;
            props += s.kind == bld::Stmt::Kind::Prop ? 1 : 0;
            calls += s.kind == bld::Stmt::Kind::Call ? 1 : 0;
            events += s.kind == bld::Stmt::Kind::Event ? 1 : 0;
            bld::applyStmt(*k, s, "preview_gate");
        }
        auto* kc = dynamic_cast<glaze::Canvas*>(k.get());
        check(calls == 4 && events == 2,
              "rityta: setCenter/addLayer/addShape är Call-satser och BÅDA "
              "kopplingarna är events (den korta onPaint)",
              std::to_string(props) + " prop, " + std::to_string(calls) +
                  " call, " + std::to_string(events) + " event");
        check(kc && kc->yUp() && kc->pannable() && kc->layerCount() == 2 &&
                  kc->shapeCount(1) == 1 && kc->shapeCount(0) == 1 &&
                  kc->center().x == 674000.0,
              "rityta: satsvägen — y, gesten, lagret och formerna i rätt lager",
              kc ? std::to_string(kc->shapeCount(0)) + "/" +
                       std::to_string(kc->shapeCount(1))
                 : "");
    }
    check(bld::isOrDerivesFrom("glaze::Canvas", "glaze::Widget") &&
              !bld::isOrDerivesFrom("glaze::Canvas", "glaze::Panel"),
          "rityta: ärver Widget, inte Panel", "");
}

// LAGRENS ORDNING — `moveLayer(från, till)` är en
// metodanropssats med två heltal och ett TILLSTÅND, så previewn UTFÖR den.
// Förväntat är lagrens namn efter flytten. Fel antal argument eller ett
// argument som inte är ett heltal är en opak rad och lämnar ritytan orörd;
// ett lager utanför är VÄLFORMAT — utfört, ritytans egen logg och ingen
// ändring, som i C++. Fixturens rad (`karta.moveLayer(2, 0);`) utförs av
// `fallCallsRityta`, som fälls om grenen saknas i `kMetoder`.
void fallLagerordning()
{
    auto w = build("glaze::Canvas", {});
    if (!w)
        return;
    auto* c = dynamic_cast<glaze::Canvas*>(w.get());
    check(c != nullptr, "lagerordning: registret ger en riktig glaze::Canvas", "");
    if (!c)
        return;
    const auto anrop = [&](const char* metod, const std::string& args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };
    const auto namnen = [&] {
        std::string s;
        for (int i = 0; i < c->layerCount(); ++i)
            s += (i ? "," : "") + c->layerName(i);
        return s;
    };
    check(anrop("setLayers", "{\"A\", \"B\", \"C\"}") && anrop("moveLayer", "2, 0") &&
              namnen() == "C,A,B",
          "lagerordning: moveLayer(2, 0) utförs — C underst", namnen());
    check(!anrop("moveLayer", "1") && !anrop("moveLayer", "1, x") &&
              !anrop("moveLayer", "0, 1, 2") && namnen() == "C,A,B",
          "lagerordning: fel antal eller ett argument som inte är ett heltal — "
          "opak rad, ritytan orörd",
          namnen());
    check(anrop("moveLayer", "9, 0") && namnen() == "C,A,B",
          "lagerordning: ett lager utanför är välformat — utfört, ritytans egen "
          "logg och ingen ändring",
          namnen());
}

// ============================================ RITYTAN I RYMDEN
//
// Regel 8 för rymden: varje rad `.glazeui` bär för ritytans 3D ska UTFÖRAS av
// previewn och ge exakt det tillstånd C++-raden ger — mätt på WIDGETENS
// tillstånd (`camera3D()`, `lighting()`, `view(i)`, `layerRenderState(i)`,
// formerna genom `Canvas::shape`), aldrig på modellen. Setter-fälten
// (provtagningen, djupet, tumlingen), 3D-kameran i varje form C++ tar —
// typad positionell och designerad, `lookFrom` och en BAR DESIGNERAD lista
// (överlagringen väljs på namnen) — ljuset, lagrens placering, renderläge och
// vyer, del-vyerna i kollektionsvokabulären, orbitens spann, tickningen,
// inramningen och varje fabrik i rymden med varje stilfält. Och AVSLAGEN: en
// bar POSITIONELL kamera och `{}` (tvetydiga mellan överlagringarna — de
// kompilerar inte), designerare i FEL ORDNING i varje aggregat,
// `gfx::RenderState` där lagrets tas, fel enumtyp, ett negativt index i en
// mesh, koddatats `pointCloud` — ATOMISKT: ett fel i SISTA argumentet eller i
// en listas sista element muterar ingenting. Handlingarna (`orbitBy`/`panBy`/
// `dollyBy`/`easeTo`/`renderToBitmap`) utförs inte. Sist hela kedjan: text →
// reader (`onTick` i den korta formen är en KOPPLING) → modell → satsvägen.
//
// Referensen är OBEROENDE av implementationen där det går: lookFroms mål räknas
// med grindens egen trigonometri ur camera.h:s konvention (azimut = kompass,
// z upp), lådans placering ur dess hörn, och förvalen ur headerns egna
// aggregat (`glaze::Lighting{}` …) — aldrig ur avkodarens tabeller.
//
// INTE HÄR: att formerna RITAS rätt (scene3d_pixel_gate, pixel_gate grupp 27),
// att raderna kompilerar (calls_fixture) och round-trippar (roundtrip_gate),
// att ett namngivet program ritas med reserven (ramverkets väg,
// scene3d_pixel_gate del C2), och BRACE ELISION — `glaze::Box3{0, 0, 0, 1, 1,
// 1}` kompilerar men ligger utanför delmängden, som i 2D-literalerna.
void fall3D()
{
    auto w = build("glaze::Canvas", {});
    if (!w)
        return;
    auto* c = dynamic_cast<glaze::Canvas*>(w.get());
    check(c != nullptr, "3D: registret ger en riktig glaze::Canvas", "");
    if (!c)
        return;
    const auto anrop = [&](const char* metod, const std::string& args) {
        return bld::applyCall(*w, {metod, args}, "preview_gate");
    };
    const auto prop = [&](const char* fält, const char* värde) {
        bld::applyProp(*w, {fält, värde}, "preview_gate");
    };
    const auto lika = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    const auto nära = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
    const auto punkt = [&](const glaze::Point3& p, double x, double y, double z) {
        return nära(p.x, x) && nära(p.y, y) && nära(p.z, z);
    };

    // ---- SETTER-FÄLTEN (applyProp, som readern normaliserar dem till).
    prop("multisample", "8");
    prop("depth", "true");
    prop("orbitTumble", "true");
    const bool satta = c->multisample() == 8 && c->depth() && c->orbitTumble();
    prop("multisample", "3");   // ritytan tar 1, 2, 4 och 8 — loggar och behåller
    check(satta && c->multisample() == 8,
          "3D: setter-fälten multisample/depth/orbitTumble, och 3 prov avvisas "
          "av ritytan", std::to_string(c->multisample()));
    prop("depth", "false");
    prop("orbitTumble", "false");
    check(!c->depth() && !c->orbitTumble(),
          "3D: depth och orbitTumble slås av igen", "");
    // MINNET: 256 MiB sätts; ett negativt tal, ett
    // decimaltal och ett tal bortom size_t avvisas (inget odefinierat).
    prop("memoryBudget", "268435456");
    const bool budget = c->memoryBudget() == 268435456u;
    prop("memoryBudget", "-1");
    prop("memoryBudget", "1.5");
    prop("memoryBudget", "1e20");
    check(budget && c->memoryBudget() == 268435456u,
          "minnet: memoryBudget sätts, och -1/1.5/1e20 avvisas",
          std::to_string(c->memoryBudget()));

    // ---- 3D-KAMERAN — varje form C++ tar.
    check(anrop("setCamera",
                "glaze::Camera3D{.target = {674100, 6580050, 12}, .distance = 80, "
                ".azimuth = 30, .elevation = 25, .roll = 5, .projection = "
                "glaze::Projection::Orthographic, .fov = 50, .orthoHeight = 20, "
                ".nearPlane = 0.5, .farPlane = 900, .up = glaze::UpAxis::Y}"),
          "3D: setCamera(glaze::Camera3D{…}) med alla elva fält utförs", "");
    {
        const glaze::Camera3D k = c->camera3D();
        check(c->is3D() && punkt(k.target, 674100, 6580050, 12) &&
                  k.distance == 80.0 && k.azimuth == 30.0f &&
                  k.elevation == 25.0f && k.roll == 5.0f &&
                  k.projection == glaze::Projection::Orthographic &&
                  k.fov == 50.0f && k.orthoHeight == 20.0 &&
                  k.nearPlane == 0.5 && k.farPlane == 900.0 &&
                  k.up == glaze::UpAxis::Y,
              "3D: kamerans elva fält i deklarationsordning", "");
    }
    check(anrop("setCamera", "glaze::Camera3D{{1, 2, 3}, 40, 10, 20}") &&
              punkt(c->camera3D().target, 1, 2, 3) &&
              c->camera3D().distance == 40.0 && c->camera3D().azimuth == 10.0f &&
              c->camera3D().elevation == 20.0f &&
              c->camera3D().projection == glaze::Camera3D{}.projection &&
              c->camera3D().fov == glaze::Camera3D{}.fov,
          "3D: POSITIONELL Camera3D — de utelämnade fälten tar headerns förval",
          "");
    // FÖRSTAPERSONEN — målet en enhet framför ögat längs kompassriktningen
    // (camera.h): gir 90 är öster (+x); lutning 30 lyfter blicken, med z upp.
    check(anrop("setCamera",
                "glaze::Camera3D::lookFrom({10, 20, 2}, 90, 0)") &&
              punkt(c->camera3D().target, 11, 20, 2) &&
              c->camera3D().distance == 1.0 &&
              c->camera3D().fov == glaze::Camera3D{}.fov,
          "3D: lookFrom(öga, gir, lutning) — målet en enhet österut", "");
    check(anrop("setCamera",
                "glaze::Camera3D::lookFrom({10, 20, 2}, 0, 30)") &&
              punkt(c->camera3D().target, 10, 20 + std::cos(std::numbers::pi / 6.0),
                    2 + std::sin(std::numbers::pi / 6.0)),
          "3D: lookFrom med lutning — blicken lyfts 30° mot norr", "");
    check(anrop("setCamera",
                "glaze::Camera3D::lookFrom({0, 0, 0}, 0, 0, 60, "
                "glaze::UpAxis::Y)") &&
              c->camera3D().fov == 60.0f && c->camera3D().up == glaze::UpAxis::Y &&
              punkt(c->camera3D().target, 0, 0, -1),
          "3D: lookFrom med synfält och UpAxis::Y — norr är −z", "");
    check(anrop("setCamera", "{.target = {5, 6, 7}, .distance = 12}") &&
              c->is3D() && punkt(c->camera3D().target, 5, 6, 7) &&
              c->camera3D().distance == 12.0,
          "3D: BAR DESIGNERAD lista med 3D-kamerans namn väljer Camera3D "
          "(överlagringen väljs på namnen, som i C++)", "");
    check(anrop("setCamera", "{.center = {1, 2}, .zoom = 4}") && !c->is3D() &&
              anrop("setCamera", "glaze::Camera3D{.distance = 30}") && c->is3D(),
          "3D: kartkameran släpper 3D-kameran, och 3D-kameran tar tillbaka", "");
    {
        const glaze::Camera3D före = c->camera3D();
        const char* fel[] = {
            // positionell bar lista — tvetydig mellan Camera och Camera3D
            "{{1, 2, 3}, 40}",
            // tom lista — lika tvetydig
            "{}",
            // designerare i fel ordning — C++20 kräver deklarationsordningen
            "glaze::Camera3D{.distance = 5, .target = {0, 0, 0}}",
            "{.distance = 5, .target = {0, 0, 0}}",
            // namn ur båda typerna — ingen av överlagringarna kan ta listan
            "{.center = {0, 0}, .distance = 5}",
            // fel enumvärde och fel enumtyp
            "glaze::Camera3D{.projection = glaze::Projection::Fisheye}",
            "glaze::Camera3D{.up = gfx::UpAxis::Z}",
            // lookFrom med för få argument och med ett fel i det SISTA
            "glaze::Camera3D::lookFrom({0, 0, 0}, 0)",
            "glaze::Camera3D::lookFrom({0, 0, 0}, 0, 0, 45, glaze::UpAxis::X)",
            // ett fel i sista fältet
            "glaze::Camera3D{.target = {1, 1, 1}, .distance = 9, .up = Z}"};
        int avvisade = 0;
        for (const char* f : fel) {
            const glaze::Camera3D k = c->camera3D();
            avvisade += (!anrop("setCamera", f) && c->is3D() &&
                         punkt(k.target, före.target.x, före.target.y,
                               före.target.z) &&
                         k.distance == före.distance)
                            ? 1
                            : 0;
        }
        check(avvisade == static_cast<int>(sizeof fel / sizeof fel[0]),
              "3D: tio felaktiga kameror avvisas — bar positionell, {}, fel "
              "ordning, blandade namn, fel enum, trasig lookFrom — kameran "
              "orörd", std::to_string(avvisade));
        // En VÄLFORMAD rad med en ogiltig kamera är ritytans egen logg och
        // no-op (avståndet 0), precis som ett index utanför.
        check(anrop("setCamera", "glaze::Camera3D{.distance = 0}") &&
                  c->camera3D().distance == före.distance,
              "3D: en giltig rad med en ogiltig kamera — ritytan vägrar själv",
              "");
    }

    // ---- LJUSET.
    check(anrop("setLighting",
                "glaze::Lighting{.direction = {0, 0, -1}, .ambient = 0.25, "
                ".diffuse = 0.5, .exponent = 1.5, .rimDirection = {1, 0, 0}, "
                ".rim = 0.125, .fog = gfx::Color{200, 210, 230}, .fogStart = 100, "
                ".fogEnd = 400, .fogPower = 2, .twoSided = false}"),
          "3D: setLighting med alla elva fält utförs", "");
    {
        const glaze::Lighting l = c->lighting();
        check(nära(l.direction.z, -1) && nära(l.direction.x, 0) &&
                  l.ambient == 0.25f && l.diffuse == 0.5f && l.exponent == 1.5f &&
                  nära(l.rimDirection.x, 1) && l.rim == 0.125f &&
                  lika(l.fog, gfx::Color{200, 210, 230, 255}) &&
                  l.fogStart == 100.0 && l.fogEnd == 400.0 && l.fogPower == 2.0f &&
                  !l.twoSided,
              "3D: ljusets elva fält", "");
    }
    check(anrop("setLighting", "{{0, 1, -1}, 0.75}") &&
              nära(c->lighting().direction.y, 1) &&
              c->lighting().ambient == 0.75f &&
              c->lighting().diffuse == glaze::Lighting{}.diffuse &&
              c->lighting().twoSided == glaze::Lighting{}.twoSided,
          "3D: positionellt ljus som bar lista — resten tar headerns förval", "");
    check(!anrop("setLighting", "{.ambient = 0.5, .direction = {0, 0, -1}}") &&
              !anrop("setLighting", "{.ambient = 0.5, .twoSided = kanske}") &&
              c->lighting().ambient == 0.75f,
          "3D: ljus i fel ordning och med ett fel i sista fältet — orört", "");

    // ---- LAGREN i rymden.
    check(anrop("addLayer", "\"Stolpar\"") && anrop("addLayer", "\"Kub\"") &&
              c->layerCount() == 3,
          "3D: två lager till", std::to_string(c->layerCount()));
    check(anrop("setLayerPlacement", "1, glaze::LayerPlacement::World") &&
              c->layerPlacement(1) == glaze::LayerPlacement::World &&
              anrop("setLayerPlacement", "2, glaze::LayerPlacement::Terrain") &&
              c->layerPlacement(2) == glaze::LayerPlacement::Terrain &&
              !anrop("setLayerPlacement", "2, gfx::LayerPlacement::Ground") &&
              !anrop("setLayerPlacement", "2, glaze::Placement::Ground") &&
              c->layerPlacement(2) == glaze::LayerPlacement::Terrain,
          "3D: setLayerPlacement — World och Terrain; fel prefix avvisas", "");
    check(anrop("setLayerRenderState",
                "2, glaze::RenderState{.depth = glaze::DepthTest::LessEqual, "
                ".depthWrite = false, .depthBias = -1, .slopeBias = 2, .blend = "
                "gfx::Blend::Additive, .cull = glaze::Cull::Back}"),
          "3D: setLayerRenderState med alla sex fält utförs", "");
    {
        const glaze::RenderState s = c->layerRenderState(2);
        check(s.depth == glaze::DepthTest::LessEqual && !s.depthWrite &&
                  s.depthBias == -1.0f && s.slopeBias == 2.0f &&
                  s.blend == glaze::Blend::Additive && s.cull == glaze::Cull::Back,
              "3D: renderlägets sex fält — enumen med båda prefixen", "");
    }
    check(anrop("setLayerRenderState", "1, {.depthWrite = false}") &&
              c->layerRenderState(1).depth == glaze::RenderState{}.depth &&
              !c->layerRenderState(1).depthWrite,
          "3D: bar designerad — djuptestet står kvar på APPENS förval", "");
    check(!anrop("setLayerRenderState",
                 "2, gfx::RenderState{.depth = gfx::DepthTest::Greater}") &&
              !anrop("setLayerRenderState",
                     "2, {.blend = glaze::Blend::Opaque, .depth = "
                     "glaze::DepthTest::Greater}") &&
              !anrop("setLayerRenderState",
                     "2, {.depth = glaze::DepthTest::Greater, .cull = "
                     "glaze::Cull::Sideways}") &&
              c->layerRenderState(2).depth == glaze::DepthTest::LessEqual,
          "3D: gfx::RenderState (en annan typ), fel ordning och ett fel i sista "
          "fältet — renderläget orört", "");

    // ---- DEL-VYERNA — kollektionsvokabulären.
    check(anrop("addView",
                "glaze::View{.anchor = glaze::TextAnchor::TopRight, .offset = "
                "{-8, 8}, .width = 96, .height = 96, .camera = "
                "glaze::Camera3D{.distance = 6, .projection = "
                "glaze::Projection::Orthographic}, .followRotation = true, "
                ".background = gfx::Color{30, 30, 30, 200}}") &&
              c->viewCount() == 1,
          "3D: addView(glaze::View{…}) med alla sju fält",
          std::to_string(c->viewCount()));
    if (const glaze::View* v = c->view(1)) {
        check(v->anchor == glaze::TextAnchor::TopRight && v->offset.x == -8.0 &&
                  v->offset.y == 8.0 && v->width == 96.0 && v->height == 96.0 &&
                  v->camera.distance == 6.0 &&
                  v->camera.projection == glaze::Projection::Orthographic &&
                  v->followRotation &&
                  lika(v->background, gfx::Color{30, 30, 30, 200}),
              "3D: vyns sju fält, med den nästlade kameran", "");
    }
    check(anrop("addView",
                "{.anchor = gfx::TextAnchor::BottomLeft, .width = 120, .height = "
                "80, .camera = {{1, 2, 3}, 40}}") &&
              c->viewCount() == 2 && c->view(2) &&
              punkt(c->view(2)->camera.target, 1, 2, 3) &&
              c->view(2)->camera.distance == 40.0,
          "3D: bar vy med en bar POSITIONELL kamera — inne i vyn finns ingen "
          "överlagring, så den kompilerar och utförs", "");
    check(anrop("addView",
                "glaze::View{.camera = glaze::Camera3D::lookFrom({0, -10, 2}, 0, "
                "0)}") &&
              c->viewCount() == 3 && c->view(3) &&
              punkt(c->view(3)->camera.target, 0, -9, 2),
          "3D: en vy med förstapersonens kamera", "");
    check(anrop("setView", "2, glaze::View{.width = 64, .height = 48}") &&
              c->view(2) && c->view(2)->width == 64.0 &&
              c->view(2)->anchor == glaze::View{}.anchor &&
              c->view(2)->camera.distance == glaze::Camera3D{}.distance,
          "3D: setView ERSÄTTER vyn helt (ankaret och kameran tar förvalen)",
          "");
    check(anrop("setLayerViews", "2, {0, 2}") &&
              c->layerViews(2) == std::vector<int>{0, 2} &&
              anrop("setLayerViews", "1, {}") && c->layerViews(1).empty(),
          "3D: setLayerViews — {0, 2} och den tomma listan", "");
    check(anrop("removeView", "3") && c->viewCount() == 2,
          "3D: removeView", std::to_string(c->viewCount()));
    check(anrop("setViews",
                "{glaze::View{.width = 96, .height = 96, .followRotation = true}, "
                "{.anchor = glaze::TextAnchor::Right, .offset = {-4, 0}, .width = "
                "64}}") &&
              c->viewCount() == 2 && c->view(1) && c->view(1)->followRotation &&
              c->view(2) && c->view(2)->anchor == glaze::TextAnchor::Right &&
              c->view(2)->width == 64.0,
          "3D: setViews fyller om — typad och bar vy i samma lista", "");
    check(!anrop("setViews",
                 "{glaze::View{.width = 10}, glaze::View{.width = x}}") &&
              !anrop("addView", "glaze::View{.width = 10, .anchor = "
                                "glaze::TextAnchor::Top}") &&
              !anrop("addView", "glaze::View{.width = 10, .background = "
                                "gfx::Color{256}}") &&
              !anrop("setView", "1, glaze::View{.camera = {{1, 2, 3}, x}}") &&
              c->viewCount() == 2 && c->view(1) && c->view(1)->width == 96.0,
          "3D: en lista vars SISTA vy är fel, fel ordning och ett fel i sista "
          "fältet — vyerna orörda", "");

    // ---- ORBITENS SPANN, TICKNINGEN OCH INRAMNINGEN.
    check(anrop("setElevationRange", "-30, 60") && c->minElevation() == -30.0f &&
              c->maxElevation() == 60.0f &&
              anrop("setDistanceRange", "5, 500.5") && c->minDistance() == 5.0 &&
              c->maxDistance() == 500.5,
          "3D: setElevationRange och setDistanceRange", "");
    check(!anrop("setElevationRange", "-30") &&
              !anrop("setDistanceRange", "5, x") && c->minElevation() == -30.0f &&
              c->maxDistance() == 500.5 &&
              anrop("setElevationRange", "-200, 0") && c->minElevation() == -30.0f,
          "3D: spann med för få argument och ett fel i det sista är opaka; ett "
          "spann utanför ±180 vägrar ritytan själv", "");
    check(anrop("setTicking", "true") && c->ticking() &&
              !anrop("setTicking", "kanske") && c->ticking(),
          "3D: setTicking(true) utförs, ett annat värde är opakt", "");
    check(anrop("zoomToBox", "glaze::Box3{{0, 0, 0}, {10, 10, 30}}, glaze::mm(5)") &&
              anrop("zoomToBox", "{{0, 0, 0}, {1, 1, 1}}") &&
              !anrop("zoomToBox", "glaze::Box3{{0, 0, 0}}, 5, 6"),
          "3D: zoomToBox utförs (väntar på ytan, som zoomToRect)", "");
    {
        const glaze::Camera3D före = c->camera3D();
        check(!anrop("orbitBy", "10, 5") && !anrop("panBy", "{10, 0}") &&
                  !anrop("dollyBy", "0.5") &&
                  !anrop("easeTo", "glaze::Camera3D{.distance = 3}, 250") &&
                  !anrop("renderToBitmap", "64, 64, glaze::Camera3D{}") &&
                  c->camera3D().distance == före.distance &&
                  c->camera3D().azimuth == före.azimuth,
              "3D: handlingarna utförs inte — bevaras opakt, som easeTo i 2D", "");
    }

    // ---- FORMERNA I RYMDEN — varje fabrik, mätt på formen ritytan fick.
    const auto senaste = [&](int lager) -> const glaze::Shape* {
        return c->shape(lager, c->shapeCount(lager) - 1);
    };
    const auto slag = [&](int lager) {
        const glaze::Shape* s = senaste(lager);
        return s ? s->kind() : glaze::Shape::Kind::None;
    };
    check(anrop("addShape",
                "1, glaze::Shape::line3({674000, 6580000, 0}, {674000, 6580000, "
                "30}, glaze::Stroke{.paint = gfx::Color{90, 90, 90}, .width = "
                "glaze::world(0.5)})") &&
              slag(1) == glaze::Shape::Kind::Lines3 &&
              senaste(1)->points3().size() == 2 &&
              senaste(1)->points3()[1].z == 30.0 &&
              senaste(1)->stroke().width.unit == glaze::Unit::World,
          "3D: line3 med världsbredd", "");
    check(anrop("addShape",
                "1, glaze::Shape::polyline3({{0, 0, 0}, {10, 0, 5}, {10, 10}}, "
                "{.width = 2, .dash = {4, 2}})") &&
              slag(1) == glaze::Shape::Kind::Lines3 &&
              senaste(1)->points3().size() == 3 &&
              senaste(1)->points3()[2].z == 0.0 &&
              senaste(1)->stroke().dash == std::vector<float>{4.0f, 2.0f},
          "3D: polyline3 — en punkt utan z är z = 0, som C++", "");
    check(anrop("addShape",
                "1, glaze::Shape::polygon3({{0, 0, 1}, {10, 0, 1}, {10, 10, 4}}, "
                "gfx::Color{0, 120, 255, 90}, glaze::Stroke{.width = 2})") &&
              slag(1) == glaze::Shape::Kind::Area3 && senaste(1)->hasStroke() &&
              senaste(1)->mesh().valid() &&
              senaste(1)->mesh().triangleCount() == 1,
          "3D: polygon3 — ytan trianguleras, konturen följer", "");
    check(anrop("addShape",
                "1, glaze::Shape::text3({674000, 6580000, 31}, glaze::tr(2), "
                "glaze::TextStyle{.size = glaze::mm(2.5), .halo = "
                "gfx::Color{255, 255, 255}})") &&
              slag(1) == glaze::Shape::Kind::Text3 &&
              senaste(1)->text().id() == 2 &&
              senaste(1)->points3()[0].z == 31.0 &&
              senaste(1)->textStyle().size.unit == glaze::Unit::Mm,
          "3D: text3 med tr-token och TextStyle", "");
    check(anrop("addShape",
                "1, glaze::Shape::marker3({1, 2, 30}, glaze::MarkerStyle{.shape = "
                "glaze::MarkerShape::Square, .size = 6})") &&
              slag(1) == glaze::Shape::Kind::Markers3 &&
              senaste(1)->markerStyle().shape == glaze::MarkerShape::Square,
          "3D: marker3", "");
    check(anrop("addShape",
                "1, glaze::Shape::points3({{0, 0, 1}, {1, 1, 2}}, "
                "glaze::PointStyle{.size = glaze::px(3), .shape = "
                "glaze::MarkerShape::Circle, .color = gfx::Color{10, 20, 30}, "
                ".ramp = glaze::ColorRamp{{{0, gfx::Color{0, 0, 255}}, {1, "
                "gfx::Color{255, 0, 0}}}, 0, 30}, .fog = false, .program = "
                "\"prickar\", .uniforms = {1, 2}})") &&
              slag(1) == glaze::Shape::Kind::Points3,
          "3D: points3 med alla sju PointStyle-fält", "");
    if (slag(1) == glaze::Shape::Kind::Points3) {
        const glaze::PointStyle& ps = senaste(1)->pointStyle();
        check(senaste(1)->pointCloud().pointCount() == 2 && ps.size.value == 3.0f &&
                  ps.shape == glaze::MarkerShape::Circle &&
                  lika(ps.color, gfx::Color{10, 20, 30, 255}) &&
                  ps.ramp.stops.size() == 2 && ps.ramp.max == 30.0 && !ps.fog &&
                  ps.program == "prickar" &&
                  ps.uniforms == std::vector<float>{1.0f, 2.0f},
              "3D: punkternas stil och rampen", "");
    }
    check(anrop("addShape",
                "1, glaze::Shape::box(glaze::Box3{{0, 0, 0}, {2, 4, 30}}, "
                "glaze::MeshStyle{.color = gfx::Color{180, 180, 190}, .opacity = "
                "0.5, .lit = false, .fog = true, .saturation = 0.25, .texture = "
                "glaze::img(\"toolbar\", \"info\"), .ramp = {{{0, gfx::Color{0, "
                "0, 0}}}, 0, 1}, .range = {3, glaze::kRangeToEnd}, .size = "
                "glaze::mm(4), .program = \"himmel\", .uniforms = {1, 0.5}, "
                ".textures = {glaze::img(\"toolbar\", \"info\"), {}}, "
                ".contrast = 1.5, .brightness = 0.75})") &&
              slag(1) == glaze::Shape::Kind::Mesh,
          "3D: box med alla fjorton MeshStyle-fält", "");
    if (slag(1) == glaze::Shape::Kind::Mesh) {
        const glaze::MeshStyle& ms = senaste(1)->meshStyle();
        const glaze::Placement& pl = senaste(1)->placement();
        check(punkt(pl.at, 1, 2, 15) && pl.scale.x == 2.0f &&
                  pl.scale.y == 4.0f && pl.scale.z == 30.0f,
              "3D: lådan står i sin mitt med hörnens mått som skala", "");
        check(lika(ms.color, gfx::Color{180, 180, 190, 255}) &&
                  ms.opacity == 0.5f && !ms.lit && ms.fog &&
                  ms.saturation == 0.25f &&
                  ms.texture.kind() == glaze::ImageSource::Kind::Asset &&
                  ms.ramp.stops.size() == 1 && ms.range.first == 3 &&
                  ms.range.count == glaze::kRangeToEnd &&
                  ms.size.unit == glaze::Unit::Mm && ms.size.value == 4.0f &&
                  ms.program == "himmel" &&
                  ms.uniforms == std::vector<float>{1.0f, 0.5f} &&
                  ms.textures.size() == 2 &&
                  ms.textures[1].kind() == glaze::ImageSource::Kind::None &&
                  ms.contrast == 1.5f && ms.brightness == 0.75f,
              "3D: meshstilens fjorton fält — programmet är ett NAMN, "
              "färgjusteringen sist",
              "");
    }
    // Kontrasten och ljusheten FÖRE `textures` är fel ordning — opak rad.
    check(!anrop("addShape",
                 "1, glaze::Shape::box({{0, 0, 0}, {1, 1, 1}}, {.contrast = 1.5, "
                 ".textures = {}})"),
          "3D: MeshStyle med kontrasten före texturerna är opak (fältordningen)",
          "");
    check(anrop("addShape", "1, glaze::Shape::sphere({0, 0, 5}, glaze::mm(2), "
                            "{.color = gfx::Color{255, 0, 0}})") &&
              slag(1) == glaze::Shape::Kind::Mesh &&
              senaste(1)->meshStyle().size.unit == glaze::Unit::Mm &&
              senaste(1)->meshStyle().size.value == 4.0f &&
              anrop("addShape", "1, glaze::Shape::sphere(glaze::Point3{0, 0, 5}, "
                                "glaze::world(0.5))") &&
              senaste(1)->placement().scale.x == 1.0f,
          "3D: sfären — radien i mm är skärmens, i världen placeringens", "");
    // Ljuset och dimman med OLIKA värden här och i lådan ovan: en avkodare som
    // blandat ihop de två fälten fälls av den ena eller den andra.
    // Sidantalet 12 ger en ANNAN enhetsmesh än förvalet — en avkodare som
    // tappat det sista argumentet hade gett samma triangelantal två gånger.
    std::size_t tolvSidor = 0;
    check(anrop("addShape", "1, glaze::Shape::cylinder({3, 4, 0}, 0.25, 12, "
                            "{.color = gfx::Color{120, 90, 60}, .lit = true, "
                            ".fog = false}, 12)") &&
              slag(1) == glaze::Shape::Kind::Mesh &&
              punkt(senaste(1)->placement().at, 3, 4, 0) &&
              senaste(1)->placement().scale.x == 0.25f &&
              senaste(1)->placement().scale.z == 12.0f &&
              senaste(1)->meshStyle().lit && !senaste(1)->meshStyle().fog &&
              (tolvSidor = senaste(1)->mesh().triangleCount()) > 0 &&
              anrop("addShape", "1, glaze::Shape::cylinder({0, 0, 0}, 1, 2)") &&
              slag(1) == glaze::Shape::Kind::Mesh &&
              senaste(1)->mesh().triangleCount() != tolvSidor,
          "3D: cylindern med sidantal (12) och med förvalet", "");
    std::size_t sexSidor = 0;
    check(anrop("addShape",
                "1, glaze::Shape::tube({{0, 0, 10}, {25, 0, 8}, {50, 0, 10}}, "
                "0.5, {.lit = false}, 6)") &&
              slag(1) == glaze::Shape::Kind::Mesh && senaste(1)->mesh().valid() &&
              !senaste(1)->meshStyle().lit &&
              (sexSidor = senaste(1)->mesh().triangleCount()) > 0 &&
              anrop("addShape",
                    "1, glaze::Shape::tube({{0, 0, 10}, {25, 0, 8}, {50, 0, "
                    "10}}, 0.5)") &&
              senaste(1)->mesh().triangleCount() != sexSidor,
          "3D: röret längs vägen, med sidantal (6) och med förvalet", "");
    check(anrop("addShape",
                "1, glaze::Shape::mesh(glaze::Mesh::triangles({{0, 0, 0}, {1, 0, "
                "0}, {0, 1, 0}}, gfx::Color{255, 255, 0}), glaze::Placement{.at = "
                "{674000, 6580000, 2}, .yaw = 30, .pitch = 5, .roll = 2, .scale = "
                "{2, 2, 2}}, glaze::MeshStyle{.lit = false, .range = {0, 3}})") &&
              slag(1) == glaze::Shape::Kind::Mesh &&
              senaste(1)->mesh().vertexCount() == 3 &&
              senaste(1)->mesh().triangleCount() == 1 &&
              punkt(senaste(1)->placement().at, 674000, 6580000, 2) &&
              senaste(1)->placement().yaw == 30.0f &&
              senaste(1)->placement().pitch == 5.0f &&
              senaste(1)->placement().roll == 2.0f &&
              senaste(1)->placement().scale.y == 2.0f &&
              senaste(1)->meshStyle().range.count == 3,
          "3D: mesh(Mesh::triangles(…), Placement{…}, MeshStyle{…})", "");
    check(anrop("addShape",
                "1, glaze::Shape::mesh(glaze::Mesh::indexed({{0, 0, 0}, {1, 0, "
                "0}, {1, 1, 0}, {0, 1, 0}}, {0, 1, 2, 0, 2, 3}, gfx::Color{0, "
                "200, 0}))") &&
              senaste(1)->mesh().vertexCount() == 4 &&
              senaste(1)->mesh().triangleCount() == 2,
          "3D: mesh(Mesh::indexed(…)) — två trianglar över fyra lägen", "");
    check(anrop("addShape",
                "1, glaze::Shape::instances(glaze::Shape::cylinder({0, 0, 0}, "
                "0.3, 6), {{.at = {674000, 6580000, 0}, .yaw = 10, .scale = {1, "
                "1, 1.5}, .color = gfx::Color{30, 120, 40}}, {.at = {674005, "
                "6580003, 0}}}, {.color = gfx::Color{255, 255, 255}})") &&
              slag(1) == glaze::Shape::Kind::Instances &&
              senaste(1)->instances().size() == 2 &&
              senaste(1)->instances()[0].yaw == 10.0f &&
              senaste(1)->instances()[0].scale.z == 1.5f &&
              lika(senaste(1)->instances()[0].color, gfx::Color{30, 120, 40, 255}) &&
              punkt(senaste(1)->instances()[1].at, 674005, 6580003, 0) &&
              senaste(1)->instances()[1].scale.x == glaze::Instance{}.scale.x &&
              senaste(1)->mesh().valid(),
          "3D: instances med en cylinder som prototyp", "");
    check(anrop("addShape",
                "1, glaze::Shape::instances(glaze::Shape::box({{0, 0, 0}, {1, 1, "
                "1}}), {glaze::Instance{.at = {1, 2, 3}}})") &&
              slag(1) == glaze::Shape::Kind::Instances &&
              senaste(1)->instances().size() == 1,
          "3D: instances med en låda och en typad instans", "");
    check(anrop("addShape",
                "1, glaze::Shape::instances(glaze::Shape::line3({0, 0, 0}, {1, 1, "
                "1}), {{}})") &&
              slag(1) == glaze::Shape::Kind::None,
          "3D: en prototyp som inte är en mesh KOMPILERAR — fabriken ger en tom "
          "form, i previewn som i appen", "");
    check(anrop("addShape",
                "2, glaze::Shape::screenPass(\"himmel\", glaze::PassStyle{.uniforms "
                "= {0.25, 0.5}, .textures = {}, .fallback = gfx::Color{10, 20, "
                "30}})") &&
              slag(2) == glaze::Shape::Kind::Pass &&
              senaste(2)->passProgram() == "himmel" &&
              lika(senaste(2)->passStyle().fallback, gfx::Color{10, 20, 30, 255}) &&
              senaste(2)->passStyle().uniforms == std::vector<float>{0.25f, 0.5f} &&
              anrop("addShape", "2, glaze::Shape::screenPass(\"himmel\")") &&
              slag(2) == glaze::Shape::Kind::Pass,
          "3D: screenPass med och utan stil — programmet är ett namn", "");

    // ---- DE ATOMISKA AVSLAGEN — ritytan orörd.
    const int  före1 = c->shapeCount(1), före2 = c->shapeCount(2);
    const auto orörd = [&] {
        return c->shapeCount(1) == före1 && c->shapeCount(2) == före2;
    };
    const char* fel[] = {
        // designerare i fel ordning, ett aggregat i taget
        "glaze::Shape::box({{0, 0, 0}, {1, 1, 1}}, {.opacity = 1, .color = "
        "gfx::Color{}})",
        "glaze::Shape::points3({{0, 0, 0}}, {.color = gfx::Color{}, .size = 3})",
        "glaze::Shape::instances(glaze::Shape::box({{0, 0, 0}, {1, 1, 1}}), "
        "{{.yaw = 10, .at = {0, 0, 0}}})",
        "glaze::Shape::mesh(glaze::Mesh::triangles({{0, 0, 0}, {1, 0, 0}, {0, 1, "
        "0}}, gfx::Color{}), {.yaw = 3, .at = {0, 0, 0}})",
        "glaze::Shape::points3({{0, 0, 0}}, {.ramp = {.min = 0, .stops = {}}})",
        "glaze::Shape::box(glaze::Box3{.max = {1, 1, 1}, .min = {0, 0, 0}})",
        "glaze::Shape::screenPass(\"x\", {.fallback = gfx::Color{}, .uniforms = "
        "{}})",
        // en kontur som bar klammerlista till std::optional<Stroke>
        "glaze::Shape::polygon3({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, gfx::Color{}, "
        "{.width = 2})",
        // en 2D-punkt där en punkt i rymden tas
        "glaze::Shape::text3(glaze::Point{1, 2}, \"x\")",
        // ett fel i meshens SISTA argument och ett negativt index
        "glaze::Shape::mesh(glaze::Mesh::triangles({{0, 0, 0}, {1, 0, 0}, {0, 1, "
        "0}}, gfx::Color{300}))",
        "glaze::Shape::mesh(glaze::Mesh::indexed({{0, 0, 0}, {1, 0, 0}, {0, 1, "
        "0}}, {0, -1, 2}, gfx::Color{}))",
        // ett negativt intervall, ett oktalt tal och en okänd enhet
        "glaze::Shape::box({{0, 0, 0}, {1, 1, 1}}, {.range = {-1, 3}})",
        "glaze::Shape::box({{010, 0, 0}, {1, 1, 1}})",
        "glaze::Shape::sphere({0, 0, 0}, glaze::cm(2))",
        // ett sidantal som inte är ett heltal
        "glaze::Shape::cylinder({0, 0, 0}, 1, 2, {}, 16.5)",
        // programmet som något annat än en strängliteral
        "glaze::Shape::screenPass(namn)",
        // ett fel i instansernas PROTOTYP
        "glaze::Shape::instances(glaze::Shape::cylinder({0, 0, 0}, x, 6), {{}})",
        // KODDATA: ett punktmoln ur data har ingen form (points3 är literalen)
        "glaze::Shape::pointCloud(glaze::PointCloud({{0, 0, 0}}, {0, 0, 0}))",
        // fel enumtyp i punkternas form
        "glaze::Shape::points3({{0, 0, 0}}, {.shape = glaze::LineCap::Round})"};
    int orörda = 0;
    for (const char* f : fel)
        orörda += (!anrop("addShape", (std::string("1, ") + f)) && orörd()) ? 1 : 0;
    check(orörda == static_cast<int>(sizeof fel / sizeof fel[0]),
          "3D: nitton felaktiga former i rymden avvisas — OPAKA, ritytan orörd",
          std::to_string(orörda));
    check(!anrop("setShapes",
                 "1, {glaze::Shape::box({{0, 0, 0}, {1, 1, 1}}), "
                 "glaze::Shape::sphere({0, 0, 0}, glaze::mm(2), {.lit = "
                 "kanske})}") &&
              orörd(),
          "3D: en formlista vars SISTA form har ett fel i sista fältet muterar "
          "ingenting", "");

    // ---- HELA KEDJAN: text → reader → modell → satsvägen. `onTick` i den
    // KORTA formen är en koppling (isKnownEvent), och setter-fälten är Prop.
    const std::string h =
        "class Z : public glaze::AppWindow {\n"
        "#pragma region GlazeBuilder\n"
        "public:\n"
        "    Z();\n"
        "private:\n"
        "    glaze::Canvas scen;\n"
        "    void initializeComponent();\n"
        "#pragma endregion\n"
        "};\n";
    const std::string g =
        "#include \"z.h\"\n"
        "Z::Z() { initializeComponent(); }\n"
        "void Z::initializeComponent()\n"
        "{\n"
        "    scen.name = \"scen\";\n"
        "    scen.setMultisample(4);\n"
        "    scen.setDepth(true);\n"
        "    scen.setOrbitTumble(true);\n"
        "    scen.setCamera(glaze::Camera3D{.target = {0, 0, 5}, .distance = 40, "
        ".azimuth = 30, .elevation = 25});\n"
        "    scen.setLighting(glaze::Lighting{.ambient = 0.5});\n"
        "    scen.addLayer(\"Stolpar\");\n"
        "    scen.setLayerPlacement(1, glaze::LayerPlacement::World);\n"
        "    scen.addShape(1, glaze::Shape::box({{-1, -1, 0}, {1, 1, 10}}, "
        "{.color = gfx::Color{180, 180, 190}}));\n"
        "    scen.addView(glaze::View{.width = 64, .height = 64, "
        ".followRotation = true});\n"
        "    scen.setLayerViews(1, {0, 1});\n"
        "    scen.setElevationRange(-10, 80);\n"
        "    scen.setTicking(true);\n"
        "\n"
        "    scen.onTick = [this] { steg(); };\n"
        "    scen.onGesture = [this](glaze::Widget* s, glaze::GestureArgs& e) { "
        "gest(s, e); };\n"
        "\n"
        "    root().adopt(scen);\n"
        "}\n";
    bld::WindowModel m;
    std::string      err;
    const bool       läst = bld::parseHeader(h, "z.h", m, err) &&
                      bld::parseGuiCpp(g, "z.glazeui", m, err);
    check(läst, "3D: fixturen läses", err);
    if (läst) {
        auto k = build("glaze::Canvas", {});
        if (!k)
            return;
        int  props = 0, calls = 0, events = 0;
        bool kortTick = false;
        for (const bld::Stmt& s : m.stmts) {
            if (s.target != "scen")
                continue;
            props += s.kind == bld::Stmt::Kind::Prop ? 1 : 0;
            calls += s.kind == bld::Stmt::Kind::Call ? 1 : 0;
            events += s.kind == bld::Stmt::Kind::Event ? 1 : 0;
            if (s.kind == bld::Stmt::Kind::Event && s.name == "onTick")
                kortTick = s.shortForm;
            bld::applyStmt(*k, s, "preview_gate");
        }
        auto* kc = dynamic_cast<glaze::Canvas*>(k.get());
        check(props == 4 && calls == 9 && events == 2 && kortTick,
              "3D: namnet och de tre setter-fälten är Prop, nio Call-satser, och "
              "BÅDA kopplingarna är events (onTick i den korta formen)",
              std::to_string(props) + " prop, " + std::to_string(calls) +
                  " call, " + std::to_string(events) + " event");
        check(kc && kc->multisample() == 4 && kc->depth() && kc->orbitTumble() &&
                  kc->is3D() && kc->camera3D().distance == 40.0 &&
                  kc->lighting().ambient == 0.5f && kc->layerCount() == 2 &&
                  kc->layerPlacement(1) == glaze::LayerPlacement::World &&
                  kc->shapeCount(1) == 1 && kc->viewCount() == 1 &&
                  kc->layerViews(1) == std::vector<int>{0, 1} &&
                  kc->minElevation() == -10.0f && kc->ticking(),
              "3D: satsvägen — setter-fälten, kameran, ljuset, lagret, lådan, "
              "vyn, spannet och tickningen", "");
    }
}

// --------------------------------------- CALLS-FIXTURENS RITYTA
//
// `tests/fixtures/calls.*` KOMPILERAR (targetet `calls_fixture`), och varje
// ny literal i ritytans 2D och 3D läggs där som
// kompileringsbevis. Regel 8:s andra krav är att previewn UTFÖR dem: här läses
// fixturen med den riktiga grammatiken (`bld::readWindow`) och varje
// `karta`-sats körs mot en riktig rityta i källordning. En Call-sats ska
// UTFÖRAS (`applyCall` svarar ja) — utom HANDLINGARNA, som medvetet bevaras
// opakt och då ska svara nej — och en Prop-sats ska vara ett basfält eller
// ett setter-fält som readern normaliserat. En rad som läggs i
// fixturen utan en gren i apply_props fäller alltså den här grinden och inte
// bara en framtida läsare.
//
// INTE HÄR: vad varje sats gör med ritytan (fallCanvas och fall3D mäter
// tillståndet form för form) och att en VÄLFORMAD rad som ritytan själv
// vägrar (ett lager som inte finns) inte ändrar något — den räknas som
// utförd, som i C++.
void fallCallsRityta()
{
    const std::string sökväg =
        std::string(GLAZE_TEST_ROOT) + "/tests/fixtures/calls.h";
    bld::ReadResult r = bld::readWindow(sökväg);
    check(r.ok, "calls-ritytan: fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    auto w = build("glaze::Canvas", {});
    if (!w)
        return;
    static const char* const kHandlingar[] = {"easeTo", "orbitBy", "panBy",
                                              "dollyBy", "renderToBitmap"};
    static const char* const kBasfält[] = {"name",  "x",      "y",
                                           "width", "height", "right",
                                           "bottom", "anchors"};
    const auto finns = [](const auto& lista, const std::string& s) {
        for (const char* e : lista)
            if (s == e)
                return true;
        return false;
    };
    int         satser = 0, handlingar = 0;
    std::string fel;
    for (const bld::Stmt& s : r.model.stmts) {
        if (s.target != "karta")
            continue;
        if (s.kind == bld::Stmt::Kind::Prop) {
            ++satser;
            if (!finns(kBasfält, s.name) &&
                !bld::isSetterField("glaze::Canvas", s.name))
                fel += " prop:" + s.name;
            bld::applyProp(*w, {s.name, s.value}, "preview_gate");
        } else if (s.kind == bld::Stmt::Kind::Call) {
            ++satser;
            const bool handling = finns(kHandlingar, s.name);
            handlingar += handling ? 1 : 0;
            if (bld::applyCall(*w, {s.name, s.value}, "preview_gate") == handling)
                fel += " " + s.name + "(" + s.value.substr(0, 48) + ")";
        }
    }
    // Golven är fixturens egna rader (110 satser, fyra handlingar) —
    // de finns för att en tom eller oläst lista aldrig ska bli grön, inte
    // för att låsa antalet: en rad till är en rad till.
    check(satser > 100 && handlingar >= 4 && fel.empty(),
          "calls-ritytan: varje karta-sats UTFÖRS (" + std::to_string(satser) +
              " satser) — handlingarna (" + std::to_string(handlingar) +
              ") bevaras opakt",
          fel.empty() ? std::to_string(handlingar) + " handlingar" : fel);
}

// BINDNINGARNA OCH RULLNINGENS RIKTNING — previewn UTFÖR
// formerna: läget (setter-fältet, bara en strängliteral), en rad med sex
// argument och `|` mellan modifierarna (`argMods`), basens `setWheelInvert` på
// en ListBox; en felskriven källa och ett uttryck i modifierarna är OPAKA
// rader som lämnar ritytan orörd. Läses ur ritytans EGNA rader
// (`bindings()`), ur `effectiveBinding` (en läsfråga som aldrig skapar App —
// headless svarar den med förvalen under ritytans rader) och listans egna
// inställning.
// SABOTAGEN: `argMods` borttagen (raden med `|` blir opak), `inputMode`
// genom den allmänna vägen (identifieraren blir ett läge).
void fallBindningar()
{
    auto w = build("glaze::Canvas", {{"inputMode", "\"orbit\""}});
    auto* cv = w ? dynamic_cast<glaze::Canvas*>(w.get()) : nullptr;
    if (!cv)
        return;
    check(cv->inputMode() == "orbit", "bindningar: setInputMode(\"orbit\") sätter läget");
    bld::applyProp(*cv, {"inputMode", "läge"}, "preview_gate");
    check(cv->inputMode() == "orbit",
          "bindningar: setInputMode(identifierare) är opak — läget orört");
    const bool ok1 = bld::applyCall(
        *cv,
        {"setBinding", "\"orbit\", glaze::InputSource::ScrollX, gfx::kModCtrl | "
                       "gfx::kModShift, glaze::InputAction::Rotate, "
                       "glaze::InputDirection::Inverted, 1.5f"},
        "preview_gate");
    const auto rader = cv->bindings();
    check(ok1 && rader.size() == 1 && rader[0].mode == "orbit" &&
              rader[0].source == glaze::InputSource::ScrollX &&
              rader[0].mods == (gfx::kModCtrl | gfx::kModShift) &&
              rader[0].action == glaze::InputAction::Rotate &&
              rader[0].direction == glaze::InputDirection::Inverted &&
              rader[0].speed == 1.5f,
          "bindningar: setBinding med sex argument och | i modifierarna lagras",
          std::to_string(rader.size()) + " rader");
    const auto eff = cv->effectiveBinding("orbit", glaze::InputSource::ScrollX,
                                          gfx::kModCtrl | gfx::kModShift | gfx::kModAlt);
    const auto förval = cv->effectiveBinding("map", glaze::InputSource::WheelY, 0);
    check(eff && eff->action == glaze::InputAction::Rotate && eff->speed == 1.5f &&
              förval && förval->action == glaze::InputAction::Zoom,
          "bindningar: effectiveBinding ser ritytans rad (delmängd) och förvalet under");
    const bool opak1 = !bld::applyCall(
        *cv, {"setBinding", "\"orbit\", glaze::InputSource::Hjul, 0, glaze::InputAction::Zoom"},
        "preview_gate");
    const bool opak2 = !bld::applyCall(
        *cv, {"setBinding", "\"orbit\", glaze::InputSource::WheelY, 1 + 2, glaze::InputAction::Zoom"},
        "preview_gate");
    check(opak1 && opak2 && cv->bindings().size() == 1,
          "bindningar: en felskriven källa och ett uttryck i modifierarna är "
          "opaka — ritytan orörd");
    check(bld::applyCall(*cv, {"clearBindings", ""}, "preview_gate") &&
              cv->bindings().empty(),
          "bindningar: clearBindings() utförs");
    auto lista = build("glaze::ListBox", {});
    if (!lista)
        return;
    const bool ok2 = bld::applyCall(
        *lista, {"setWheelInvert", "glaze::InputDevice::Mouse, true, false"},
        "preview_gate");
    check(ok2 && lista->isWheelInverted(glaze::InputDevice::Mouse, false) &&
              !lista->isWheelInverted(glaze::InputDevice::Mouse, true),
          "bindningar: lista.setWheelInvert(Mouse, true, false) sätter listans "
          "egen riktning");
    check(!bld::applyCall(*lista, {"setWheelInvert", "glaze::InputDevice::Mus, true, false"},
                          "preview_gate"),
          "bindningar: en felskriven enhet är opak");
}

// TRÄDET OCH TABELLEN UR FIXTUREN — `fallCallsRityta`s
// mönster: varje `trad`- och `tabell`-sats i tests/fixtures/calls.* körs mot
// en RIKTIG widget i källordning. Varje anropssats ska UTFÖRAS (`applyCall`
// svarar true), och varje fältrad ska vara ett basfält eller ett setter-fält
// — en ny satsform i fixturen utan en gren här fäller fallet (parseTreeLiteral
// som stannar vid fem fält, ett `std::nullopt` som inte tas emot, en saknad
// `setRowColor`-gren). Fixturen KOMPILERAR (targetet `calls_fixture`), så det
// som mäts är att previewn gör det C++ gör. Sluttillståndet kontrolleras
// också: fixturens sista `setNodes` med kryss och färg, och radfärgerna.
void fallCallsTradTabell()
{
    const std::string sökväg =
        std::string(GLAZE_TEST_ROOT) + "/tests/fixtures/calls.h";
    bld::ReadResult r = bld::readWindow(sökväg);
    check(r.ok, "calls-träd/tabell: fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    static const char* const kBasfält[] = {"name",  "x",      "y",
                                           "width", "height", "right",
                                           "bottom", "anchors"};
    const auto bas = [](const std::string& s) {
        for (const char* e : kBasfält)
            if (s == e)
                return true;
        return false;
    };
    // Bildernas handtag behöver demons assets.bin — utan den blir de
    // ogiltiga handtag, och satserna utförs ändå (som i appen).
    struct Mål {
        const char* namn;
        const char* typ;
        int         golv;   // fixturens egna satser — ingen tom lista
    };
    for (const Mål& m : {Mål{"trad", "glaze::TreeView", 20},
                         Mål{"tabell", "glaze::ListView", 9}}) {
        auto w = build(m.typ, {});
        if (!w)
            return;
        int         satser = 0;
        std::string fel;
        for (const bld::Stmt& s : r.model.stmts) {
            if (s.target != m.namn)
                continue;
            if (s.kind == bld::Stmt::Kind::Prop) {
                ++satser;
                if (!bas(s.name) && !bld::isSetterField(m.typ, s.name))
                    fel += " prop:" + s.name;
                bld::applyProp(*w, {s.name, s.value}, "preview_gate");
            } else if (s.kind == bld::Stmt::Kind::Call) {
                ++satser;
                if (!bld::applyCall(*w, {s.name, s.value}, "preview_gate"))
                    fel += " " + s.name + "(" + s.value.substr(0, 48) + ")";
            }
        }
        check(satser >= m.golv && fel.empty(),
              std::string("calls-träd/tabell: varje ") + m.namn +
                  "-sats UTFÖRS (" + std::to_string(satser) + " satser)",
              fel);
        if (auto* tv = dynamic_cast<glaze::TreeView*>(w.get())) {
            check(tv->isMultiSelect() && tv->showsChecks() &&
                      tv->nodeCount({}) == 3 && tv->isChecked({0, 0}) &&
                      tv->nodeColor({0, 0}) == gfx::Color{200, 40, 40} &&
                      tv->isChecked({1}) &&
                      tv->nodeColor({1}) == gfx::Color{0, 140, 0} &&
                      !tv->isChecked({2}) && !tv->nodeColor({2}) &&
                      !tv->allowDropInto({0, 0}) && tv->allowDropInto({2}) &&
                      tv->selectedPaths().empty(),
                  "calls-träd: sluttillståndet — sju fält positionellt, "
                  "designerat och typat");
        }
        if (auto* lv = dynamic_cast<glaze::ListView*>(w.get())) {
            check(lv->rowCount() == 4 &&
                      lv->rowColor(1) == gfx::Color{200, 0, 0} &&
                      !lv->rowColor(0) &&
                      lv->rowColor(2) == gfx::Color{0, 140, 0} &&
                      !lv->rowColor(3) && lv->cell(3, 0) == "d",
                  "calls-tabell: sluttillståndet — radfärgerna ur setRows och "
                  "setRowColor");
            // KRYSSEN — satta EFTER `setRows`, som
            // nollar dem: rutorna syns och rad 1, bara den, är kryssad.
            check(lv->showsChecks() && lv->isChecked(1) && !lv->isChecked(0) &&
                      lv->checkedRows() == std::vector<int>{1},
                  "calls-tabell: sluttillståndet — setShowChecks och "
                  "setChecked(1, true)");
            // Cellfärgerna (efter `setRows`, som nollar dem),
            // radrubrikerna och bredden efter innehållet.
            check(lv->cellBackground(0, 1) == gfx::Color{255, 224, 224} &&
                      lv->cellTextColor(0, 1) == gfx::Color{160, 0, 0} &&
                      !lv->cellBackground(1, 0) && !lv->cellTextColor(1, 0) &&
                      lv->rowHeaders() &&
                      lv->columnWidth(1) ==
                          glaze::ColumnPanel::Column::autoWidth,
                  "calls-tabell: sluttillståndet — setCellColor, setRowHeaders "
                  "och setColumnWidth(…, autoWidth)");
        }
    }
}

// DOCKNINGEN UR FIXTUREN — verktygsvägen i ett RIKTIGT fönster:
// fixturens fyra dockningsnoder genom `bld::buildWindowTree`, som bygger
// trädet FÖRST och kör satserna därefter (fälten, `dockPanel` i båda formerna
// och `activate()`). Sluttillståndet är en flikgrupp utforskare/sok med en
// stapel egenskaper under, allt till vänster — samma som appen får, som kör
// fälten FÖRE adoptionerna och satserna efter (`.glazeui`s källordning).
// Referensen är den handräknade strängen: utforskares cell delades lika när
// egenskaper staplades under den (0.5 + 0.5), utforskare är aktiv efter
// `activate()`, och egenskaper har fått kanten tillbakaskriven. VERKTYGSFÄLTET
// får sitt band ur `setBand(2)` och kanten ur det råa
// `dock = Left` — satta EFTER trädbygget här, före i appen — och står i band
// 0 vid vänsterkanten (nyckeln packas i strängen, fältets tal står kvar).
//
// VAD DEN INTE MÄTER: att raderna KOMPILERAR (targetet `calls_fixture`) och
// hur dockningen RITAS (dock_gate).
void fallCallsDockning()
{
    const char*     o = "preview_gate";
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "calls-dockning: fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    const std::vector<std::string> mina = {"dock1",      "utforskare",
                                           "sok",        "egenskaper",
                                           "verktygsrad", "flytrad"};
    const auto min = [&](const std::string& id) {
        return std::find(mina.begin(), mina.end(), id) != mina.end();
    };
    bld::WindowModel bara;
    for (const bld::WidgetNode& n : r.model.nodes)
        if (min(n.id))
            bara.nodes.push_back(n);
    for (const bld::Stmt& s : r.model.stmts)
        if (min(s.target))
            bara.stmts.push_back(s);
    check(bara.nodes.size() == 6, "calls-dockning: fixturen bär sex noder",
          std::to_string(bara.nodes.size()));

    glaze::AppWindow                  fönster("dockning", 600, 400);
    const std::vector<glaze::Widget*> perNod =
        bld::buildWindowTree(fönster, bara, o);
    glaze::DockHost*  host = nullptr;
    glaze::DockPanel* utf = nullptr;
    glaze::DockPanel* sok = nullptr;
    glaze::DockPanel* egen = nullptr;
    glaze::ToolBar*   rad  = nullptr;
    glaze::ToolBar*   flyt = nullptr;
    for (std::size_t i = 0; i < bara.nodes.size() && i < perNod.size(); ++i) {
        const std::string& id = bara.nodes[i].id;
        if (id == "dock1")
            host = dynamic_cast<glaze::DockHost*>(perNod[i]);
        else if (id == "utforskare")
            utf = dynamic_cast<glaze::DockPanel*>(perNod[i]);
        else if (id == "sok")
            sok = dynamic_cast<glaze::DockPanel*>(perNod[i]);
        else if (id == "egenskaper")
            egen = dynamic_cast<glaze::DockPanel*>(perNod[i]);
        else if (id == "verktygsrad")
            rad = dynamic_cast<glaze::ToolBar*>(perNod[i]);
        else if (id == "flytrad")
            flyt = dynamic_cast<glaze::ToolBar*>(perNod[i]);
    }
    check(host && utf && sok && egen && utf->parent() == host,
          "calls-dockning: värden och panelerna byggs, panelerna som barn");
    if (!host || !utf || !sok || !egen || !rad || !flyt)
        return;
    // Det flytande fältet står som `ft=` sist — platsen
    // `-` där fönster saknar skärmposition (headless, Wayland).
    const bool pos = glaze::DockHost::isFloatDragSupported();
    check(host->layout() ==
              "v2;s=left;c=0.5;p=utforskare,1,0,1,0,240,0;p=sok,0,0,1,0,0,0;"
              "c=0.5;p=egenskaper,1,1,0,0,0,0;t=verktygsrad,left,0,0,0;"
              "ft=flytrad," +
                  std::string(pos ? "500,80" : "-,-") + ",0,top,0",
          "calls-dockning: dockPanel och activate utfördes — flikgrupp + "
          "stapel till vänster (egenskaper onålad och infälld), "
          "verktygsfältet i band 0 vid vänsterkanten och flytrad flytande",
          host->layout());
    check(flyt->isFloating() && flyt->parent() == host &&
              flyt->textToken().id() == 166 &&
              flyt->floatingPosition().x == 500.0f &&
              flyt->floatingPosition().y == 80.0f && !rad->isFloatable(),
          "calls-dockning: setText(tr), setFloating, setFloatingPosition och "
          "setFloatable(false) når fälten — inget fönster öppnas");
    check(!egen->isPinned() && egen->isCollapsed() &&
              host->frontChild() == nullptr,
          "calls-dockning: setPinned(false) efter setCollapsed(false) — "
          "egenskaper är ett HANDTAG, ingen overlay");
    check(rad->band() == 2 && rad->dock == glaze::Dock::Left &&
              rad->isVertical(),
          "calls-dockning: setBand(2) och det råa dock = Left gäller — fältet "
          "står, och dess eget tal står kvar");
    check(!rad->overflow(),
          "calls-dockning: setOverflow(false) når fältet");
    check(egen->dock == glaze::Dock::Left,
          "calls-dockning: egenskapers kant är tillbakaskriven (Left)");
    check(utf->showsChildren() && !sok->showsChildren(),
          "calls-dockning: utforskare är den visade fliken, sok en inaktiv");
    check(utf->minimumSize().w == 160.0f && utf->maximumSize().w == 400.0f &&
              !sok->isClosable(),
          "calls-dockning: min/max och setClosable gäller i fönstret");
    // Platsen och flytbarheten når panelerna, och ingen av dem
    // flyter — strängen ovan är oförändrad.
    check(utf->floatingBounds().x == 60.0f &&
              utf->floatingBounds().y == 40.0f &&
              utf->floatingBounds().w == 280.0f &&
              utf->floatingBounds().h == 320.0f && !utf->isFloating() &&
              !sok->isFloatable(),
          "calls-dockning: setFloatingBounds och setFloatable(false) gäller i "
          "fönstret");
}

// ================================================ PROGRAMIKONEN
//
// Regel 8 åt PREVIEW-hållet för den TREDJE inbyggda fönstermedlemmen:
// formerna `.glazeui` bär för `appIcon` ska UTFÖRAS — bilderna i båda
// formerna (`glaze::img(…)` och `{}` = förvalet), de två bools och
// programmenyn med den TOMMA stigen `{}` som roten. Fem delar:
//   1. setter-fälten når ikonen (applyProp) — och en oparsbar bild lämnar
//      den ORÖRD i stället för att gissa
//   2. programmenyn byggs på rätt nivå, `{}` är roten och en undermeny
//      adresseras som på menyraden
//   3. ATOMICITETEN: trasig stig, oparsbar text och `addMenu` (som ikonen
//      inte har) ger `false` och en orörd meny; `setHandler` är känd
//   4. `appIcon.hide()` är den generiska anropsgrenen
//   5. VERKTYGSVÄGEN i ett RIKTIGT fönster: fixturens appIcon-satser genom
//      `bld::buildWindowTree` hamnar på fönstrets EGEN `appIcon` (builtin-
//      tabellen i window_tree.cpp) och inte på en ny widget ur registret
// Kör SIST: del 1 och 5 laddar demons assets.bin (en global butik), så
// inget fall före det här får se ett laddat bildregister.
//
// VAD DEN INTE MÄTER: att ikonen RITAS eller sätts på OS-fönstret (det är
// icon_gate G och pixel_gate), och att `.glazeui`-raderna KOMPILERAR (det
// gör targetet `calls_fixture`).
void fallProgramikon()
{
    glaze::setLanguage("sv");
    auto w = build("glaze::AppIcon", {});
    if (!w)
        return;
    auto* ai = static_cast<glaze::AppIcon*>(w.get());
    const char* o = "preview_gate";

    // ---- 1. setter-fälten.
    bld::applyProp(*w, {"large", "true"}, o);
    check(ai->isLarge(), "programikon: setLarge(true) når ikonen");
    bld::applyProp(*w, {"noIcon", "true"}, o);
    check(ai->isNoIcon(), "programikon: setNoIcon(true) når ikonen");
    bld::applyProp(*w, {"noIcon", "false"}, o);
    check(!ai->isNoIcon(), "programikon: setNoIcon(false) tar tillbaka den");

    const bool laddad = glaze::loadAssets(std::string(GLAZE_TEST_ROOT) +
                                          "/apps/demo/assets.bin");
    check(laddad, "programikon: demons assets.bin laddas");
    const glaze::AssetHandle kugge = glaze::img("toolbar", "gear");
    check(kugge.valid(), "programikon: toolbar/gear finns i demons assets");
    bld::applyProp(*w, {"icon", "glaze::img(\"toolbar\", \"gear\")"}, o);
    check(ai->icon() == kugge, "programikon: setIcon(glaze::img(…)) når ikonen",
          std::to_string(ai->icon().asset().id));
    bld::applyProp(*w, {"smallIcon", "glaze::img(\"toolbar\", \"gear\")"}, o);
    check(ai->smallIcon() == kugge,
          "programikon: setSmallIcon(glaze::img(…)) når ikonen");
    bld::applyProp(*w, {"icon", "minBild"}, o);
    check(ai->icon() == kugge,
          "programikon: en oparsbar bild lämnar ikonen ORÖRD");
    bld::applyProp(*w, {"icon", "{}"}, o);
    check(!ai->icon().valid(), "programikon: setIcon({}) = förvalet");
    bld::applyProp(*w, {"smallIcon", "{}"}, o);
    check(!ai->smallIcon().valid(), "programikon: setSmallIcon({}) = förvalet");

    // ---- 2. programmenyn.
    check(bld::applyCall(*w, {"addItem", "{}, \"Nytt\", \"Ctrl+N\""}, o),
          "programikon: addItem({}, …) utförs");
    check(bld::applyCall(*w, {"addSubmenu", "{}, \"Exportera\""}, o),
          "programikon: addSubmenu({}, …) utförs");
    check(bld::applyCall(*w, {"addItem", "\"Exportera\", glaze::tr(2)"}, o),
          "programikon: en post i undermenyn utförs");
    check(bld::applyCall(*w, {"addCheck", "{ }, \"Visa\", true"}, o),
          "programikon: addCheck med blanksteg i rotstigen utförs");
    check(bld::applyCall(*w, {"addSeparator", "{}"}, o),
          "programikon: addSeparator({}) utförs");
    const glaze::Menu& m = ai->menu();
    check(m.items.size() == 4 && m.items[1].submenu.size() == 1 &&
              m.items[2].checkable && m.items[2].checked &&
              m.items[3].separator,
          "programikon: posterna hamnade på rotnivån i källordning",
          std::to_string(m.items.size()));
    check(ai->itemAtPath({"Exportera", glaze::tr(2)}) != nullptr,
          "programikon: undermenyns tr-post matchas på ID:t");

    // ---- 3. atomiciteten.
    const std::size_t före = m.items.size();
    check(!bld::applyCall(*w, {"addItem", "{}, namn"}, o),
          "programikon: oparsbar TEXT ⇒ false");
    check(!bld::applyCall(*w, {"addItem", "{namn}, \"X\""}, o),
          "programikon: trasig stig ⇒ false");
    check(!bld::applyCall(*w, {"addMenu", "\"Arkiv\""}, o),
          "programikon: addMenu finns inte på ikonen ⇒ opak rad");
    check(!bld::applyCall(*w, {"addSeparator", "{}, {}"}, o),
          "programikon: fel argumentantal ⇒ false");
    check(bld::applyCall(*w, {"setHandler", "{}, [this] { nytt(); }"}, o),
          "programikon: setHandler är KÄND (utförs inte)");
    check(m.items.size() == före, "programikon: …och menyn är ORÖRD",
          std::to_string(m.items.size()));

    // ---- 4. dölja.
    check(bld::applyCall(*w, {"hide", ""}, o) && !ai->isVisible(),
          "programikon: appIcon.hide() döljer ikonen");

    // ---- 5. verktygsvägen i ett riktigt fönster.
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "programikon: calls-fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    bld::WindowModel bara;
    for (const bld::WidgetNode& n : r.model.nodes)
        if (n.id == "appIcon")
            bara.nodes.push_back(n);
    for (const bld::Stmt& s : r.model.stmts)
        if (s.target == "appIcon")
            bara.stmts.push_back(s);
    check(bara.nodes.size() == 1 && bara.stmts.size() == 12,
          "programikon: fixturen bär appIcon med tolv satser",
          std::to_string(bara.stmts.size()));

    glaze::AppWindow fönster("programikon", 400, 300);
    const std::vector<glaze::Widget*> perNod =
        bld::buildWindowTree(fönster, bara, o);
    check(perNod.size() == 1 && perNod[0] == &fönster.appIcon,
          "programikon: noden ÄR fönstrets egen appIcon (ingen ny widget)");
    check(fönster.appIcon.isLarge() && !fönster.appIcon.isNoIcon() &&
              fönster.appIcon.icon() == kugge &&
              !fönster.appIcon.smallIcon().valid(),
          "programikon: fixturens setter-fält gäller i fönstret");
    check(fönster.appIcon.menu().items.size() == 4 &&
              fönster.appIcon.itemAtPath({"Exportera", "PDF"}) != nullptr,
          "programikon: fixturens programmeny byggs i fönstret",
          std::to_string(fönster.appIcon.menu().items.size()));
    // MENYPOSTENS TILLSTÅND: fixturens tre satser syns i
    // menyns data efter trädbygget.
    const glaze::MenuItem* exp  = fönster.appIcon.itemAtPath("Exportera");
    const glaze::MenuItem* pdf  = fönster.appIcon.itemAtPath({"Exportera", "PDF"});
    const glaze::MenuItem* visa = fönster.appIcon.itemAtPath("Visa");
    check(exp && pdf && visa && exp->icon == glaze::ImageSource(kugge) &&
              !pdf->enabled && !visa->checked && visa->checkable,
          "programikon: setItemIcon/setItemEnabled/setItemChecked ur fixturen "
          "syns i posterna");
    check(!bld::applyCall(*w, {"setItemEnabled", "{}, kanske"}, o) &&
              !bld::applyCall(*w, {"setItemIcon", "\"Visa\""}, o) &&
              bld::applyCall(*w, {"setItemChecked", "{}, true"}, o),
          "programikon: trasiga posttillstånd ⇒ opaka; {}-roten avkodas (och "
          "är ikonens eget no-op med logg, som i appen)");
}

// MENYPOSTENS TILLSTÅND PÅ SYSTEMFÄLTSIKONEN:
// samma tre satser som menyraden och programikonen, utförda genom
// KOMPONENTVÄGEN (`applyComponentStmt`) med `{}`-roten — och de trasiga
// formerna opaka, utan att menyn rörs. Efter `fallProgramikon`, som laddar
// demons assets (bilden slås upp med `glaze::img`).
void fallSystemfaltetsPoster()
{
    const char*     o = "preview_gate";
    glaze::TrayIcon t;
    t.name    = "ikon";
    auto sats = [&](const char* metod, const char* arg) {
        bld::Stmt s;
        s.kind   = bld::Stmt::Kind::Call;
        s.target = "ikon";
        s.name   = metod;
        s.value  = arg;
        return bld::applyComponentStmt(t, s, o);
    };
    check(sats("addItem", "{}, \"Visa\"") && sats("addSubmenu", "{}, \"Läge\"") &&
              sats("addRadio", "\"Läge\", \"Tyst\", true"),
          "systemfältet: menyn byggs med {}-roten");
    const glaze::AssetHandle kugge = glaze::img("toolbar", "gear");
    check(sats("setItemIcon", "\"Visa\", glaze::img(\"toolbar\", \"gear\")") &&
              sats("setItemEnabled", "{\"Visa\"}, false") &&
              sats("setItemChecked", "{\"Läge\", \"Tyst\"}, false"),
          "systemfältet: setItemIcon/setItemEnabled/setItemChecked är KÄNDA");
    const glaze::MenuItem* visa = t.itemAtPath("Visa");
    const glaze::MenuItem* tyst = t.itemAtPath({"Läge", "Tyst"});
    check(visa && tyst && kugge.valid() &&
              visa->icon == glaze::ImageSource(kugge) && !visa->enabled &&
              !tyst->checked,
          "systemfältet: …och syns i menyns poster");
    check(!sats("setItemEnabled", "{\"Visa\"}, kanske") &&
              !sats("setItemIcon", "\"Visa\"") &&
              !sats("setItemChecked", "{\"Läge\"}") && visa && !visa->enabled &&
              visa->icon == glaze::ImageSource(kugge),
          "systemfältet: trasiga posttillstånd ⇒ opaka, posterna ORÖRDA");
}

// VERKTYGSKNAPPENS RULLGARDIN — regel 8 åt
// PREVIEW-hållet: formerna `.glazeui` bär för knappmenyn ska UTFÖRAS, så att
// previewn ritar knappen med pil och poster. Tre delar:
//   1. setter-fältet `split` och menyn med `{}`-roten — alla fem
//      add-satserna, en undermeny med ett tr-segment — och postens tre
//      tillstånd, på en knapp ur registret;
//   2. ATOMICITETEN: ett genvägsargument (knappmenyn har inget — raden hade
//      inte kompilerat), oparsbar text, `addMenu` och trasiga posttillstånd
//      ger `false` och en ORÖRD meny; `setHandler` är KÄND men utförs inte;
//   3. FIXTUREN: varje `nyKnapp`-sats i tests/fixtures/calls.* UTFÖRS
//      (`fallCallsTradTabell`s mönster), och sluttillståndet stämmer.
// Efter `fallProgramikon`: bilderna slås upp i demons assets.bin.
// VAD DEN INTE MÄTER: att menyn ÖPPNAS — previewn öppnar den aldrig, och ett
// tryck kräver ett fönster (toolbar_gate) — och att pilen RITAS.
void fallKnappmeny()
{
    const char* o = "preview_gate";
    glaze::setLanguage("sv");
    auto w = build("glaze::ToolButton", {{"split", "true"}});
    if (!w)
        return;
    auto* tb = dynamic_cast<glaze::ToolButton*>(w.get());
    check(tb != nullptr && tb->isSplit(),
          "knappmeny: setSplit(true) — setter-fältet split — når knappen");
    if (!tb)
        return;

    // ---- 1. menyn och postens tillstånd.
    check(bld::applyCall(*w, {"addItem", "{}, \"Dokument\""}, o) &&
              bld::applyCall(*w, {"addSubmenu", "{}, \"Mall\""}, o) &&
              bld::applyCall(*w, {"addItem", "\"Mall\", glaze::tr(2)"}, o) &&
              bld::applyCall(*w, {"addCheck", "{ }, \"Visa\", true"}, o) &&
              bld::applyCall(*w, {"addRadio", "{}, \"Liten\""}, o) &&
              bld::applyCall(*w, {"addSeparator", "{}"}, o),
          "knappmeny: de fem add-satserna med {}-roten utförs");
    const glaze::Menu& m = tb->menu();
    check(m.items.size() == 5 && m.items[1].submenu.size() == 1 &&
              m.items[2].checkable && m.items[2].checked && m.items[3].radio &&
              m.items[4].separator,
          "knappmeny: posterna hamnade på rotnivån i källordning",
          std::to_string(m.items.size()));
    check(tb->itemAtPath({"Mall", glaze::tr(2)}) != nullptr,
          "knappmeny: undermenyns tr-post matchas på ID:t");

    const glaze::AssetHandle kugge = glaze::img("toolbar", "gear");
    check(bld::applyCall(*w,
                         {"setItemIcon",
                          "\"Dokument\", glaze::img(\"toolbar\", \"gear\")"},
                         o) &&
              bld::applyCall(
                  *w, {"setItemEnabled", "{\"Mall\", glaze::tr(2)}, false"}, o) &&
              bld::applyCall(*w, {"setItemChecked", "\"Visa\", false"}, o),
          "knappmeny: setItemIcon/setItemEnabled/setItemChecked utförs");
    const glaze::MenuItem* dok  = tb->itemAtPath("Dokument");
    const glaze::MenuItem* mall = tb->itemAtPath({"Mall", glaze::tr(2)});
    const glaze::MenuItem* visa = tb->itemAtPath("Visa");
    check(kugge.valid() && dok && mall && visa &&
              dok->icon == glaze::ImageSource(kugge) && !mall->enabled &&
              visa->checkable && !visa->checked,
          "knappmeny: …och syns i posterna");

    // ---- 2. atomiciteten.
    const std::size_t före = m.items.size();
    check(!bld::applyCall(*w, {"addItem", "{}, \"Ny\", \"Ctrl+N\""}, o),
          "knappmeny: ett genvägsargument ⇒ opak rad (knappmenyn har inget)");
    check(!bld::applyCall(*w, {"addItem", "{}, namn"}, o),
          "knappmeny: oparsbar TEXT ⇒ false");
    check(!bld::applyCall(*w, {"addMenu", "\"Arkiv\""}, o),
          "knappmeny: addMenu finns inte på knappen ⇒ opak rad");
    check(!bld::applyCall(*w, {"setItemEnabled", "\"Visa\", kanske"}, o) &&
              !bld::applyCall(*w, {"setItemIcon", "\"Dokument\""}, o),
          "knappmeny: trasiga posttillstånd ⇒ opaka");
    check(bld::applyCall(*w, {"setHandler", "\"Dokument\", [this] { nytt(); }"},
                         o),
          "knappmeny: setHandler är KÄND (utförs inte)");
    check(m.items.size() == före && dok && visa && visa->checkable &&
              !visa->checked && dok->icon == glaze::ImageSource(kugge),
          "knappmeny: …och menyn är ORÖRD", std::to_string(m.items.size()));
    check(!tb->isMenuOpen(), "knappmeny: previewn öppnar aldrig menyn");

    // ---- 3. fixturen.
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "knappmeny: calls-fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    auto f = build("glaze::ToolButton", {});
    if (!f)
        return;
    static const char* const kBasfält[] = {"name",  "x",      "y",
                                           "width", "height", "right",
                                           "bottom", "anchors"};
    int         satser = 0;
    std::string fel;
    for (const bld::Stmt& s : r.model.stmts) {
        if (s.target != "nyKnapp")
            continue;
        if (s.kind == bld::Stmt::Kind::Prop) {
            ++satser;
            const bool bas = std::any_of(
                std::begin(kBasfält), std::end(kBasfält),
                [&s](const char* e) { return s.name == e; });
            if (!bas && !bld::isSetterField("glaze::ToolButton", s.name))
                fel += " prop:" + s.name;
            bld::applyProp(*f, {s.name, s.value}, o);
        } else if (s.kind == bld::Stmt::Kind::Call) {
            ++satser;
            if (!bld::applyCall(*f, {s.name, s.value}, o))
                fel += " " + s.name + "(" + s.value.substr(0, 48) + ")";
        }
    }
    // Golvet är fixturens egna satser (fem fält, nio anrop) — det finns
    // för att en tom eller oläst lista aldrig ska bli grön.
    check(satser >= 14 && fel.empty(),
          "knappmeny: varje nyKnapp-sats i fixturen UTFÖRS (" +
              std::to_string(satser) + " satser)",
          fel);
    auto*                    fk   = static_cast<glaze::ToolButton*>(f.get());
    const glaze::AssetHandle info = glaze::img("toolbar", "info");
    const glaze::MenuItem*   fDok = fk->itemAtPath("Dokument");
    const glaze::MenuItem*   fMall = fk->itemAtPath({"Mall", glaze::tr(3)});
    const glaze::MenuItem*   fVisa  = fk->itemAtPath("Visa");
    const glaze::MenuItem*   fLiten = fk->itemAtPath("Liten");
    check(fk->isSplit() && fk->text() == "Ny" &&
              fk->menu().items.size() == 5 && fDok && fMall && fVisa &&
              fLiten && fDok->icon == glaze::ImageSource(info) &&
              !fMall->enabled && fVisa->checkable && !fVisa->checked &&
              fLiten->radio && fLiten->checked,
          "knappmeny: fixturens sluttillstånd — delad, fem poster, postens "
          "tre tillstånd");
}

// ------------------------------------------------- NYA WIDGETFORMER
//
// De nya setter-fälten och Call-satserna genom verktygsvägen, en widget i
// taget: varje form UTFÖRS (sluttillståndet mäts mot widgetens egna
// läsvägar), och varje fel form är OPAK (`applyCall` svarar false och
// widgeten är orörd). SABOTAGEN: en `kSetterFields`-rad borttagen (matrisen),
// en gren borttagen ur `applyCall` (fallet svarar false på en giltig rad),
// `parseStatusPanelsLiteral` som godtar fältordningen fel
// (`{.fill = true, .text = "A"}` utförd), `Checkbox::setProp` utan
// `checkState` (kryssrutans läge stannar Unchecked).
void fallFas1B()
{
    const char* o = "preview_gate";

    // KRYSSRUTANS TRE LÄGEN — två setter-fält; enumets prefix skalas av
    // verktygsvägen (`parseEnumTail`), ett okänt namn är en opak rad.
    {
        auto w = build("glaze::Checkbox",
                       {{"threeState", "true"},
                        {"checkState", "glaze::CheckState::Indeterminate"}});
        auto* cb = w ? dynamic_cast<glaze::Checkbox*>(w.get()) : nullptr;
        check(cb && cb->isThreeState() &&
                  cb->checkState() == glaze::CheckState::Indeterminate &&
                  cb->isChecked(),
              "fas1b: kryssrutans threeState och checkState(Indeterminate) "
              "utförs — isChecked() är sann i mellanläget");
        if (cb) {
            bld::applyProp(*w, {"checkState", "glaze::CheckState::Bogus"}, o);
            check(cb->checkState() == glaze::CheckState::Indeterminate,
                  "fas1b: ett okänt CheckState-namn lämnar läget orört");
            bld::applyProp(*w, {"checked", "false"}, o);
            check(cb->checkState() == glaze::CheckState::Unchecked,
                  "fas1b: setChecked(false) ur mellanläget ger Unchecked");
        }
    }

    // LISTBOXENS FLERVAL — setter-fältet och de tre anropen.
    {
        auto w = build("glaze::ListBox",
                       {{"items", "{\"a\", \"b\", \"c\", \"d\"}"},
                        {"multiSelect", "true"}});
        auto* lb = w ? dynamic_cast<glaze::ListBox*>(w.get()) : nullptr;
        check(lb && lb->isMultiSelect(), "fas1b: listboxens multiSelect utförs");
        if (lb) {
            const bool a = bld::applyCall(*w, {"selectAll", ""}, o);
            check(a && lb->selectedRows() == std::vector<int>{0, 1, 2, 3},
                  "fas1b: listboxens selectAll() utförs");
            const bool b = bld::applyCall(*w, {"clearSelection", ""}, o) &&
                           bld::applyCall(*w, {"setSelected", "2, true"}, o) &&
                           bld::applyCall(*w, {"setSelected", "0, true"}, o);
            check(b && lb->selectedRows() == std::vector<int>{0, 2},
                  "fas1b: clearSelection() + setSelected(rad, på) utförs",
                  std::to_string(lb->selectedRows().size()));
            check(!bld::applyCall(*w, {"setSelected", "1"}, o) &&
                      !bld::applyCall(*w, {"setSelected", "x, true"}, o) &&
                      !bld::applyCall(*w, {"selectAll", "1"}, o) &&
                      lb->selectedRows() == std::vector<int>{0, 2},
                  "fas1b: setSelected med fel argument och selectAll(1) är "
                  "OPAKA och lämnar valet orört");
        }
    }

    // FLIKARNAS SIDOR — addPage med och utan text, setPage, setPages.
    {
        auto w = build("glaze::TabControl", {});
        auto* tc = w ? dynamic_cast<glaze::TabControl*>(w.get()) : nullptr;
        if (tc) {
            const bool ok =
                bld::applyCall(*w, {"addPage", ""}, o) &&
                bld::applyCall(*w, {"addPage", "\"Två\""}, o) &&
                bld::applyCall(*w, {"addPage", "glaze::tr(3)"}, o) &&
                bld::applyCall(*w, {"setPage", "0, \"Ett\""}, o);
            check(ok && tc->pageCount() == 3 && tc->page(0)->text() == "Ett" &&
                      tc->page(1)->text() == "Två" &&
                      tc->page(2)->textToken() == glaze::tr(3),
                  "fas1b: addPage() / addPage(text) / addPage(tr) / setPage "
                  "utförs", std::to_string(tc->pageCount()));
            const bool sp = bld::applyCall(*w, {"setPages", "{\"A\", \"B\"}"}, o);
            check(sp && tc->pageCount() == 2 && tc->page(0)->text() == "A" &&
                      tc->page(1)->text() == "B",
                  "fas1b: setPages({…}) byter alla sidor",
                  std::to_string(tc->pageCount()));
            check(!bld::applyCall(*w, {"setPage", "\"A\""}, o) &&
                      !bld::applyCall(*w, {"setPages", "\"A\""}, o) &&
                      !bld::applyCall(*w, {"addPage", "1, 2"}, o) &&
                      tc->pageCount() == 2,
                  "fas1b: flikarnas fel former är OPAKA och lämnar sidorna");
        } else {
            check(false, "fas1b: registret ger en TabControl");
        }
    }

    // DATAGRIDDENS RADVAL — läget (ett enum med typens prefix), flervalet
    // och de tre anropen.
    {
        auto w = build("glaze::DataGrid",
                       {{"selectionMode",
                         "glaze::DataGrid::SelectionMode::FullRow"},
                        {"multiSelect", "true"}});
        auto* dg = w ? dynamic_cast<glaze::DataGrid*>(w.get()) : nullptr;
        if (dg) {
            bld::applyCall(*w, {"setRows", "{{{\"a\"}}, {{\"b\"}}, {{\"c\"}}}"},
                           o);
            const bool ok = bld::applyCall(*w, {"selectAll", ""}, o) &&
                            bld::applyCall(*w, {"setSelected", "1, false"}, o);
            check(ok &&
                      dg->selectionMode() ==
                          glaze::DataGrid::SelectionMode::FullRow &&
                      dg->isMultiSelect() &&
                      dg->selectedRows() == std::vector<int>{0, 2},
                  "fas1b: datagriddens selectionMode/multiSelect och "
                  "selectAll/setSelected utförs");
            bld::applyProp(*w, {"selectionMode",
                                "glaze::DataGrid::SelectionMode::Cell"}, o);
            check(dg->selectedRows().empty() && dg->isSelected(0) == false,
                  "fas1b: i Cell-läget är inga rader valda (läsningen)");
            check(!bld::applyCall(*w, {"setSelected", "0"}, o),
                  "fas1b: datagriddens setSelected(0) är OPAK");
        } else {
            check(false, "fas1b: registret ger en DataGrid");
        }
    }

    // FÄRGVÄLJARENS ALFA — åtta siffror och flaggan, i BÅDA ordningarna.
    {
        auto w1 = build("glaze::ColorPicker", {{"color", "\"#11223344\""},
                                               {"allowAlpha", "true"}});
        auto w2 = build("glaze::ColorPicker", {{"allowAlpha", "true"},
                                               {"color", "\"#11223344\""}});
        auto* a = w1 ? dynamic_cast<glaze::ColorPicker*>(w1.get()) : nullptr;
        auto* b = w2 ? dynamic_cast<glaze::ColorPicker*>(w2.get()) : nullptr;
        const gfx::Color väntad{0x11, 0x22, 0x33, 0x44};
        check(a && b && a->allowAlpha() && a->color() == väntad &&
                  b->color() == väntad && a->colorText() == "#11223344",
              "fas1b: färgväljarens #RRGGBBAA + allowAlpha ger samma färg i "
              "båda satsordningarna", a ? a->colorText() : "");
        if (a) {
            bld::applyProp(*w1, {"allowAlpha", "false"}, o);
            check(a->color() == gfx::Color{0x11, 0x22, 0x33, 0xFF} &&
                      a->colorText() == "#112233",
                  "fas1b: utan allowAlpha läses färgen opak", a->colorText());
        }
    }

    // DATUMVÄLJARENS KLOCKSLAG — setter-fältet och `setTime(h, m)`.
    {
        auto w = build("glaze::DatePicker", {{"showTime", "true"}});
        auto* dp = w ? dynamic_cast<glaze::DatePicker*>(w.get()) : nullptr;
        if (dp) {
            const bool ok = bld::applyCall(*w, {"setDate", "2026, 10, 13"}, o) &&
                            bld::applyCall(*w, {"setTime", "9, 5"}, o);
            check(ok && dp->showsTime() &&
                      dp->time() == std::chrono::minutes{9 * 60 + 5} &&
                      dp->text() == "2026-10-13 09:05",
                  "fas1b: datumväljarens showTime och setTime(9, 5) utförs",
                  dp->text());
            check(!bld::applyCall(*w, {"setTime", "9"}, o) &&
                      !bld::applyCall(*w, {"setTime", "\"09:05\""}, o) &&
                      dp->time() == std::chrono::minutes{9 * 60 + 5},
                  "fas1b: setTime med fel argument är OPAK");
            bld::applyCall(*w, {"setTime", "25, 0"}, o);
            check(dp->time() == std::chrono::minutes{9 * 60 + 5},
                  "fas1b: setTime(25, 0) är widgetens logg + no-op");
        } else {
            check(false, "fas1b: registret ger en DatePicker");
        }
    }

    // STATUSRADENS FÄLT — alla fyra anropen och massformens tre former.
    {
        auto w = build("glaze::StatusBar", {});
        auto* sb = w ? dynamic_cast<glaze::StatusBar*>(w.get()) : nullptr;
        if (sb) {
            const bool ok =
                bld::applyCall(*w, {"addPanel", ""}, o) &&
                bld::applyCall(*w, {"addPanel", "\"Rad\", 80"}, o) &&
                bld::applyCall(*w, {"addPanel", "glaze::tr(3), 0, true"}, o) &&
                bld::applyCall(*w, {"setPanel", "0, \"Klar\", 40"}, o) &&
                bld::applyCall(*w, {"setPanelText", "1, \"Rad 2\""}, o);
            check(ok && sb->panelCount() == 3 && sb->panelText(0) == "Klar" &&
                      sb->panel(0).width == 40.0f && !sb->panel(0).fill &&
                      sb->panelText(1) == "Rad 2" && sb->panel(1).width == 80.0f &&
                      sb->panel(2).fill &&
                      sb->panel(2).text == glaze::tr(3),
                  "fas1b: statusradens addPanel/setPanel/setPanelText utförs",
                  std::to_string(sb->panelCount()));
            const bool m = bld::applyCall(
                *w,
                {"setPanels",
                 "{{\"A\", 0, true}, glaze::StatusPanel{\"B\", 50}, "
                 "{.text = \"C\", .fill = true}}"},
                o);
            check(m && sb->panelCount() == 3 && sb->panelText(0) == "A" &&
                      sb->panel(0).fill && sb->panel(1).width == 50.0f &&
                      sb->panelText(2) == "C" && sb->panel(2).fill,
                  "fas1b: setPanels positionellt, typat och designerat",
                  std::to_string(sb->panelCount()));
            check(!bld::applyCall(*w, {"setPanels",
                                       "{{.fill = true, .text = \"X\"}}"}, o) &&
                      !bld::applyCall(*w, {"setPanels", "{{}}"}, o) &&
                      !bld::applyCall(*w, {"setPanel", "\"A\""}, o) &&
                      !bld::applyCall(*w, {"setPanelText", "0"}, o) &&
                      sb->panelCount() == 3 && sb->panelText(0) == "A",
                  "fas1b: statusradens fel former är OPAKA (fel fältordning, "
                  "ett tomt aggregat, saknat index)");
        } else {
            check(false, "fas1b: registret ger en StatusBar");
        }
    }
}

// FIXTURENS FAS1_B-SATSER genom `bld::buildWindowTree` — trädet byggs före
// satserna, precis som i verktygen, och sluttillståndet ska vara appens:
// flikarnas `addPage` står EFTER trädbygget i fixturen, så medlemssidan står
// först i båda vägarna. SABOTAGEN: en `addPage`-rad flyttad FÖRE trädbygget i
// fixturen (calls-fas1b: sidordningen — appen och previewn skiljer sig, och
// det här fallet ser previewns), `TabControl::setPages` som inte behåller det
// lagrade valet (flikar2 väljer 0), `ColorPicker::color()` som läser alfan
// utan flaggan (alfa: 0x80 utan allowAlpha).
void fallCallsFas1B()
{
    const char*     o = "preview_gate";
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "calls-fas1b: fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    const std::vector<std::string> mina = {"kryss",  "flerval", "flikar",
                                           "sida1",  "flikar2", "rutnat",
                                           "alfa",   "tid",     "statusBar"};
    const auto min = [&](const std::string& id) {
        return std::find(mina.begin(), mina.end(), id) != mina.end();
    };
    bld::WindowModel bara;
    for (const bld::WidgetNode& n : r.model.nodes)
        if (min(n.id))
            bara.nodes.push_back(n);
    // Satserna OCH adoptionerna (en adoption bär barnet i `target`) — så
    // `flikar.adopt(sida1)` följer med, i källordning.
    std::string opaka;
    for (const bld::Stmt& s : r.model.stmts)
        if (min(s.target))
            bara.stmts.push_back(s);
    check(bara.nodes.size() == mina.size(),
          "calls-fas1b: fixturen bär nio noder (statusBar inräknad)",
          std::to_string(bara.nodes.size()));
    // Varje sats är en KÄND form — en opak hade ritats annorlunda i appen.
    for (const bld::Stmt& s : bara.stmts) {
        if (s.kind == bld::Stmt::Kind::Prop &&
            s.name != "name" && s.name != "x" && s.name != "y" &&
            s.name != "width" && s.name != "height") {
            std::string typ;
            for (const bld::WidgetNode& n : bara.nodes)
                if (n.id == s.target)
                    typ = n.type;
            if (!bld::isSetterField(typ, s.name))
                opaka += " prop:" + s.target + "." + s.name;
        }
    }
    check(opaka.empty(), "calls-fas1b: varje fältrad är ett känt setter-fält",
          opaka);

    glaze::AppWindow                  fönster("fas1b", 600, 400);
    const std::vector<glaze::Widget*> perNod =
        bld::buildWindowTree(fönster, bara, o);
    const auto hitta = [&](const char* id) -> glaze::Widget* {
        for (std::size_t i = 0; i < bara.nodes.size() && i < perNod.size(); ++i)
            if (bara.nodes[i].id == id)
                return perNod[i];
        return nullptr;
    };
    auto* kryss   = dynamic_cast<glaze::Checkbox*>(hitta("kryss"));
    auto* flerval = dynamic_cast<glaze::ListBox*>(hitta("flerval"));
    auto* flikar  = dynamic_cast<glaze::TabControl*>(hitta("flikar"));
    auto* sida1   = dynamic_cast<glaze::TabPage*>(hitta("sida1"));
    auto* flikar2 = dynamic_cast<glaze::TabControl*>(hitta("flikar2"));
    auto* rutnat  = dynamic_cast<glaze::DataGrid*>(hitta("rutnat"));
    auto* alfa    = dynamic_cast<glaze::ColorPicker*>(hitta("alfa"));
    auto* tid     = dynamic_cast<glaze::DatePicker*>(hitta("tid"));
    check(kryss && flerval && flikar && sida1 && flikar2 && rutnat && alfa &&
              tid,
          "calls-fas1b: alla åtta widgetar byggs");
    if (!kryss || !flerval || !flikar || !sida1 || !flikar2 || !rutnat ||
        !alfa || !tid)
        return;

    check(kryss->isThreeState() &&
              kryss->checkState() == glaze::CheckState::Indeterminate,
          "calls-fas1b: kryss står i mellanläget med tre lägen");
    check(flerval->isMultiSelect() &&
              flerval->selectedRows() == std::vector<int>{1, 3},
          "calls-fas1b: flerval har raderna 1 och 3 valda",
          std::to_string(flerval->selectedRows().size()));
    check(flikar->pageCount() == 3 && flikar->page(0) == sida1 &&
              flikar->page(1)->text() == "Andra sidan" &&
              flikar->page(2)->textToken() == glaze::tr(3),
          "calls-fas1b: flikar = medlemssidan först, sedan addPage-sidorna "
          "(satserna efter trädbygget)", std::to_string(flikar->pageCount()));
    check(flikar2->pageCount() == 3 && flikar2->page(0)->text() == "Ett" &&
              flikar2->page(1)->textToken() == glaze::tr(3) &&
              flikar2->page(2)->text() == "C" && flikar2->selectedIndex() == 1,
          "calls-fas1b: flikar2 = setPages + setPage, och valet före setPages "
          "står kvar", std::to_string(flikar2->selectedIndex()));
    check(rutnat->selectionMode() == glaze::DataGrid::SelectionMode::FullRow &&
              rutnat->isMultiSelect() &&
              rutnat->selectedRows() == std::vector<int>{0, 2},
          "calls-fas1b: rutnat har raderna 0 och 2 valda i radläget");
    check(alfa->allowAlpha() &&
              alfa->color() == gfx::Color{0x33, 0x66, 0xFF, 0x80},
          "calls-fas1b: alfa bär alfan 0x80 (setColor före setAllowAlpha)",
          alfa->colorText());
    check(tid->showsTime() && tid->text() == "2026-10-13 14:30",
          "calls-fas1b: tid visar datum och klockslag", tid->text());
    const glaze::StatusBar& sb = fönster.statusBar;
    check(sb.panelCount() == 3 && sb.panelText(0) == "Sparat" &&
              sb.panel(0).fill && sb.panel(1).text == glaze::tr(3) &&
              sb.panel(1).width == 110.0f && sb.panelText(2) == "75 %" &&
              sb.panel(2).width == 60.0f,
          "calls-fas1b: statusradens tre fält (setPanels, addPanel, setPanel, "
          "setPanelText)", std::to_string(sb.panelCount()));
}

// =========================================== ETIKETT- OCH KNAPPFÄLT
//
// Regel 8: varje ny setter UTFÖRS av previewn, mätt på WIDGETENS tillstånd —
// de elva setter-fälten genom `applyProp`, `dialogTitle` i båda textformerna
// (literal till `setProp`, `glaze::tr(N)` till `setTextToken` med tokenet
// bevarat) och knapparnas `setColor(std::optional)` genom `applyCall` i sina
// tre former. En bar `{200, 0, 0}` kompilerar inte till en optional och är
// OPAK: false, och widgeten ORÖRD. SABOTAGEN som ska fälla den: grenen för
// Button/Checkbox/Radio borttagen i `applyCall` (setColor opak), `dialogTitle`
// borttagen ur `ärTextfält` (tokenet blir text), en rad borttagen ur
// `kSetterFields` (kravmatrisen). MÄTER INTE ritningen — label_gate gör det.
void fallReq3()
{
    glaze::setLanguage("sv");
    const char* o = "preview_gate";

    for (const char* typ : {"glaze::ListView", "glaze::DataGrid"}) {
        auto  w = build(typ, {{"fillColumn", "1"}, {"iconSize", "48"}});
        auto* t = dynamic_cast<glaze::Table*>(w.get());
        check(t && t->fillColumn() == 1 && t->iconSize() == 48.0f,
              std::string("preview: ") + typ + ".setFillColumn/setIconSize utförs");
    }
    {
        auto  w = build("glaze::Label", {{"elide", "true"},
                                         {"vAlign", "glaze::VAlign::Bottom"},
                                         {"buddy", "\"falt\""}});
        auto* l = dynamic_cast<glaze::Label*>(w.get());
        check(l && l->elide() && l->vAlign() == glaze::VAlign::Bottom &&
                  l->buddy() == "falt",
              "preview: Label.setElide/setVAlign/setBuddy utförs");
    }
    {
        auto  b  = build("glaze::Button", {{"iconSize", "32"}});
        auto  tb = build("glaze::ToolButton", {{"iconSize", "24"}});
        auto* kb = dynamic_cast<glaze::Button*>(b.get());
        auto* kt = dynamic_cast<glaze::ToolButton*>(tb.get());
        check(kb && kb->iconSize() == 32.0f && kt && kt->iconSize() == 24.0f,
              "preview: Button/ToolButton.setIconSize utförs");
    }
    {
        auto  w = build("glaze::TextArea", {{"acceptsTab", "true"}});
        auto* a = dynamic_cast<glaze::TextArea*>(w.get());
        check(a && a->acceptsTab(), "preview: TextArea.setAcceptsTab utförs");
    }
    {
        auto  w = build("glaze::FilePicker", {{"dialogTitle", "\"Välj CSV-fil\""}});
        auto* f = dynamic_cast<glaze::FilePicker*>(w.get());
        check(f && f->dialogTitle() == "Välj CSV-fil" &&
                  f->dialogTitleToken().id() == 0,
              "preview: FilePicker.setDialogTitle — literalen", f ? f->dialogTitle() : "");
        if (f) {
            bld::applyProp(*w, {"dialogTitle", "glaze::tr(43)"}, o);
            check(f->dialogTitleToken().id() == 43,
                  "preview: FilePicker.setDialogTitle(glaze::tr(43)) bevarar tokenet",
                  f->dialogTitle());
        }
    }

    // KNAPPARNAS TEXTFÄRG — tre typer, tre former, och den opaka.
    for (const char* typ : {"glaze::Button", "glaze::Checkbox", "glaze::Radio"}) {
        auto w = build(typ, {});
        if (!w)
            continue;
        const auto färg = [&w]() -> std::optional<gfx::Color> {
            if (auto* b = dynamic_cast<glaze::Button*>(w.get()))
                return b->color();
            if (auto* k = dynamic_cast<glaze::Checkbox*>(w.get()))
                return k->color();
            if (auto* r = dynamic_cast<glaze::Radio*>(w.get()))
                return r->color();
            return std::nullopt;
        };
        const std::string t = typ;
        check(bld::applyCall(*w, {"setColor", "gfx::Color{200, 40, 40}"}, o) &&
                  färg() == std::optional<gfx::Color>(gfx::Color{200, 40, 40, 255}),
              "preview: " + t + ".setColor(gfx::Color{200, 40, 40}) utförs");
        check(!bld::applyCall(*w, {"setColor", "{200, 0, 0}"}, o) &&
                  färg() == std::optional<gfx::Color>(gfx::Color{200, 40, 40, 255}),
              "preview: " + t + ".setColor({200, 0, 0}) är OPAK och lämnar färgen");
        check(bld::applyCall(*w, {"setColor", "std::nullopt"}, o) && !färg(),
              "preview: " + t + ".setColor(std::nullopt) ger temat");
        bld::applyCall(*w, {"setColor", "gfx::Color{0, 0, 255}"}, o);
        check(bld::applyCall(*w, {"setColor", "{}"}, o) && !färg(),
              "preview: " + t + ".setColor({}) ger temat");
        check(!bld::isSetterField(typ, "color"),
              "preview: " + t + ".color är inget setter-fält (en anropssats)");
    }
}

// FÖNSTRETS STORLEK OCH KANTER: `setSizeToContent` är ett naket
// fönsteranrop med en STRIKT enumtabell (tre prefixformer, ett okänt namn är
// opakt och lämnar attributet), och `setAllowedEdges` ett metodanrop med en
// lista av KANTER — `{}` är en tom lista, `Fill` och ett namn utan prefix är
// opaka och lämnar fältet ORÖRT (atomiskt). Att det anpassade fönstret får
// innehållets höjd mäter geometry_gate SC; att `onResize` är en koppling
// mäter roundtrip_gate (fixturen).
void fallStorlekOchKanter()
{
    const char*      o = "preview_gate";
    glaze::AppWindow w("preview_gate", 400, 300, gfx::Chrome::Custom);
    check(bld::applyWindowCall(w, {"setSizeToContent",
                                   "glaze::SizeToContent::Height"}, o) &&
              w.sizeToContent() == glaze::SizeToContent::Height,
          "preview: setSizeToContent(glaze::SizeToContent::Height) utförs");
    check(bld::applyWindowCall(w, {"setSizeToContent", "WidthAndHeight"}, o) &&
              w.sizeToContent() == glaze::SizeToContent::WidthAndHeight,
          "preview: setSizeToContent(WidthAndHeight) — den bara formen");
    check(!bld::applyWindowCall(w, {"setSizeToContent",
                                    "glaze::SizeToContent::Auto"}, o) &&
              w.sizeToContent() == glaze::SizeToContent::WidthAndHeight,
          "preview: setSizeToContent(…::Auto) är opak och lämnar attributet");

    // LÅS ELLER GOLV — samma strikta tabell och tre
    // prefixformer; ett okänt värde är opakt och lämnar läget.
    check(bld::applyWindowCall(w, {"setSizeToContentMode",
                                   "glaze::SizeToContentMode::GrowOnly"}, o) &&
              w.sizeToContentMode() == glaze::SizeToContentMode::GrowOnly,
          "preview: setSizeToContentMode(glaze::SizeToContentMode::GrowOnly) utförs");
    check(bld::applyWindowCall(w, {"setSizeToContentMode", "GrowAndShrink"}, o) &&
              w.sizeToContentMode() == glaze::SizeToContentMode::GrowAndShrink,
          "preview: setSizeToContentMode(GrowAndShrink) — den bara formen");
    check(bld::applyWindowCall(w, {"setSizeToContentMode",
                                   "SizeToContentMode::GrowOnly"}, o) &&
              w.sizeToContentMode() == glaze::SizeToContentMode::GrowOnly,
          "preview: setSizeToContentMode(SizeToContentMode::GrowOnly) — typprefixet");
    check(!bld::applyWindowCall(w, {"setSizeToContentMode",
                                    "glaze::SizeToContentMode::Minimum"}, o) &&
              w.sizeToContentMode() == glaze::SizeToContentMode::GrowOnly,
          "preview: setSizeToContentMode(…::Minimum) är opak och lämnar läget");

    auto  tw = build("glaze::ToolBar", {});
    auto* t  = dynamic_cast<glaze::ToolBar*>(tw.get());
    check(t != nullptr, "preview: (fixturen) ett verktygsfält");
    if (!t)
        return;
    check(bld::applyCall(*t, {"setAllowedEdges",
                              "{glaze::Dock::Top, glaze::Dock::Bottom}"}, o) &&
              !t->allowsEdge(glaze::Dock::Left) &&
              t->allowsEdge(glaze::Dock::Top) && t->allowsEdge(glaze::Dock::Bottom),
          "preview: setAllowedEdges({Top, Bottom}) utförs");
    check(!bld::applyCall(*t, {"setAllowedEdges", "{glaze::Dock::Fill}"}, o) &&
              t->allowedEdges().size() == 2,
          "preview: setAllowedEdges({…::Fill}) är opak och lämnar fältet orört");
    check(!bld::applyCall(*t, {"setAllowedEdges", "{Top}"}, o) &&
              t->allowedEdges().size() == 2,
          "preview: setAllowedEdges({Top}) utan prefix är opak");
    check(bld::applyCall(*t, {"setAllowedEdges", "{}"}, o) &&
              t->allowedEdges().empty(),
          "preview: setAllowedEdges({}) — ingen kant");
}

// Etikettens BREDDGRUPP är ett setter-fält med en
// TYPAD gren — bara en strängliteral sätter gruppen; en identifierare (laglig
// C++, `lbl.setWidthGroup(grupp);`) är OPAK och etiketten ORÖRD, för den
// allmänna normaliseringen hade gjort den till gruppnamnet "grupp". Samma sak
// genom setter-fallbacken (en egen Label-subklass skrivs som ett anrop). Och
// calls-fixturens `setSizeToContentMode(GrowOnly)` — med flit inte förvalet —
// och `etikett.setWidthGroup("form")` UTFÖRS. SABOTAGEN som ska fälla den:
// `widthGroup` genom den allmänna `setProp`-vägen ("grupp" accepteras),
// enumgrenen för `setSizeToContentMode` borttagen (GrowOnly sätts inte) och
// raden borttagen ur `kSetterFields` (kravmatrisen). MÄTER INTE gruppens
// bredd (label_gate W) eller fönstrets storlek (geometry_gate SG).
void fallReq7()
{
    const char* o = "preview_gate";
    {
        auto  w = build("glaze::Label", {{"widthGroup", "\"form\""}});
        auto* l = dynamic_cast<glaze::Label*>(w.get());
        check(l && l->widthGroup() == "form",
              "preview: Label.setWidthGroup(\"form\") utförs",
              l ? l->widthGroup() : "");
        if (l) {
            bld::applyProp(*w, {"widthGroup", "grupp"}, o);
            check(l->widthGroup() == "form",
                  "preview: setWidthGroup(grupp) — en identifierare är OPAK och "
                  "etiketten orörd",
                  l->widthGroup());
        }
    }
    {
        struct MinEtikett : glaze::Label {};
        MinEtikett e;
        check(!bld::applyCall(e, {"setWidthGroup", "grupp"}, o) &&
                  e.widthGroup().empty(),
              "preview: setter-fallbacken — identifieraren är opak", e.widthGroup());
        check(bld::applyCall(e, {"setWidthGroup", "\"kolumn\""}, o) &&
                  e.widthGroup() == "kolumn",
              "preview: setter-fallbacken — literalen sätter gruppen",
              e.widthGroup());
    }
    check(bld::isSetterField("glaze::Label", "widthGroup"),
          "preview: widthGroup är ett setter-fält på Label");

    // CALLS-FIXTUREN: fönsteranropet och etikettens rad, satta som previewn
    // sätter dem.
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "preview: calls-fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    glaze::AppWindow fönster("preview", 400, 300);
    bool             läge = false;
    for (const bld::Stmt& s : r.model.stmts)
        if (s.kind == bld::Stmt::Kind::WindowCall &&
            s.name == "setSizeToContentMode")
            läge = bld::applyWindowCall(fönster, {s.name, s.value}, o);
    check(läge && fönster.sizeToContentMode() ==
                      glaze::SizeToContentMode::GrowOnly,
          "preview: fixturens setSizeToContentMode(GrowOnly) utförs");
    const bld::Stmt* grupp = nullptr;
    for (const bld::Stmt& s : r.model.stmts)
        if (s.kind == bld::Stmt::Kind::Prop && s.target == "etikett" &&
            s.name == "widthGroup")
            grupp = &s;
    check(grupp != nullptr, "preview: fixturens etikett bär widthGroup som Prop");
    if (grupp) {
        glaze::Label etikett;
        bld::applyProp(etikett, {grupp->name, grupp->value}, o);
        check(etikett.widthGroup() == "form",
              "preview: fixturens etikett.setWidthGroup(\"form\") utförs",
              etikett.widthGroup());
    }
}

// MARKDOWN-VYN: en RIKTIG `glaze::MarkdownView` ur
// registret (ingen stand-in). Dokumentet är setter-fältet `text`: en literal
// med `\n`, `\"` och `\\` når `text()` avkodad, `glaze::tr(N)` sätter TOKENET
// och `glaze::tr(<konstant>)` är opak och lämnar tokenet. `setImage(namn,
// bild)` och `scrollToAnchor(ankare)` är Call-satser som UTFÖRS — ATOMISKT:
// fel antal argument, ett namn som identifierare och en oparsbar bild är opaka
// och lämnar tabellen orörd —, och ANKARET VÄNTAR: rullningen är orörd tills
// vyn ritas (previewn ritar inte här). En okänd metod är opak, `setBorder` är
// Panels setter-fält, och calls-fixturens satser för `hjalp` utförs i
// källordning till sitt sluttillstånd. Körs efter fallProgramikon: bilden
// slås upp i demons assets.bin.
//
// SABOTAGEN som ska fälla den: MarkdownView-grenen i `applyCall` borttagen
// (setImage blir ett okänt fält i setter-fallbacken — bildtabellen tom),
// raden i `kTypeParents` borttagen (border är inget setter-fält — och
// matrisens Panel-orakel fäller), raden i `kSetterFields` borttagen
// (matrisen räknar en post för lite). MÄTER INTE hur vyn ritar (pixel_gate,
// markdown_gate) eller att ankaret rullar vid ritningen (markdown_gate W3).
void fallMarkdown()
{
    const char* o = "preview_gate";
    glaze::setLanguage("sv");
    auto w = build("glaze::MarkdownView",
                   {{"text", "\"# Rubrik\\n\\nEtt \\\"citat\\\" och \\\\ ett snedstreck\""}});
    if (!w)
        return;
    auto* md = dynamic_cast<glaze::MarkdownView*>(w.get());
    check(md != nullptr,
          "markdown: registret ger en riktig glaze::MarkdownView, ingen stand-in");
    if (!md)
        return;
    check(md->text() == "# Rubrik\n\nEtt \"citat\" och \\ ett snedstreck" &&
              md->textToken().id() == 0,
          "markdown: setText med \\n, \\\" och \\\\ når text() avkodad, som en "
          "literal",
          md->text());

    bld::applyProp(*w, {"text", "glaze::tr(2)"}, o);
    check(md->textToken() == glaze::tr(2),
          "markdown: setText(glaze::tr(2)) sätter TOKENET",
          std::to_string(md->textToken().id()));
    bld::applyProp(*w, {"text", "glaze::tr(str::Ok)"}, o);
    check(md->textToken() == glaze::tr(2),
          "markdown: setText(glaze::tr(<konstant>)) är opak och lämnar tokenet",
          std::to_string(md->textToken().id()));

    // BILDTABELLEN — en asset, borttagningen och de opaka formerna.
    const glaze::ImageSource info(glaze::img("toolbar", "info"));
    check(info.valid(), "markdown: (fixturen) toolbar/info finns i demons assets");
    check(bld::applyCall(*w, {"setImage", "\"logo\", glaze::img(\"toolbar\", \"info\")"},
                         o) &&
              md->image("logo") == info,
          "markdown: setImage(\"logo\", glaze::img(…)) utförs");
    check(bld::applyCall(*w, {"setImage", "\"logo\", {}"}, o) &&
              !md->image("logo").valid(),
          "markdown: setImage(\"logo\", {}) tar bort posten");
    bld::applyCall(*w, {"setImage", "\"logo\", glaze::img(\"toolbar\", \"info\")"}, o);
    check(!bld::applyCall(*w, {"setImage", "\"logo\""}, o) &&
              md->image("logo") == info,
          "markdown: setImage med ETT argument är opak och lämnar tabellen");
    check(!bld::applyCall(*w, {"setImage", "logo, {}"}, o) &&
              md->image("logo") == info,
          "markdown: ett namn som identifierare är opakt");
    check(!bld::applyCall(*w, {"setImage", "\"logo\", minBild"}, o) &&
              md->image("logo") == info,
          "markdown: en oparsbar bild är opak — tabellen orörd (atomiskt)");

    // ANKARET — känt och utfört, men det VÄNTAR på ritningen.
    check(bld::applyCall(*w, {"scrollToAnchor", "\"rubrik\""}, o) &&
              md->scrollY() == 0.0f,
          "markdown: scrollToAnchor(\"rubrik\") utförs och VÄNTAR — rullningen "
          "orörd tills vyn ritas");
    check(!bld::applyCall(*w, {"scrollToAnchor", "rubrik"}, o),
          "markdown: scrollToAnchor(identifierare) är opak");
    check(!bld::applyCall(*w, {"scrollToAnchor", "\"a\", \"b\""}, o),
          "markdown: scrollToAnchor med två argument är opak");
    check(!bld::applyCall(*w, {"rullaTill", "\"rubrik\""}, o),
          "markdown: en okänd metod är opak");

    bld::applyProp(*w, {"border", "true"}, o);
    check(md->isBorder(), "markdown: setBorder(true) — Panels setter-fält");
    check(bld::isSetterField("glaze::MarkdownView", "text") &&
              bld::isSetterField("glaze::MarkdownView", "border") &&
              bld::isOrDerivesFrom("glaze::MarkdownView", "glaze::Panel"),
          "markdown: text är ett setter-fält, och vyn ärver Panels border");

    // CALLS-FIXTUREN: `hjalp`s satser i källordning — literalen, sedan
    // tr-id:t, bilderna, ankaret och ramen.
    bld::ReadResult r = bld::readWindow(std::string(GLAZE_TEST_ROOT) +
                                        "/tests/fixtures/calls.h");
    check(r.ok, "markdown: calls-fixturen läses (" + r.error + ")");
    if (!r.ok)
        return;
    auto f = build("glaze::MarkdownView", {});
    if (!f)
        return;
    auto* fm = static_cast<glaze::MarkdownView*>(f.get());
    int   texter = 0;
    for (const bld::Stmt& s : r.model.stmts) {
        if (s.target != "hjalp")
            continue;
        if (s.kind == bld::Stmt::Kind::Prop && s.name == "text")
            ++texter;
        bld::applyStmt(*f, s, o);
    }
    check(texter == 2,
          "markdown: fixturens två setText är Prop-satser (literalen och tr-id:t)",
          std::to_string(texter));
    check(fm->textToken() == glaze::tr(2) && fm->image("logo") == info &&
              !fm->image("tom").valid() && fm->isBorder() &&
              fm->scrollY() == 0.0f,
          "markdown: fixturens satser ger tokenet, bilden, den tomma posten, "
          "ramen och ett väntande ankare");
}

// TEXTSTORLEKEN — setter-fältet `textSize` på de fyra som har det:
// fältraden når sättaren TYPAT (mm, px och ett naket tal), ett värde som inte
// är ett mått och ett i världsenheter lämnar widgeten orörd, och en egen
// subklass (en Call) går samma typade väg genom setter-fallbacken. MÄTER INTE
// att storleken ritas och mäts — textfield_gate T9 gör det i ett fönster.
// SABOTAGEN: grenen i `applyProp` borttagen (måttet går till `setProp`, som
// ingen textform har — ingen storlek), `harTextstorlek` utan en av typerna,
// fallbackens gren borttagen (den egna subklassen får ingen storlek).
struct EgetFalt : glaze::TextInput {};

void fallTextstorlek()
{
    const auto lika = [](const glaze::Length& l, float v, glaze::Unit u) {
        return l.value == v && l.unit == u;
    };
    for (const char* typ : {"glaze::TextInput", "glaze::TextArea",
                            "glaze::ListBox", "glaze::Dropdown"}) {
        check(bld::isSetterField(typ, "textSize"),
              std::string("textSize: setter-fält på ") + typ, "");
        auto w = build(typ, {{"textSize", "glaze::mm(3)"}});
        if (!w)
            return;
        glaze::Length l;
        if (auto* f = dynamic_cast<glaze::TextInput*>(w.get()))
            l = f->textSize();
        else if (auto* a = dynamic_cast<glaze::TextArea*>(w.get()))
            l = a->textSize();
        else if (auto* b = dynamic_cast<glaze::ListBox*>(w.get()))
            l = b->textSize();
        else if (auto* d = dynamic_cast<glaze::Dropdown*>(w.get()))
            l = d->textSize();
        check(lika(l, 3.0f, glaze::Unit::Mm),
              std::string("textSize: glaze::mm(3) når ") + typ,
              std::to_string(l.value));
    }
    auto w = build("glaze::TextInput", {{"textSize", "glaze::px(18)"}});
    if (!w)
        return;
    auto* f = static_cast<glaze::TextInput*>(w.get());
    check(lika(f->textSize(), 18.0f, glaze::Unit::Px),
          "textSize: glaze::px(18) i px", std::to_string(f->textSize().value));
    bld::applyProp(*f, {"textSize", "14"}, "preview_gate");
    check(lika(f->textSize(), 14.0f, glaze::Unit::Px),
          "textSize: ett naket tal är px", std::to_string(f->textSize().value));
    for (const char* fel : {"3mm", "glaze::mm()", "storlek", "glaze::world(2)",
                            "glaze::mm(-1)"}) {
        bld::applyProp(*f, {"textSize", fel}, "preview_gate");
        check(lika(f->textSize(), 14.0f, glaze::Unit::Px),
              std::string("textSize: '") + fel + "' lämnar fältet orört",
              std::to_string(f->textSize().value));
    }
    bld::applyProp(*f, {"textSize", "0"}, "preview_gate");
    check(lika(f->textSize(), 0.0f, glaze::Unit::Px),
          "textSize: 0 är stilens", std::to_string(f->textSize().value));
    check(!bld::isSetterField("glaze::Label", "textSize") &&
              !bld::isSetterField("glaze::TreeView", "textSize"),
          "textSize: inte på Label och TreeView", "");
    // En EGEN subklass emitteras som ett anrop och går setter-fallbacken.
    EgetFalt eget;
    check(bld::applyCall(eget, {"setTextSize", "glaze::mm(2.5)"},
                         "preview_gate") &&
              lika(eget.textSize(), 2.5f, glaze::Unit::Mm),
          "textSize: en egen TextInput-subklass genom setter-fallbacken",
          std::to_string(eget.textSize().value));
    check(!bld::applyCall(eget, {"setTextSize", "2mm"}, "preview_gate") &&
              lika(eget.textSize(), 2.5f, glaze::Unit::Mm),
          "textSize: fallbacken avvisar ett värde som inte är ett mått", "");
    glaze::Length mått;
    check(bld::parseLength("glaze::mm(3)", mått) &&
              lika(mått, 3.0f, glaze::Unit::Mm) &&
              !bld::parseLength("3 mm", mått),
          "textSize: parseLength (egenskapspanelens grind)", "");
}

int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

    std::printf("=== preview_gate — verktygsvägen (bld::applyProp) ===\n");
    fallNamn();
    fallNamnSlarMedlemsnamn();
    fallGamlaIdNyckeln();
    fallBasfalt();
    fallSetProp();
    fallAlign();
    fallReadOnly();
    fallLosenordAngra();
    fallDialogerOchFalt();
    fallWrap();
    fallRoll();
    fallAnrop();
    fallFonsteranrop();
    fallOpak();
    fallItemsLista();
    fallItemsForlustfri();
    fallSelectedIndex();
    fallListBoxIPaletten();
    fallSyskonankare();
    fallSyskonankareKortForm();
    fallSyskonankareOpak();
    fallSyskonankareEmission();
    fallTalgrind();
    fallEscapeText();
    fallEscapeOpak();
    fallEscapeMeny();
    fallTrText();
    fallMenyStigPreview();
    fallDesignSprak();
    fallDemoOversatt();
    fallStandInEnhet();
    fallKomponenter();
    fallEgnaKomponenter();
    fallEgnaWidgetar();
    fallRadioEnval();
    fallFonsterRam();
    fallSetterMatris();
    fallVerktygsfalt();
    fallGruppram();
    fallTalfalt();
    fallDelare();
    fallDockning();
    fallVerktygsfaltFlytande();
    fallTrad();
    fallTooltip();
    fallListvy();
    fallDatagrid();
    fallFlikar();
    fallFilvaljarfalt();
    fallFargvaljare();
    fallDatumvaljare();
    fallCanvas();
    fallLagerordning();
    fall3D();
    fallCallsRityta();
    fallBindningar();
    fallCallsTradTabell();
    fallCallsDockning();
    fallFlerradigt();
    fallDragDrop();
    fallCommitFokusMarkering();
    fallInmatningEvents();
    fallAppMusEvents();
    fallPropertyGridStyckeform();
    fallStyckeformAtomisk();
    fallSetRowsParser();
    fallListStyckeform();
    fallListBoxChecked();
    fallNameWidth();
    fallRestoreGlyph();
    fallLabelSetColor();
    fallPreviewVandring();
    fallProgramikon();   // SIST — laddar demons assets.bin (se fallet)
    fallSystemfaltetsPoster();   // efter: bilden slås upp i samma assets
    fallKnappmeny();             // likaså
    fallFas1B();
    fallCallsFas1B();
    fallReq3();
    fallStorlekOchKanter();
    fallReq7();
    fallMarkdown();   // efter fallProgramikon: toolbar/info ur demons assets
    fallTextstorlek();
    fallTerminal();      // bara med sidopaketet

    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "PREVIEW_GATE GRÖN" : "PREVIEW_GATE FÄLLD",
                g_cases, g_fails);
    return g_fails == 0 ? 0 : 1;
}
