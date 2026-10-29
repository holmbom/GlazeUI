#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gfx/events.h>
#include <gfx/geometry.h>

#include "glaze/canvas/image_source.h"   // MenuItem::icon
#include "glaze/language.h"
#include "glaze/widget.h"

// MENYN — meny, snabbmeny, undermenyer och acceleratorer.
// Menyraden är en WIDGET och bor i `glaze/widgets/menu_bar.h`; det här är
// DATASTRUKTUREN och visningen av den.
//
// Filen ligger i katalogROTEN och inte i `widgets/`, av exakt samma skäl
// som `message_box.h`: en `Menu` är ingen widget — den registreras inte,
// kan inte adopteras och finns aldrig i en `.glazeui`. Visningen är ett
// popup-`AppWindow` som bor helt i `src/menu.cpp`, precis som
// `Dropdown::ListWindow` och `MessageBoxWindow`.
//
// RADERNA ÄR DATA (structar, inte widgets) — husets svar, som i
// PropertyGrid och ListBox, och för en meny med undermenyer vore
// alternativet N fönster × M widgets.
//
//     glaze::Menu snabb;
//     snabb.addItem("Klipp ut").shortcut = {gfx::Key::X, gfx::kModCtrl};
//     snabb.addSeparator();
//     snabb.addCheck("Visa rutnät", true).onClick = [this] { … };
//     root().onContextMenu = [this, &snabb](glaze::Widget* s,
//                                           glaze::ContextMenuArgs& e) {
//         snabb.popupAt(*s, e.x, e.y);
//     };

namespace glaze {

class MenuPresentation;   // intern visning (src/menu.cpp)

// ------------------------------------------------------------- GENVÄGEN
//
// MATCHNING och PRESENTATION är SKILDA saker, och det är skrivet för att
// de annars glider ihop: `{key, mods}` är det som MATCHAS mot en tangent,
// medan visningstexten HÄRLEDS (`shortcutText`) och är plattformsberoende.
// Skriv aldrig in en visningstext i itemets semantik.
//
// `gfx::Key` är LEVEL-0-SYMBOLEN i användarens aktiva layout — den symbol
// tangenten ger UTAN modifierare (kontraktet står vid `enum class Key` i
// `gfx/events.h`). FÄLLAN som följer av det: "Ctrl+Plus" på ett svenskt
// tangentbord är `{Key::Plus, kModCtrl}` och kräver INGEN Shift, medan
// samma etikett på ett amerikanskt är Shift+Equals. Bind den symbol du
// menar, inte den tangent du minns.
struct Shortcut {
    gfx::Key      key  = gfx::Key::Unknown;
    std::uint32_t mods = 0;   // gfx::kMod*-mask

    bool set() const { return key != gfx::Key::Unknown; }

    // EXAKT modifierarmatchning: Ctrl+S träffar inte på Ctrl+Shift+S.
    bool matches(gfx::Key k, std::uint32_t m) const
    {
        return set() && k == key && m == mods;
    }
};

// Genvägens VISNINGSTEXT. Modifierarordningen är `Ctrl · Alt · Shift ·
// Super`; macOS visar symbolerna (⌃ ⌥ ⇧ ⌘) utan plustecken, övriga
// textformen (`Ctrl+S`). En tangent utan namn (och `Key::Unknown`) ger en
// definierad reservtext i stället för tomhet, så en felbunden genväg SYNS.
std::string shortcutText(const Shortcut& s);

// TEXT -> GENVÄG, spegeln av `shortcutText` — den form `.glazeui` bär
// ("Ctrl+N"). Modifierarna i valfri ordning; okänd text ger en OSATT
// genväg och en loggrad, aldrig en gissning.
//
// ⚠ SERIALISERINGEN ÄR KANONISK OCH ENGELSK, medan VISNINGEN översätts —
// annars hade en fil sparad på ett språk inte kunnat parsas på ett annat.
// Använd `canonicalKeyName`/`parseCanonicalKey` nedan i allt som SPARAS.
Shortcut parseShortcut(const std::string& text);

// TANGENTENS KANONISKA NAMN — den STABILA formen som `.glazeui` bär och
// som `parseShortcut` matchar. Engelsk, täcker HELA `gfx::Key` inklusive
// numpaden, och ändras ALDRIG av ett språkbyte. `Key::Unknown` och
// oräknade värden ger `"?"`.
const char* canonicalKeyName(gfx::Key k);
// Spegeln: kanoniskt namn → symbol. `Key::Unknown` = okänt namn.
gfx::Key parseCanonicalKey(const std::string& name);

// ---------------------------------------------------------------- STIGEN
//
// MENYSTIGEN ÄR SEGMENTERAD — ingen `/`-splittning av en sträng.
//
//     menuBar.addSubmenu("Utseende", glaze::tr(str::Mode));
//     menuBar.addRadio({glaze::tr(str::Appearance), glaze::tr(str::Mode)},
//                      glaze::tr(str::System));
//
// SKÄLET: en stig som EN sträng med flera led splittad på `/`
// (`"Utseende/Läge"`) kan aldrig bäras av ett enda `TrText`, som bara har ett
// stabilt id. Segmenten är därför var för sig ett token.
//
// DE TRE ENKELSEGMENT-CTORERNA ÄR NÖDVÄNDIGA, inte bekvämlighet: med
// enbart `initializer_list<TrText>` kan `"Arkiv"` inte bli en `MenuPath`,
// eftersom det skulle kräva TVÅ användardefinierade konverteringar
// (`const char* → TrText → MenuPath`) — och varenda befintligt
// `addItem("Arkiv", …)` hade slutat kompilera.
//
// VALIDERINGEN LIGGER I EN PRIVAT VALIDATOR SOM ALLA CTORER GÅR GENOM.
// Överlagringarna är entydiga, men LISTINITIALISERING prioriterar
// `initializer_list`, så `MenuPath{"a/b"}` går genom LISTctorn och inte
// genom `const char*`-ctorn: låg kontrollen bara i enkelsegment-ctorerna
// kunde klammerformen kringgå den.
//
// DEN TOMMA STIGEN ÄR ROTEN: programmenyn
// (`AppIcon`) har ingen rubriknivå, så dess poster läggs direkt i menyn med
// `appIcon.addItem({}, "Nytt")`. Den förvalda ctorn är därför ICKE-explicit
// — `{}` som argument blir den tomma stigen, utan loggrad. `MenuBar`
// avvisar den fortfarande: dess toppnivå är rubriker och inte poster, och
// där är tom stig "okänd stig".
//
// AVVISAS (logg + en OGILTIG stig, som aldrig matchar något — inte ens som
// roten, så en felskriven stig aldrig hamnar i programmenyns rot):
//   * tomt LITERALsegment
//   * `/` i ett LITERALsegment — en gammal stig skriven som
//     `"Utseende/Läge"` skulle annars tyst bli ETT segment med snedstreck i
//     namnet och bara ge ett diffust "okänd stig".
class MenuPath {
public:
    MenuPath() = default;                          // den TOMMA stigen = roten
    MenuPath(const char* s);                       // ETT segment
    MenuPath(std::string s);                       // ETT segment
    MenuPath(TrText t);                            // ETT segment
    MenuPath(std::initializer_list<TrText> segs);  // flera segment
    // DYNAMISKT antal segment — verktygsvägen (`bld::applyCall` bygger
    // stigen ur `.glazeui`-textens segment) kan inte använda en
    // initializer_list, vars längd är känd i kompileringstid. `explicit`
    // med flit: en `std::vector<TrText>` ska aldrig tyst bli en stig.
    explicit MenuPath(std::vector<TrText> segs);

    const std::vector<TrText>& segments() const { return segments_; }
    bool        empty() const { return segments_.empty(); }
    // false för en AVVISAD stig (se ovan) — den är också tom, men den är
    // inte roten och får aldrig matcha något.
    bool        valid() const { return !rejected_; }
    std::size_t size()  const { return segments_.size(); }
    const TrText& operator[](std::size_t i) const { return segments_[i]; }

    // Stigen minus sitt SISTA segment — `itemAtPath` behöver föräldern.
    MenuPath parent() const;
    // Läsbar form för LOGGAR (`"Utseende/⟦42⟧"`). Aldrig för matchning.
    std::string debugText() const;

private:
    // ÄGANDE lagring: en `initializer_list` äger inte sina element efter
    // uttrycket, så segmenten KOPIERAS hit. Aldrig list::begin/end,
    // aldrig span.
    void validate();
    std::vector<TrText> segments_;
    bool                rejected_ = false;
};

// MATCHNINGSREGELN, EN sanning: id mot id jämför ID,
// literal mot literal jämför LITERAL, och BLANDNING id/literal är ett
// TYPFEL — `typeError` sätts, och anroparen loggar och gör no-op.
//
// "Upplöst text vid blandning" är uttryckligen förkastat: det vore
// språkberoende, kunde byta resultat i drift, gjorde identiteten
// icke-transitiv och hade låtit två OLIKA id med samma översättning bli
// samma meny.
bool menuTokenMatches(const TrText& a, const TrText& b, bool& typeError);

// ---------------------------------------------------------------- posten
//
// EN HANDLER PER ITEM: ingen tag-tabell, ingen nästlad pump.
// Identifieringen blir trivial och den korta lambdaformen fungerar gratis
// (`onClick = [this] { … };`).
struct MenuItem {
    // TEXTEN ÄR ETT TOKEN: literal ELLER
    // översättnings-id. `addItem("Spara")` kompilerar oförändrat.
    TrText      text;
    ImageSource icon{};        // valfri bild i glyfkolumnen (asset eller
                               // pixlar — `setItemIcon` är den platta formen)
    Shortcut    shortcut{};

    bool enabled   = true;
    bool checkable = false;    // bär en bock
    bool checked   = false;
    // RADIOMARKERING: gruppen är de SAMMANHÄNGANDE radio-posterna mellan
    // separatorer eller icke-radiorader i SAMMA meny (wxWidgets
    // `AppendRadioItem`-semantik). Inget grupp-id behövs — och utan den
    // definitionen går uteslutningen inte att skriva ett test för.
    bool radio     = false;
    bool separator = false;

    // Undermenyn. Ett item med undermeny fyrar ALDRIG `onClick` — det
    // ÖPPNAR. (Aktiveringstabellen står i menu.cpp.)
    std::vector<MenuItem> submenu;

    Handler<ClickArgs> onClick;
};

// ------------------------------------------------------------------ Menu
//
// `Menu` är REN DATA som appen äger. VISNINGEN sker genom en intern
// `MenuPresentation` som `popupAt` skapar och som lever tills kedjan
// stängts — se menu.cpp. Reglerna, skrivna eftersom de annars blir
// dinglande pekare:
//
//   1. Presentationen tar en SNAPSHOT av det som ritas, plus en stabil väg
//      tillbaka till itemet: en INDEX-STIG (`{2, 0, 3}`), aldrig en
//      pekare. En `push_back` i en `std::vector<MenuItem> submenu`
//      reallokerar, och en rå itempekare hade dött med den.
//   2. MUTATION medan menyn visas slår INTE igenom i den öppna
//      presentationen. Alternativet vore att varje setter måste hitta och
//      invalidera alla öppna presentationer.
//   3. En `Menu` som FÖRSTÖRS medan den visas stänger sina presentationer
//      i destruktorn — den känner dem.
//   4. Samma `Menu` från två ställen ger TVÅ presentationer. Ingen delad
//      fönsterinstans: `showOwned` höjer bara ett redan öppet fönster utan
//      att byta ägare eller ankare, så återanvändning vore odefinierad.
//   5. Efter att en item-handler körts får ingen kod röra det gamla itemet
//      eller anta att roten fortfarande är öppen — handlern kan ha stängt
//      ägarfönstret, muterat menyn eller öppnat en ny meny.
class Menu {
public:
    Menu() = default;
    ~Menu();
    // ICKE KOPIERBAR OCH ICKE FLYTTBAR: presentationerna pekar tillbaka
    // hit, och en flytt hade lämnat dem dinglande. En menyrad lagrar sina
    // menyer bakom `unique_ptr` av precis det skälet.
    Menu(const Menu&)            = delete;
    Menu& operator=(const Menu&) = delete;
    Menu(Menu&&)                 = delete;
    Menu& operator=(Menu&&)      = delete;

    // Posterna är ett publikt VÄRDE-fält — samma val som
    // `PropertyGrid::rows()`: innehållet är data appen bygger fritt, och
    // ingen setter kan ändå invalidera en öppen presentation (regel 2).
    std::vector<MenuItem> items;

    // Bekvämlighet. RETURVÄRDET ÄR EN REFERENS IN I `items` och gäller
    // bara till NÄSTA add — vektorn reallokerar. Formen är byggd för att
    // användas i en enda sats:
    //     m.addItem("Spara").onClick = [this] { spara(); };
    MenuItem& addItem(TrText text);
    MenuItem& addCheck(TrText text, bool checked = false);
    MenuItem& addRadio(TrText text, bool checked = false);
    MenuItem& addSeparator();
    // Undermenyn fylls via den returnerade postens `submenu`.
    MenuItem& addSubmenu(TrText text);

    // SNABBMENYN — icke-blockerande: funktionen returnerar
    // direkt och menyn lever vidare i sina egna popup-fönster.
    // `x`/`y` är FYSISKA px i VÄRDFÖNSTRETS klientkoordinater, alltså
    // exakt vad `ContextMenuArgs` bär. Värden måste sitta i ett ÖPPET
    // fönster (annars no-op + loggrad), och en TOM meny öppnar ingenting.
    void popupAt(Widget& host, float x, float y);

    // Samma sak med ett riktigt ANKARRECT (menyradens rubrik): menyn
    // hamnar UNDER rectet. Fysiska px i värdfönstrets klientkoordinater.
    void popupAtRect(Widget& host, const gfx::Rectf& anchor);

    // Stäng kedjan (uppskjutet, som allt annat stängande).
    void close();
    bool isOpen() const { return !presentations_.empty(); }

    // ACCELERATORVÄGEN: leta rekursivt efter en AKTIVERAD post vars
    // genväg matchar och aktivera den — SAMMA `activate(stig)` som klick
    // och Enter går igenom, aldrig en parallell commitväg.
    // `sender` blir handlerns sender. true = tangenten konsumerades.
    bool tryShortcut(gfx::Key k, std::uint32_t mods, Widget* sender);

    // Kedjan stängdes (alla nivåer). Sender är värd-widgeten.
    Handler<EventArgs> onClose;

    // INTERN motsvarighet som menyRADEN använder för sin egen bokföring
    // (vilken rubrik som är öppen). SKILD från `onClose` ovan med flit:
    // hade raden lagt beslag på appens event hade `menuBar.menuAt(0)
    // ->onClose = …` tyst slagit ut rubrikmarkeringen. Båda körs.
    std::function<void()> onChainClosed;

    // INTERN krok som menyRADEN sätter: Höger/Vänster på ROTNIVÅN ska
    // byta RUBRIK i stället för att göra ingenting. `dir` är +1/−1;
    // returnera true om bytet gjordes. Tom för en fristående snabbmeny.
    std::function<bool(int dir)> stepSibling;

    // Aktivera posten på en INDEX-STIG — den ENDA vägen in i en
    // aktivering. Publik för att menyraden och testerna ska nå den;
    // `pres` är alltid nullptr utifrån.
    bool activatePath(const std::vector<int>& path, Widget* sender,
                      MenuPresentation* pres = nullptr);

    // Posten på en stig, eller nullptr om stigen inte finns längre
    // (menyn kan ha muterats sedan snapshotten togs).
    MenuItem* itemAt(const std::vector<int>& path);

private:
    friend class MenuPresentation;
    std::vector<MenuPresentation*> presentations_;
};

} // namespace glaze
