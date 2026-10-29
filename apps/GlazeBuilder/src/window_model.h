#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// WindowModel — GlazeBuilders värdesemantiska modell av ETT fönster i
// tre-fils-modellen. Ren std, fönsterfri och
// headless-testbar: cpp_reader fyller den ur .h + *.glazeui, cpp_writer
// emitterar tillbaka. Egenskapsvärden lagras som RÅ C++-uttryckstext
// (t.ex. "\"Tryck här\"", "130", "{false, false, true, true}",
// "glaze::Dock::Top") — det är så okända fältrader (egna widgets fält)
// round-trippas ordagrant utan att buildern förstår dem.

namespace bld {

// En fältsättningsrad i initializeComponent: <id>.<field> = <value>;
// LAGRINGEN är `Stmt` (satslistan nedan) — den här structen är den
// LÖSKOPPLADE formen som `bld::applyProp` tar emot, alltså ett par och
// inget ägande.
struct Prop {
    std::string field;   // "text", "width", "anchors", eller okänt (opak)
    std::string value;   // rå C++-uttryckstext, ordagrann
};

// En METODANROPSSATS i initializeComponent: <id>.<metod>(<argument>);
// Lagras som `Stmt`; den här structen är formen
// `bld::applyCall`/`bld::applyWindowCall` tar emot.
//
// Varför formen finns: `.glazeui` är riktig C++ som KOMPILERAS, och
// tillstånd som `hide()`/`disable()`/`setIndeterminate(true)` går inte att
// uttrycka som fältrader — `visible_` är privat och API:t är
// `setVisible()`/`show()`/`hide()`, just för att settern är den som
// INVALIDERAR (regel 1). Ett publikt `visible`-fält hade gått runt den.
// Alltså: en satsform till i grammatiken, hellre än en vitlista som tar
// genvägen en gång per widget.
//
// ARGUMENTET är RÅ C++-uttryckstext, precis som ett fältvärde — det är så
// okända anrop round-trippas ordagrant utan att buildern förstår dem.
// Tom sträng = inga argument.
struct Call {
    std::string method;   // "show", "disable", "setIndeterminate", eller okänt
    std::string args;     // rå C++-uttryckstext mellan parenteserna
};

// En event-koppling: <id>.<event> = [this](<sender>* s,
// <args>& e) { <handler>(s, e); };  (hård regel 6 — exakt den formen).
// SENDERTYPEN ÄR FRI: den som avfyrar är
// sender (`Handler<Args, Sender>`), så en `glaze::Timer`s koppling bär
// `glaze::Timer* s` och en `glaze::TrayIcon`s `glaze::TrayIcon* s`.
struct EventBind {
    std::string event;     // "onClick", "onChange", "onLoad", ...
    std::string args;      // "glaze::ClickArgs" (fullt kvalificerad)
    std::string handler;   // "okBtnClicked"

    // DEN KORTA FORMEN: `btn.onClick = [this] { klick(); };`
    // — lambdan tar inga parametrar och handlern likaså. `glaze::Handler`
    // tar emot båda formerna, så raden kompilerar och FUNGERAR i den byggda
    // appen; readern känner båda, så kopplingen aldrig blir en opak fältrad
    // som buildern skriver över med en andra `onClick`.
    //
    // FORMEN MINNS EXPLICIT och härleds INTE ur tom `args`: kort in ⇒ kort
    // ut. Att expandera till fullformen går inte att göra säkert — handlern
    // tar noll argument, så `klick(s, e)` hade inte kompilerat, och att
    // rätta signaturen vore att skriva i användarens `.cpp` (regel 5).
    bool        shortForm = false;

    // FULLFORMENS SENDERTYP, kvalificerad (`glaze::Timer`). TOM = `glaze::
    // Widget` — det vanliga fallet lagras inte, så varje
    // `EventBind{…}` utan sendertyp betyder en widgetkoppling. Står SIST av samma skäl:
    // ett aggregat som initieras positionellt får aldrig en ny medlem i
    // mitten.
    std::string sender;
};

// En SATS i initializeComponent-kroppen.
//
// Satslistan är modellens ENDA sanning om kroppen, och den bär
// KÄLLORDNINGEN. Separata listor (props/calls/events per nod + adoptioner +
// titel/storlek som egna fält) hade krävt GRUPPVIS emission — och ett
// handskrivet anrop som stod EFTER `adopt()` hade flyttats före hela
// trädbygget, varvade fält/anrop hade kastats om. Regel 4 säger att okänt
// bevaras ORDAGRANT, och ordningen är en del av det: satserna är exekverbar
// C++, inte en mängd.
//
// Formen är MEDVETET utbyggbar (t.ex. en variabeldeklaration): en ny
// satstyp är en `Kind` + en reader-gren + en
// writer-gren, aldrig en ny lista.
struct Stmt {
    enum class Kind {
        WindowCall,    // setTitle(...) / setClientSize(...) / valfritt naket
                       // fönsteranrop
        WindowEvent,   // glaze::AppWindow::onLoad = [this](...) { ... };
        Prop,          // <id>.<fält> = <värde>;
        Call,          // <id>.<metod>(<argument>);
        Event,         // <id>.<event> = [this](...) { ... };
        Adopt,         // <förälder>.adopt(<id>);
    };

    Kind        kind = Kind::Prop;
    // Mottagarens id (Prop/Call/Event) respektive det ADOPTERADE barnet
    // (Adopt). Tom för WindowCall/WindowEvent — de talar till fönstret.
    std::string target;
    // Fältnamn (Prop) · metodnamn (Call/WindowCall) · eventnamn
    // (Event/WindowEvent) · FÖRÄLDERN (Adopt; tom sträng = `root()`).
    std::string name;
    // Rå C++-uttryckstext, ordagrann: fältvärdet (Prop) respektive
    // argumenten mellan parenteserna (Call/WindowCall). Tom = inga.
    std::string value;
    std::string args;      // Event/WindowEvent: "glaze::ClickArgs"
    std::string handler;   // Event/WindowEvent: "okBtnClicked"
    // Event/WindowEvent: den PARAMETERLÖSA lambdaformen —
    // se `EventBind::shortForm`. Tom `args` duger inte som signal; formen
    // ska minnas explicit så writern kan skriva tillbaka den som den stod.
    bool        shortForm = false;
    // Event/WindowEvent: fullformens sendertyp — se `EventBind::sender`
    // (tom = `glaze::Widget`).
    std::string sender;
};

struct WidgetNode {
    std::string type;      // C++-typnamn ur medlemsdeklarationen
    std::string id;        // medlemsnamnet (= identiteten)
    bool        standIn = false;   // typen saknas i paletten

    // INBYGGD FÖNSTER-MEDLEM: noden motsvarar inte en
    // medlemsdeklaration i GlazeBuilder-regionen utan ett fält som
    // `glaze::AppWindow` redan har (i dag bara `statusBar`). Den skapas av
    // läsaren vid FÖRSTA referensen och känns igen på namnet
    // (`builtinMember` nedan).
    //
    // Konsekvenser, alla i cpp_writer: ingen medlemsrad i regionen (den
    // hade skuggat basklassens fält och inte kompilerat) och ingen
    // adopt-rad (fönstret adopterar den redan i sin konstruktor). Därför
    // är `adopted` FALSKT för en builtin — flaggan betyder "har en
    // adopt-rad", inte "sitter i trädet", och de två är olika saker just
    // här. Trädbyggarna särhanterar den i stället.
    bool        builtin = false;
};

struct WindowModel {
    std::string className;            // "StartWindow" (ur ctor-deklarationen)

    // DESIGNTAGGEN: fönstrets utseende för
    // VERKTYGEN — texten EFTER `// glazeui-design:`, trimmad. En rå
    // std::string håller builder_core ren std: den vet inget om
    // glaze::UI/Theme/Style — den avkodningen bor i design_theme.h.
    //
    // `hasDesign` är NÄRVARON och `designLine` är INNEHÅLLET, och de är
    // skilda för att en tom tagg (`// glazeui-design:` utan nycklar) är
    // närvarande men innehållslös. Med bara strängen hade writern utelämnat
    // den och första spara RADERAT användarens rad — regel 4 säger att
    // buildern aldrig kastar rader den inte förstår. Tagglösa tripletter
    // (hasDesign == false) emitterar ingen rad och får därför noll diff.
    bool        hasDesign = false;
    std::string designLine;

    // NODORDNINGEN och SATSORDNINGEN är två olika saker, och det är
    // avsiktligt:
    //
    //   `stmts` bär KÄLLORDNINGEN — exekveringsordningen, det regel 4
    //           skyddar. Writern emitterar listan rakt av.
    //   `nodes` bär Z-/TRÄDBYGGSORDNINGEN — INBYGGDA fönstermedlemmar
    //           FÖRST (fönstret adopterar dem i sin konstruktor, alltså
    //           före allt genererat), sedan adopt-ordningen, sist de
    //           odopterade i deklarationsordning.
    //
    // Att `statusBar` flyttas först bland NODERNA flyttar alltså inte dess
    // satser — annars vore omordningsbuggen tillbaka.
    std::vector<WidgetNode> nodes;
    std::vector<Stmt>       stmts;

    WidgetNode* find(const std::string& id)
    {
        for (auto& n : nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }
    const WidgetNode* find(const std::string& id) const
    {
        for (const auto& n : nodes)
            if (n.id == id)
                return &n;
        return nullptr;
    }

    // ------------------------------------------------------ läsfunktioner
    //
    // Titel och storlek är INTE lagrade fält (en lagrad kopia hade gett
    // modellen två sanningar om samma rad). De slås upp i den
    // SISTA relevanta `WindowCall`-satsen — "den som sätts sist gäller",
    // exakt kontraktet i app_window.h.

    // Sista fönsteranropet med det namnet, eller nullptr.
    const Stmt* lastWindowCall(const std::string& method) const;

    std::string title() const;          // avciterad LITERAL ur setTitle, annars tom
    int         clientWidth() const;    // ur setClientSize (800 utan rad)
    int         clientHeight() const;   // ur setClientSize (520 utan rad)

    // Adoptionssatsen för en nod, eller nullptr = ligger utanför trädet.
    const Stmt* adoptionOf(const std::string& id) const;
    bool        isAdopted(const std::string& id) const
    {
        return adoptionOf(id) != nullptr;
    }
    // Förälderns id; tom sträng = root() ELLER inte adopterad alls (kolla
    // `isAdopted` när skillnaden spelar roll).
    std::string parentOf(const std::string& id) const;

    // Fältsatsen för <id>.<fält>, eller nullptr.
    const Stmt* findProp(const std::string& id, const std::string& field) const;
    Stmt*       findProp(const std::string& id, const std::string& field);

    // ---------------------------------------------------- muteringarna
    //
    // Buildern REDIGERAR modellen, så listan muteras genom NAMNGIVNA
    // operationer — aldrig genom accessorer som returnerar temporärer.

    // Sätt/uppdatera ett fält: befintligt värde skrivs om PÅ PLATS (raden
    // behåller sin position i satslistan), nytt fält appendas sist.
    void setProp(const std::string& id, const std::string& field,
                 const std::string& value);
    void appendCall(const std::string& id, const std::string& method,
                    const std::string& args);
    void appendEvent(const std::string& id, const EventBind& e);
    void appendWindowEvent(const EventBind& e);
    // Adoptionen för en nod: uppdaterar befintlig sats PÅ PLATS (adopt-
    // ordningen ÄR z-ordningen och får inte hoppa), annars appendas den.
    void setAdoption(const std::string& id, const std::string& parent);
    // DUPLIKATPOLICY: flera anrop med samma namn är LAGLIGT i källan
    // ("den som sätts sist gäller"), så sättaren uppdaterar SISTA
    // förekomsten på plats och appendar bara när ingen finns. En
    // handskriven fil med två `setContentSize` round-trippar oförändrad.
    void setWindowCall(const std::string& method, const std::string& args);
    // READERNS väg: appendar ALLTID, så två anrop med samma namn i källan
    // förblir två satser (duplikatpolicyn ovan gäller SÄTTAREN, inte
    // läsningen — annars hade den andra raden tyst ätit den första).
    void appendWindowCall(const std::string& method, const std::string& args);
    WidgetNode& appendNode(const std::string& type, const std::string& id);
    // Tar bort noden OCH alla dess satser (fält, anrop, events, adoption).
    void removeNode(const std::string& id);
};

// ------------------------------------------------ inbyggda fönstermedlemmar
//
// Namn som grammatiken känner igen UTAN en medlemsdeklaration i regionen,
// eftersom `glaze::AppWindow` redan har dem. Tabellen är
// den enda sanningen om vilka de är; `builder_core` förblir ren std och
// känner bara NAMNET och TYPTEXTEN, aldrig `glaze::`-typen.
//
// Att den är en TABELL och inte ett specialfall för `statusBar` är
// poängen: nästa inbyggda fönsterwidget är en rad, inte en gren.
struct BuiltinMember {
    const char* id;     // "statusBar"
    const char* type;   // "glaze::StatusBar" — modellens typtext
};

// nullptr = inte en inbyggd medlem.
const BuiltinMember* builtinMember(const std::string& id);

// ------------------------------------------------------- NAMN-INVARIANTEN
//
// `Widget::name` ÄR objektets namn,
// speglat till runtime — inte ett separat fält. Modellen SÄGER det, men
// grammatiken tar fältrader, så en handredigerad fil kan bära
//
//     glaze::Button Button1;
//     Button1.name = "annat";
//
// och då återuppstår exakt buggen den kanoniska formen finns för att
// stänga: buildern skriver ankare mot `Button1` medan preview och byggd app
// bär `"annat"`. Invarianten är därför VALIDERAD och inte bara avsedd.
//
// BUILTINS (`statusBar`/`menuBar`) undantas HELT: de har ingen
// medlemsdeklaration, och en emitterad `name`-rad hade brutit regeln att
// writern aldrig ger dem en medlemsrad. Deras runtime-namn sätts av
// `glaze::AppWindow`s konstruktor i stället.
enum class NameState {
    Kanoniskt,   // exakt en name-rad, värdet == nodnamnet
    Saknas,      // ingen name-rad — MIGRERAS
    Avviker,     // värde ≠ nodnamn, eller FLERA name-rader — FAIL-SAFE
};

NameState nameState(const WindowModel& m, const WidgetNode& n);

// Migrera modellen till kanonisk form: lägg en `name`-rad på varje vanlig
// nod som saknar en. AVVIKANDE nod ⇒ INGET skrivs och `fel` fylls med en
// läsbar diagnos (fail-safe: regel 4 säger vägra i stället för att gissa;
// användaren rättar raden för hand eller döper om noden).
//
// Migreringen är en MEDVETEN ENGÅNGSDIFF på orörda filer — tvärtemot
// tabIndex-prejudikatet, och skillnaden är att namnet MÅSTE finnas i den
// byggda appen för att ankare och `find<>` ska fungera. Att inte emittera
// vore det tysta WYSIWYG-brottet auto-namnet ska stänga. Brytande, och
// därmed gratis före v1.00.
bool migrateNames(WindowModel& m, std::string& fel);

// Döp om en nod: medlemsnamnet OCH allt som pekar på det — satsernas
// `target`, adoptionsföräldrarna, syskonankarnas `to`-strängar och den
// kanoniska `name`-radens LITERALVÄRDE. Missas det sista produceras
// `BtnStatus.name = "Button1";`, alltså exakt den tvetydighet
// valideringen ovan fail-safe:ar på — införd av vår egen operation.
//
// false = avvisat, `fel` säger varför: ogiltig C++-identifierare,
// C++-nyckelord, namnet upptaget, eller ett INBYGGT namn
// (`statusBar`/`menuBar`). Buildern VARNAR separat om användarkod
// refererar det gamla namnet — men rättar aldrig `.cpp` (regel 5).
bool renameNode(WindowModel& m, const std::string& från,
                const std::string& till, std::string& fel);

// Är strängen en giltig C++-identifierare som INTE är ett nyckelord?
// Exponerad för builderns egenskapspanel, som validerar medan man skriver.
bool validNodeName(const std::string& s);

// ---------------------------------------------------------- värde-hjälpare
// Rå C++-uttryckstext ↔ typade värden för fälten buildern själv redigerar.
// Allt annat lämnas orört (opaka rader).

// STRÄNGLITERALEN — en STRIKT delmängd av C++, symmetrisk åt båda hållen
// KONTRAKTET, som grinden mäter:
//
//   Avkodade escapes:  \"  \\  \n  \t  \r      — och INGA andra.
//   Kodade tecken:     "   \   LF  TAB CR      — allt annat rått.
//   AVVISAS:  okänd escape (`\x41`, oktalt, `\u`, `\b`, `\f`, `\0`, `\å`),
//             escape utan efterföljande tecken (avslutande backslash i
//             `"abc\"` — slutcitatet är då ESCAPAT och raden alltså ingen
//             komplett literal), oescapat `"` inuti (`"a" "b"`) och RÅA
//             KONTROLLBYTES (< 0x20) inuti literalen.
//
// De två sista raderna är vad som gör round-trippen BYTE-exakt:
// `cppString(parseCppString(x)) == x` gäller ordagrant på hela den
// accepterade mängden, och `parseCppString(cppString(s)) == s` på hela den
// kodningsbara. Vanlig UTF-8 (≥ 0x80) passerar OFÖRÄNDRAD — svensk text är
// normalfallet, inte ett undantag.
//
// Varför AVVISA i stället för att avkoda `\x41` till `A`: emissionen hade
// då skrivit `"A"` tillbaka, alltså en TYST omskrivning av användarens rad.
// Avvisad betyder OPAK rad (regel 4), aldrig förvrängd data.
bool parseCppString(const std::string& v, std::string& out);

// Hej "du" → "\"Hej \\\"du\\\"\"" (citerad + escapad literal).
// FELKANAL med flit: `raw` kan bära tecken delmängden inte kan koda (en rå
// LF hade emitterats som en radbrytning MITT I literalen — kod som
// inte kompilerar). false ⇒ `ut` är orörd och anroparen måste avbryta
// operationen.
bool cppString(const std::string& raw, std::string& ut);

// '{"a", "b"}' → {"a", "b"} — LISTLITERALEN, förlustfritt.
// Behövs för `items`: raden lagras som RÅ C++-text, och `setProp`:s
// semikolonform kan inte representera ett item med semikolon i, ett tomt
// element eller en escape. Strikt delmängd — varje element måste vara EN
// strängliteral; `{}` är en TOM lista och inte ett fel; false = raden är
// inte en listliteral och ska förbli OPAK (aldrig degraderad till en
// skräplista — att tyst visa fel data är värre än att inte visa något).
bool parseStringList(const std::string& v, std::vector<std::string>& out);

// '{0, 2}' → {0, 2} — TRÄDETS INDEXSTIG (`glaze::TreePath`).
// Samma familj som `parseStringList`: exakt ett yttre `{…}`, varje element
// ett ICKE-negativt DECIMALT heltal, `{}` är roten (en TOM stig och inte
// ett fel), och en avslutande komma tillåts — den är laglig C++. `-1`,
// `0x2`, `1u`, `2.0` och uttryck avvisas, och avvisad betyder OPAK rad
// (regel 4): previewn får aldrig tolka en stig annorlunda än C++ gör.
bool parseIndexPath(const std::string& v, std::vector<int>& out);
// Kanonisk emission: {0, 2} — och {} för roten.
std::string indexPathText(const std::vector<int>& p);

// ARGUMENTSPLITTAREN — STRUKTURELL. Delar en
// argumentlista på KOMMA vid djup 0, utanför strängliteraler.
//
// Djupet bärs av en STACK över `{}` och `()` — inte av två oberoende
// räknare, som hade accepterat felordnade former som `({)}` eftersom
// totalsummorna ändå blir noll. `false` = obalanserad, felordnad eller
// oavslutad text ⇒ raden är ingen giltig argumentlista och ska förbli
// OPAK (regel 4), aldrig splittas sönder.
//
// Varför den bor i `builder_core` och inte hos `apply_props`, där
// splittningen användes: `parseMenuPath` nedan är också en
// GRAMMATIKfunktion och måste dela med EXAKT samma regler. Två
// grammatiktolkningar som kan glida isär är precis vad den delade splittern
// undviker: ett nästlat argument som `{glaze::tr(1), glaze::tr(2)}` får
// aldrig splittas sönder av en splitter som bryter på VARJE komma utanför
// sträng eller räknar `{}` men aldrig `()`. Den rör varenda metodanrop och
// är därför grindad för sig.
//
// SISTA ELEMENTET: läggs till om det bär något ELLER om det finns
// tidigare element — `f(a, )` har två argument, `f()` har noll, och
// `f( )` har noll (whitespace är inte ett argument).
bool splitArgs(const std::string& s, std::vector<std::string>& out);

// "12" / "12.5" → tal. false om inte numeriskt.
bool parseNum(const std::string& v, float& out);
// 12 → "12", 12.5 → "12.5" (heltal utan decimaler — kanonisk emission).
std::string numText(float v);

// "800, 452" → två heltal. STRIKT: exakt två, inget släpande skräp —
// fönsteranropens argumentform (setClientSize/setContentSize/min/max).
bool parseTwoInts(const std::string& v, int& a, int& b);

// "{false, false, true, true}" → fyra bools. false om fel form.
bool parseAnchorsText(const std::string& v, bool out[4]);
// Kanonisk emission: "{true, false, false, true}".
std::string anchorsText(const bool a[4]);

// SYSKONANKARET — `{"knapp7", 8}` → id + gap.
// Fältet är en STRUCT-LITERAL och därför en vanlig fältrad i grammatiken:
// ingen ny satsform behövdes, writerns Prop-gren emitterar den redan
// generiskt och readerns `balancedValue` släpper igenom klammern.
//
// STRIKT delmängd som `parseImgCall`/`parseAnchorsText`, men täcker alla
// tre formerna som faktiskt KOMPILERAR: `{"id", tal}` (den kanoniska),
// `{"id"}` (gap tar sitt default 0) och `{}` (inget ankare). Allt annat är
// false ⇒ raden förblir OPAK (regel 4) — aldrig en gissning, och aldrig
// ett halvsatt ankare. Att de två korta formerna avkodas i stället för att
// bli opaka är hård regel 8: en rad som kompilerar och som previewn inte
// utför är ett WYSIWYG-brott.
//
// Formen är id-STRÄNG och inte en pekare (`&knapp7`) med flit: previewn
// har bara widgeten i handen (`bld::applyProp`) och kan aldrig förvandla
// ett namn till en pekare in i preview-trädet, så pekarformen hade blivit
// en opak rad och previewn visat fel geometri — precis det hård regel 8
// kallar ett REGELBROTT. `Radio`s `group` är prejudikatet.
bool parseSiblingAnchorText(const std::string& v, std::string& to,
                            float& gap);
// Kanonisk emission: {"knapp7", 8}. `to` är ett NODNAMN och därmed alltid
// kodningsbart (strängliteralens kontrakt ovan); TOM sträng = kunde inte emitteras, och
// anroparen ska då vägra operationen i stället för att skriva en halv rad.
std::string siblingAnchorText(const std::string& to, float gap);

// Är fältet ett av de fyra syskonankarna? EN sanning — verktygsvägen
// frågar den på fyra ställen (applyProp, egenskapspanelen, canvasens
// kantlås och kopplingsritningen).
bool isSiblingAnchorField(const std::string& field);

// "glaze::Dock::Top" → "top" (gemener, sista segmentet). false annars.
bool parseEnumTail(const std::string& v, std::string& outLower);

// 'glaze::img("toolbar", "ok")' → lista + nyckel. false om fel form —
// bildfältets värdegrammatik. Samma familj som
// parseEnumTail: strikt delmängd, aldrig generell C++-parsning.
bool parseImgCall(const std::string& v, std::string& list,
                  std::string& key);
// Kanonisk emission: glaze::img("toolbar", "ok"). Samma invariant som
// `siblingAnchorText`: lista och nyckel, aldrig fri text — TOM sträng =
// kunde inte emitteras.
std::string imgCallText(const std::string& list, const std::string& key);

// ---------------------------------------------------- ÖVERSÄTTNINGEN
//
// `glaze::tr(1042)` → id:t. STRIKT delmängd efter `parseImgCall`s
// prejudikat: exakt `glaze::tr(` + ETT decimalt heltal + `)`, inget
// annat. `glaze::tr(kNågot)`, `tr(1042)` utan namnrymd, `0x412` och
// `1042u` avvisas — och avvisad betyder OPAK rad (regel 4), aldrig en
// gissning. `id == 0` avvisas också: 0 betyder "ingen översättning".
//
// FORMEN ÄR NUMERISK och kan inte vara något annat: `builder_core` är
// ren std utan reflektion och kan aldrig lösa `glaze::str::Ok` till ett
// tal. Konstantnamnen bor i C++-koden; filen bär numret.
bool parseTrCall(const std::string& v, std::uint32_t& id);
// Kanonisk emission: glaze::tr(1042).
std::string trCallText(std::uint32_t id);

// Vad ÄR det här textvärdet? Klassificeringen hör till GRAMMATIKEN och
// inte till editorn — därför bor den här och grindas headless, medan
// GlazeBuilder-exet bara konsumerar svaret (panelen får aldrig
// koda om ett tr-id till strängliteralen `"glaze::tr(1042)"`, vilket är
// DATAFÖRLUST och inte en saknad funktion).
//
// `TrExpr` är ETT ensamt `glaze::tr(…)`-anrop vars
// argument inte är ett tal — `glaze::tr(app::str::Hej)`. Raden KOMPILERAR,
// men verktygen kan inte lösa konstanten (filen bär numret), så den är lika
// opak som `Other` — klassen finns för att loggen kan säga "skriv numret" och
// för att panelen ska skydda den som ett tr-id. Ett textfält visar ALDRIG
// `TrExpr` eller `Other` som text.
enum class TextValueKind {
    Literal,   // "…" — `text` bär den avkodade strängen
    TrId,      // glaze::tr(N) — `id` bär numret
    TrExpr,    // glaze::tr(<konstant/uttryck>) ⇒ opakt, rör inte
    Other      // något annat (uttryck, okänt) ⇒ opakt, rör inte
};
TextValueKind classifyTextValue(const std::string& v, std::string& text,
                                std::uint32_t& id);

// ------------------------------------------------ MENYSTIGEN
//
// Ett SEGMENT i en menystig: antingen en strängliteral eller ett tr-id.
struct PathSeg {
    bool          isId = false;
    std::uint32_t id   = 0;   // giltigt när isId
    std::string   text;       // AVKODAD literal när !isId
};

// `{"Utseende", glaze::tr(1042)}` → segmenten. STRIKT som `parseImgCall`:
//
//   * exakt ETT yttre `{…}` ELLER en ensam strängliteral/`glaze::tr(N)`
//     (ett enkelsegment behöver inga klamrar — `addMenu("Arkiv")`),
//   * segmenten delas med `splitArgs` (samma grammatiktolkning som allt
//     annat),
//   * varje segment är ANTINGEN en C++-strängliteral ELLER strikt
//     `glaze::tr(<decimalt heltal>)`,
//   * MINST ett segment,
//   * `/` i ett LITERALsegment AVVISAS.
//
// Snedstreckregeln är migreringens säkerhetsnät: en gammal stig som EN
// sträng splittad på `/` (`"Utseende/Läge"`) hade utan regeln tyst blivit
// ETT segment med snedstreck i namnet, aldrig matchat någon meny och bara
// gett ett diffust "okänd stig" — en tyst regression i stället för en läsbar
// migrering.
//
// false ⇒ OPAK rad. Att avkoda stigen med `argStr`, som
// bara accepterar strängliteraler, hade gjort en klammerform till en
// PÅHITTAD TOM stig medan grenen ändå returnerade framgång.
bool parseMenuPath(const std::string& v, std::vector<PathSeg>& out);
// Kanonisk emission: {"Utseende", glaze::tr(1042)} — och för ETT segment
// den nakna formen ("Arkiv" resp. glaze::tr(1042)), som är den `addMenu`
// och `addItem` skriver i dag. TOM sträng = kunde inte emitteras.
std::string menuPathText(const std::vector<PathSeg>& segs);

// ------------------------------------------------- fält ↔ setter
//
// Widgetarnas TILLSTÅNDSFÄLT är privata med
// setter + getter (settern är den som INVALIDERAR). `.glazeui` är riktig
// C++ som KOMPILERAS, så en fältrad `okBtn.text = "…";` går inte att
// emittera — den formen finns bara för LAYOUT-fälten, som med
// flit är råa (`x`/`y`/`width`/`height`/`right`/`bottom`/`dock`/
// `anchors`/`tabIndex`/`id`).
//
// KONTRAKTET, och skälet det ser ut så:
//   * WRITERN emitterar setter-fälten i CALL-formen
//     (`okBtn.setText("Tryck här");`) — regel 4-säker per konstruktion,
//     eftersom en ÄLDRE builder läser den som ett okänt metodanrop och
//     bevarar den opakt i stället för att gå i fail-safe.
//   * READERN normaliserar TILLBAKA till en `Prop`, så `kFieldMeta`,
//     `applyProp` och egenskapspanelen är helt oförändrade. Modellen ser
//     alltså samma fältvokabulär som en fältrad.
//   * Filer med fältrader migreras GRATIS: en fältrad läses som `Prop` och
//     skrivs ut i setterformen vid första spara.
//
// TYP-VILLKORET är bärande: omskrivningen sker BARA för `glaze::`-typer.
// En egen widget med ett publikt `text`-fält ska fortsätta round-trippa
// som en fältrad — annars hade verktyget skrivit en rad som inte
// kompilerar i användarens projekt.
//
// Namnregeln är MEKANISK: `set` + versal första bokstav. Tabellen bär
// därför FÄLTNAMNET och inte par av namn.
//
// TABELLEN ÄR TYPMEDVETEN, eftersom fälten är per-typ: med en namn-GLOBAL
// tabell normaliserades `lista.setChecked(1, true);` på en ListBox till
// `Prop{checked, "1, true"}` — Checkbox/Radios bool-fält, med en
// ListBox-signatur i värdet. Raden round-trippade av en slump och utfördes
// ALDRIG i previewn.
//
// `cppType == nullptr` betyder "alla glaze-typer" (samma konvention som
// builderns `kFieldMeta`), och `subtypes == true` betyder "typen OCH dess
// ättlingar" — `Panel::clip`/`border` gäller varje Panel-ättling, och utan
// flaggan hade de krävt en duplicerad rad per ättling. Basens egna fält
// (`allowDrag`/`allowDrop`) står som `{"glaze::Widget", …, true}`
// och INTE som `nullptr`: en okänd ANVÄNDARtyp ska behålla sin anropsform,
// och `isOrDerivesFrom` svarar nej för en typ tabellen inte känner.
struct SetterFieldSpec {
    const char* cppType;   // nullptr = alla glaze::-typer
    const char* field;     // "text", "clip", …
    bool        subtypes;  // true = cppType och dess ättlingar
};

// Produktionens tabell, ITERERBAR. Den ligger annars i ett anonymt
// namespace, och utan den här ytan hade `preview_gate` fått skriva en
// ANDRA kopia av matrisen för att alls kunna mäta FULLSTÄNDIGHET — och två
// kopior som ingen jämför är precis den risk grinden finns för.
// (Grindens ORAKEL är en oberoende matris;
// det är två skilda saker och båda behövs.)
std::span<const SetterFieldSpec> setterFieldSpecs();

// ARVET — en MANUELLT UNDERHÅLLEN tabell, och det står här i stället för
// ett löfte som inte håller: `builder_core` är ren std, ser bara
// typnamnet som en STRÄNG i `WidgetNode::type` och har ingen reflektion.
// En framtida `class TreeView : public Panel` känns alltså INTE igen
// automatiskt — det är `preview_gate`s register/palett-kontroll som gör en
// utebliven klassning synlig i stället för tyst.
//
// Kedjorna är widget-headernas egna: Panel → Bar → MenuBar/TopBar/
// StatusBar, Panel → PropertyGrid/ListBox, allt annat direkt på Widget —
// och en ANDRA rot, `glaze::Component`, med Timer
// och TrayIcon. Tabellen klassar alltså både widgets och komponenter.
bool isKnownType(const std::string& cppType);   // klassad i tabellen?
// "glaze::ListBox" ärver "glaze::Panel" ⇒ true. Reflexiv: typen är sig själv.
bool isOrDerivesFrom(const std::string& cppType, const std::string& base);

// "glaze::Label" + "wrap" → true.
bool isSetterField(const std::string& cppType, const std::string& field);
std::string setterName(const std::string& field);  // "text" → "setText"
// "glaze::Label" + "setText" → "text" (false = inte en känd setter FÖR DEN
// TYPEN — `lista.setChecked(…)` på en ListBox svarar false och blir en Call).
bool setterField(const std::string& cppType, const std::string& method,
                 std::string& field);
// NAMN-GENERÖS overload, och den finns för EXAKT ETT anropsställe:
// previewns setter-fallback (`bld::applyCall`), där en ANVÄNDARWIDGET som
// ärver en glaze-typ har typnamnet `MyPanel` i modellen och alltså faller
// utanför matrisen. Där är generositeten ofarlig — det är preview-vägen och
// inte emissionen, och widgetens egen `setProp` är den som avgör till slut.
// Använd ALDRIG den här för reader/writer: det är den namn-globala
// felklassen ovan.
bool setterField(const std::string& method, std::string& field);
// "glaze::Button" → true. Formen är hela villkoret ovan.
bool isGlazeType(const std::string& cppType);

// ------------------------------------------------------------ designtaggen
//
// En KOMMENTAR i *.glazeui och aldrig en sats — därför är "används EJ vid
// kompilering och körning" sant per KONSTRUKTION (kompilatorn ser en
// kommentar), och en äldre GlazeBuilder hoppar bara över raden i stället
// för att tvingas i fail-safe av en okänd sats.
//
//     "// glazeui-design:" WS ( nyckel "=" värde WS* )*
//     nyckel  = [a-z]+
//     värde   = alla tecken UTOM blanksteg
//
// Kända nycklar: ui, theme, style, lang (avkodas i design_theme.h). Okänd
// nyckel ignoreras vid avkodning men SKRIVS TILLBAKA (hård regel 4).
inline constexpr const char* kDesignPrefix = "// glazeui-design:";

struct DesignPair {
    std::string key;
    std::string value;
};

// Splittar designradens TEXT (det som står EFTER prefixet) i nyckel/värde.
// **Värden får inte innehålla blanksteg** — raden splittas på whitespace,
// så `theme=slate( Teal )` är tre fält och inte ett värde. Det är en tyst
// fälla om det inte står skrivet, därför står det här.
// false = raden är trasig (ogiltig nyckel, saknat '=', tomt värde eller
// DUBBLERAD nyckel) ⇒ ingen axel sätts alls; raden bevaras ändå ordagrant.
bool parseDesignLine(const std::string& text, std::vector<DesignPair>& out);

} // namespace bld
