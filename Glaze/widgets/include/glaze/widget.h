#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <typeinfo>   // registret bär widgetens klass (GLAZE_REGISTER_WIDGET)
#include <utility>
#include <vector>

#include <gfx/color.h>
#include <gfx/events.h>
#include <gfx/geometry.h>
#include <gfx/renderer.h>
#include <gfx/text.h>
#include <gfx/window.h>   // gfx::PointerShape — `Widget::cursorAt`

#include "glaze/canvas/geometry.h"       // Point — MouseArgs::local (ren std)
#include "glaze/canvas/image_source.h"   // ImageSource — widgetarnas bildvärde
#include "glaze/input.h"   // InputDevice — rullningens riktning
                                         // (dragbilden, knapparna, raderna)
#include "glaze/language.h"   // TrText — widgetarnas textfält
#include "glaze/style.h"
#include "glaze/theme.h"

// Widget-basen + kontraktet alla widgets delar: layout (Dock/Anchors),
// ritkontexten (Ctx), event-formen (sender, args) och basklassen Widget. De inbyggda
// widgetarna bor i varsin egen fil (label.h, button.h, ...) — varje fil är
// tänkt som en kopierbar mall för egna komponenter (visionens pelare 4).
//
// ETT mönster (hård regel 3): varje widget ritar SIG SJÄLV. draw()/
// contentSize() bor i widgetens egen cpp och väljer form med
// `switch (c.ui)` (`default:` = Slate-vägen); ritningen går via
// c.r-primitiverna och de DELADE idiomen i glaze/draw.h. En EGEN widget
// utanför ramverket ser exakt likadan ut — kopiera närmaste widget-fil.

namespace glaze {

class ImageCache;   // per-fönster texturcache (intern, src/image_cache.h)
class FontSet;      // per-fönster textfonter (intern, src/font_set.h)
class CanvasResources;   // ritytans GPU-resurser per fönster (intern,
                         // src/canvas/resources.h)

// ---------------------------------------------------------------- layout

// Dock — hur en CONTAINER placerar ett barn (WinForms-semantiken):
// dockade barn skär av remsor ur förälderns childArea i
// BARNORDNING (= adopt-ordning = z-ordning); Fill tar resten (utan att
// förbruka den — flera Fill delar ytan, z-ordningen avgör).
//
// **ICKE-DOCKADE BARN SKÄRS OCKSÅ.** Ett icke-dockat barn bor i "resten",
// precis som `Dock::Fill`, och ytan är förälderns childArea minus ALLA
// SYNLIGA dockade syskons remsor (`Widget::carveSiblingDocks`). Därmed tar en
// Top-dockad menyrad eller Bottom-dockad statusrad plats både i fönstrets
// storleksräkning och i syskonens layout.
//
// Tre preciseringar som är skrivna beslut:
//   - ALLA synliga dockade syskon skär, inte bara de TIDIGARE. Att låta
//     adopt-ordningen styra ett icke-dockat barns yta hade gjort layouten
//     ordningskänslig utan vinst. Ordningsberoendet MELLAN dockade barn
//     står orört (det ÄR WinForms-semantiken).
//   - DOLDA remsor skär ingenting — samma regel som `chromeInsetH`, så
//     `statusBar.show()` aldrig kan bli osynkad med storleksräkningen.
//   - `Fill` förbrukar ingen tjocklek och skär därför inget. Ett
//     icke-dockat barn DELAR alltså yta med ett Fill-syskon; z-ordningen
//     avgör vad som syns.
//
// Stateless: remsan räknas fram i resolve() via syskonvandring — ingen
// cachad layout, ingen invalidering.
enum class Dock { None, Left, Top, Right, Bottom, Fill };

// Anchors — beteendet vid resize (fråga 8): vilka kanter av förälderns
// childArea widgeten sitter fast i. Default left+top = klassiska
// pos-beteendet (x/y från övre vänstra hörnet). Dubbla ankare per axel =
// sträckning (bredden följer ytan); INGET ankare på en axel = centrerad +
// offset. Offsetfälten: x/y mäter från vänster/topp, right/bottom från
// höger/botten (alla logiska px).
//
// SYSKONANKARET (SiblingAnchor nedan) ligger BREDVID, per kant, och har
// FÖRETRÄDE på den kant där det är satt — förälderankaret gäller de kanter
// som inte är bundna till ett syskon.
struct Anchors {
    bool left   = true;
    bool top    = true;
    bool right  = false;
    bool bottom = false;
};

// SYSKONANKARE — placera en kant mot en ANNAN
// WIDGET i stället för mot förälderns kant. Ett spikat `x = 210` uttrycker
// en POSITION, och en position är bara sann i EN Style: Slates knapphöjd
// är 26.7 px och Minimalistics 18, så vid stilbyte stämmer talet inte och
// felet KASKADERAR åt höger. Syskonankaret uttrycker RELATIONEN — "8 px
// efter knapp7" — och den är sann i varje Style, eftersom positionen
// härleds ur grannens FAKTISKA, stil-mätta rect vid varje `resolve()`.
//
// SEMANTIKEN är KANT-semantik och riktningen följer av VILKET FÄLT man
// sätter (implicit "närmaste kant" är cirkulärt: var jag hamnar avgör
// vilken kant som är närmast, men närmaste kant avgör var jag hamnar):
//
//     b.anchorLeft   = {"a", 8}   b.vänsterkant = a.högerkant + 8   (b höger om a)
//     b.anchorRight  = {"c", 8}   b.högerkant   = c.vänsterkant − 8 (b vänster om c)
//     b.anchorTop    = {"a", 8}   b.överkant    = a.underkant  + 8
//     b.anchorBottom = {"c", 8}   b.underkant   = c.överkant   − 8
//
// `to` är syskonets **`name`**, inte en pekare — det är den enda formen
// som kan uttryckas BÅDE i kod och i `.glazeui` (hård regel 8), och
// `Radio`s `group` är prejudikatet. HANDSKRIVEN KOD MÅSTE SÄTTA `name` PÅ
// MÅLET; GlazeBuilder emitterar name-raden själv (den står på VARJE nod,
// eftersom namnet ÄR medlemsnamnet).
//
// UPPSLAGET STANNAR VID MIG SJÄLV (dock-carvingens mönster): bara TIDIGARE
// syskon i barnordningen kan träffas. Därmed är cykler inte något som
// upptäcks utan något som är OUTTRYCKBART — en framåtreferens, en
// självreferens och ett okänt namn ger alla samma utfall: hittades inte ⇒
// kanten faller TYST tillbaka på förälderankaret/x/y. `resolve()` loggar
// aldrig (den körs 6+ gånger per frame); valideringen sker där texten
// kommer IN (`setProp`, builderns egenskapspanel).
//
// TVÅ konsekvenser som är skrivna beslut och inte förbiseenden:
//   - Inne i en container som PLACERAR sina barn själv (`Bar`) IGNORERAS
//     syskonankaret, precis som dock och anchors — gren 0 i `resolve()`
//     körs före allt.
//   - Ett DOLT mål räknas ändå (dess rect finns), så följaren står kvar
//     där grannen skulle ha varit och lämnar ett hål. `resolve()` kollar
//     inte `visible_` på sig självt heller, och `childrenExtent` räknar
//     hålet likadant.
//   - `name` förutsätts UNIKT bland syskonen. Bär två syskon samma namn binder
//     ankaret till det FÖRSTA, och en ändrad adopt-ordning flyttar då
//     widgeten utan att ankarraden rörts. GlazeBuilder genererar unika
//     medlemsnamn, så det här träffar bara handskriven kod — och en
//     unikhetsvakt i `adopt` vore ett nytt kontrakt för HELA trädet, inte
//     en ankarfråga. Därför skrivet här i stället för kodat.
struct SiblingAnchor {
    std::string to;         // syskonets name; "" = inget ankare (förvalet)
    float       gap = 0;    // logiska px, KANT till KANT
};


// Ritkontext som skickas till widgets och frame()-hooken.
// Alla widget-mått är logiska px och multipliceras med scale (DPI).
// theme är den AKTIVA temavarianten, resolvad per repaint av AppWindow
// (System-läget följer OS:et) — all färg hämtas ur c.theme->colors,
// aldrig hårdkodat. ui är den AKTIVA RITNINGEN och style de AKTIVA MÅTTEN
// (samma rytm, app-globala, tre skilda axlar) — varje widget switchar på
// `ui` i sin egen draw() och läser sina tal ur `style`. Bygg aldrig en Ctx
// för hand: hämta den via AppWindow::makeCtx() så allt alltid är satt.
struct Ctx {
    gfx::Renderer*        r;
    gfx::Font*            font;

    // ROLLFONTEN. Fonten är en FÖNSTER-resurs, så den kan
    // inte bo i Style (som är en värdetabell) och inte i widgeten (som
    // inte äger renderern) — den måste komma in via Ctx.
    //
    // Mängden fonter ett fönster har är SLUTEN: `Style` räknar upp
    // storlekarna, och ingen fri multiplikator finns — fontskapande är dyrt
    // i alla tre backends, och det är exakt den kostnaden en fri storlek
    // hade gjort obegränsad. ROLLFONTEN förskapas EAGER i `reloadFont()`
    // (varje fönster med en rubrik behöver den) och laddas om med brödtexten
    // vid stil-/UI-/skalbyte; TEXTFONTERNA nedan (`textFonts`) är samma
    // slutna mängd men LATA, för bara en vy med
    // formaterad text frågar efter dem.
    //
    // FÅR vara null (fönster vars renderer inte finns än) — läsaren
    // faller tillbaka på `font`, precis som ritkoden gör med `images`.
    gfx::Font*            headingFont;

    float                 scale;
    gfx::Rectf            area;    // fönstrets innehållsyta (under titelraden)
    const Theme::Variant* theme;
    UI                    ui;      // aktiv RITNING — widgetens draw() switchar
    const Style*          style;   // aktiva MÅTTEN (logiska px) — c.style->x
    ImageCache*           images;  // per-FÖNSTER texturcache (assets) —
                                   // FÅR vara null, ritkoden kollar alltid

    // SKÄRMENS TÄTHET: fönstrets FYSISKA px per
    // millimeter på skärmen det står på — `AppWindow::display().pxPerMm`,
    // alltså med appens kalibrering. Det är ritytans mm-enhet (linjebredd,
    // text och streck i mm); mm i logiska px är `mm * pxPerMm / scale`.
    // Default är det antagna 96 dpi, för en Ctx byggd utanför ett fönster.
    float                 pxPerMm = gfx::kAssumedPxPerMm;

    // RITYTANS RESURSER: fönstrets GPU-buffertar för
    // lagrens chunkar, SDF-atlaserna, ramperna och rutnätet. FÅR vara null
    // (en Ctx byggd utanför ett fönster) — ritytan ritar då bara sin yta.
    CanvasResources*      canvas = nullptr;

    // TEXTFONTERNA: fönstrets fonter för
    // FORMATERAD text — brödtexten, underrubriken och rubriken i UI och Mono,
    // vanliga, feta, kursiva och båda (intern `src/font_set.h`). En SLUTEN
    // mängd, laddad LAT per variant när den frågas, också mitt i en bildruta
    // (ritytans `SdfAtlas`-prejudikat); en ny storlek laddar om. FÅR vara
    // null (en Ctx byggd utanför ett fönster) — läsaren faller då tillbaka
    // på `font`/`headingFont`, och fet och kursiv ritas i vanlig stil.
    // Läggs FÖRE `box`: Ctx brace-initieras i `makeCtx` med `box` sist.
    FontSet*              textFonts = nullptr;

    // Widgetens EGEN rect i fysiska px, satt av ritvandringen strax före
    // draw(). Draw-anropen utgår från den: en widget ritar i sitt EGET rum
    // från (0,0) och behöver aldrig veta var den sitter — därför står det
    // varken resolve() eller * c.scale i ritkod.
    //
    // OBS: giltig BARA under draw()/drawAfterChildren(). contentSize()
    // körs INNE i resolve() och får därför aldrig läsa box (cirkulärt);
    // beteende-krokar (mouseDown m.fl.) använder resolve().
    gfx::Rectf box{};

    // Widgetens yta i LOGISKA px med origo i eget hörn — argumentet till
    // nästan varje Draw-anrop.
    gfx::Rectf bounds() const
    {
        return gfx::Rectf{0.0f, 0.0f, box.w / scale, box.h / scale};
    }
};

// ---------------------------------------------------------------- events

// Widget-eventens form följer C#:s (sender, args)-mönster:
//     onClick(Widget* sender, ClickArgs& e)
// sender = widgeten som avfyrade eventet (samma funktion kan betjäna
// flera widgets via sender->name). e = typad args-struct per event-typ.
// Värden läses ur sender, inte ur e (C#-troget: onChange på en checkbox
// läser checkbox.checked) — args bär bara det som inte finns där.
// Basen är tom med flit: framtida fält (e.fromUser, e.handled) läggs i
// den utan att en enda handler behöver skrivas om — det är hela poängen
// med mönstret. e skickas som icke-const & för att hålla den vägen öppen.
struct EventArgs {};
struct ClickArgs  : EventArgs {};
struct HoverArgs  : EventArgs { bool entered = false; };  // in (true) / ut
struct ChangeArgs : EventArgs {};

// AKTIVERINGEN — användaren "öppnade" den aktuella
// posten: dubbelklick på den eller Enter (WinForms `ItemActivate`, wx
// `EVT_LIST_ITEM_ACTIVATED`, Qt `activated`). Skild från `onChange` med
// flit: att VÄLJA en fil och att ÖPPNA den är olika handlingar, och en
// filväljare behöver båda. VILKEN post läses ur sender (regel 6) —
// `ListView::currentRow()`. Bor HÄR och inte i listvyns header, för
// aktiveringen är ett begrepp varje lista med poster delar (ett träd
// aktiverar en nod på samma sätt) och delade saker delar namn.
struct ActivateArgs : EventArgs {};

// FOKUSHÄNDELSERNA — `Widget::onFocus`/`onFocusLost`. Tom med flit:
// VILKEN widget som fick eller tappade fokus är `sender` (regel 6), och var
// fokus kom ifrån eller gick till är inte data än. Behövs det läggs ett fält
// till här, utan att en enda handler skrivs om.
struct FocusArgs : EventArgs {};

// VARFÖR FÖNSTRET STÄNGS (WinForms `CloseReason`)
// — `CloseArgs::reason`, satt när stängningen BEGÄRS:
//   UserClosing   fönstrets EGEN stängning: krysset, Alt+F4, OS:ets och
//                 kompositörens stängbegäran — och `close()`/`closeWithResult`
//                 ur appens kod, som WinForms `Form.Close()`: appen stänger för
//                 användarens räkning. En app som vill skilja sin "Avsluta"
//                 från krysset sätter en egen flagga.
//   AppQuit       appavslutet (`gfx::EventType::Quit`): Cmd+Q och appmenyns
//                 Avsluta på macOS, `WM_QUIT` på Windows. Ägarkedjornas RÖTTER
//                 får det; kommer det medan fönstrets egen stängning väntar i
//                 samma varv vinner det (ett appavslut är starkare).
//   OwnerClosing  kaskaden: ägaren — eller någon längre upp — stängs, och
//                 fönstret frågas som barn. Oavsett VARFÖR ägaren stängs.
// En app som stänger till systemfältet vetar `UserClosing` och släpper
// igenom `AppQuit` — annars hade Cmd+Q aldrig avslutat den.
enum class CloseReason : std::uint8_t { UserClosing, AppQuit, OwnerClosing };

// Args till fönstrets onClose-event (livscykel, fråga 15): sätt
// cancel = true för att STOPPA stängningen — WinForms FormClosing.
// cancel är event-UTDATA (svaret på "får jag stänga?"), inte
// widget-tillstånd, så regel 6:s "läs värden ur sender" hålls.
// Ett veto stoppar hela stängningskaskaden (barnen frågas före ägaren).
// `reason` säger varför (se `CloseReason`) — event-DATA, sist (regel 6).
struct CloseArgs : EventArgs {
    bool        cancel = false;
    CloseReason reason = CloseReason::UserClosing;
};

// Resultatet ur en modal dialog (WinForms-pariteten — växer vid behov).
// Modaliteten (vilka fönster som får input) och returvärdet är två skilda
// saker: filtret i App är modaliteten, den nästlade pumpen är returvärdet.
// `Ignore` lades SIST — `AbortRetryIgnore` kan annars
// inte svara. Sist och inte i "logisk" ordning: värdena är inte
// serialiserade någonstans, men en insättning mitt i listan flyttar tyst
// varje efterföljande värde, och det är en fälla ingen behöver.
// Bor HÄR och inte i app_window.h: knappen bär ett
// eget resultat (`Button::setDialogResult`), och button.h får inte inkludera
// app_window.h.
enum class DialogResult { None, OK, Cancel, Yes, No, Abort, Retry, Ignore };

// Args till mouseDownOutside-kroken (grab-stacken): var klicket landade.
// Ny event-data läggs som fält HÄR — aldrig som nya parametrar (regel 6).
struct MouseDownOutsideArgs : EventArgs {
    float x = 0.0f;   // fysiska px, fönsterkoordinater
    float y = 0.0f;
};

class Widget;

// Args till onContextMenu — HÖGERklicket. Positionen är
// fysiska px i FÖNSTERkoordinater, alltså exakt det `Menu::popupAt` vill
// ha; ingen omräkning behövs mellan eventet och menyn.
//
// `source` är widgeten UNDER pekaren och kan vara **nullptr** (tryck på
// tom fönsteryta — `widgetAt` svarar nullptr där, inte roten). Den ligger
// i args och inte som `sender` med FLIT: routingen är en ROUTED LOOKUP och
// inte bubbling — första FÖRFADER med satt handler anropas, och regel 6
// säger att `sender` är widgeten som avfyrade eventet, alltså den
// förfadern. Att skicka den träffade widgeten som `sender` hade varit ett
// kontraktsbrott; den hör hemma här.
struct ContextMenuArgs : EventArgs {
    float   x = 0.0f;
    float   y = 0.0f;
    Widget* source = nullptr;
};

// Args till en widgets TANGENT-event — `Widget::onKeyDown`/`onKeyUp`, på
// VARJE widget (formen kom med ritytans `onKeyDown`
// och bor här: delade saker delar namn). `key`/`mods` är
// tangenten som den fokuserade widgeten får i sin `keyDown` — alltså den
// VIKTA (numpadens navigationsläge blir sin dedikerade tangent,
// `gfx::navigationKey`) — och `gfx::kMod*`-masken. I `onKeyUp` är `key` den
// tangent tryckets `onKeyDown` fick, också när släppet kommer i numpadens
// andra läge (Shift vänder NumLock på Windows mitt i ett tryck).
//   handled    event-UTDATA (`CloseArgs::cancel`s form), i båda: handlern TOG
//              tangenten. I `onKeyDown` stannar den där — ingen `keyDown`,
//              inga förfäder, acceleratorer eller Tab — och i `onKeyUp` får
//              widgeten ingen `keyUp`.
//   repeat     `onKeyDown`: tangenten HÖLLS redan före eventet — OS:ets
//              upprepning. Ett läge som slås på av ett tryck prövar den.
//   cancelled  `onKeyUp`: släppet kom aldrig hit — fönstret tappade fokus
//              eller doldes medan tangenten hölls, och det riktiga släppet
//              gick till ett annat fönster. Läget ska avslutas ändå; `mods`
//              är då 0.
//   text       `onKeyDown`: texten trycket
//              SKRIVER, UTF-8 — exakt det ett textfält får som tecken direkt
//              efter. Tom för modifierare, navigering, funktionstangenter,
//              styrtecken (Enter, Tab, Escape, Backspace), en död tangent
//              (dess tecken kommer med nästa tryck: ´ sedan e ger "é" på e)
//              och Ctrl/Alt/Cmd-kombinationer — men inte AltGr eller macOS
//              Option, som skriver. Upprepningen bär texten igen; `onKeyUp`
//              har ingen. `handled` sväljer inte tecknet. Inte IME,
//              Windows Alt+numpad-koden eller en headless-backend.
struct KeyArgs : EventArgs {
    gfx::Key      key       = gfx::Key::Unknown;
    std::uint32_t mods      = 0;
    bool          handled   = false;
    bool          repeat    = false;
    bool          cancelled = false;
    std::string   text;
};

// Args till APPENS mushändelser på varje widget — `Widget::onMouseDown`/
// `onMouseUp`/`onMouseCancel` (WinForms
// `MouseEventArgs`). Kontraktet — målet, ordningen och vad `handled` gör —
// står vid fälten i `Widget`.
//   button   knappen trycket gäller: Left, Middle eller Right
//   local    pekaren i LOGISKA px från widgetens övre vänstra hörn (`Draw`s
//            rymd och layoutfältens enhet). I `onMouseCancel` pekarens senast
//            kända läge, {0, 0} när den har lämnat fönstret.
//   mods     `gfx::kMod*`-masken (`AppWindow::modifierKeys()`)
//   handled  event-UTDATA (`KeyArgs::handled`s form): i `onMouseDown` TAR
//            appen trycket, i `onMouseUp` släppet; `onMouseCancel` läser den
//            inte.
// POLYMORF med flit (den virtuella destruktorn): ritytans `CanvasMouseArgs`
// ÄRVER och bär världen och strålen, och en handler som väntar sig den kan
// sättas på basens fält (`Handler`).
struct MouseArgs : EventArgs {
    gfx::MouseButton button  = gfx::MouseButton::None;
    Point            local{};
    std::uint32_t    mods    = 0;
    bool             handled = false;

    MouseArgs()                            = default;
    MouseArgs(const MouseArgs&)            = default;
    MouseArgs& operator=(const MouseArgs&) = default;
    virtual ~MouseArgs()                   = default;
};

// Args till `Widget::onResize` (WinForms `Resize`,
// WPF `SizeChanged`). Den NYA storleken läses ur sendern —
// `sender->actualSize()` (regel 6). Kontraktet står vid fältet.
//   previous  storleken vid förra LEVERANSEN, logiska px; {0, 0} vid den första
//   first     den första leveransen i fönstrets liv (varje öppning ger en ny)
struct ResizeArgs : EventArgs {
    gfx::Sizef previous{};
    bool       first = false;
};

// ---------------------------------------------------------- DRAG-AND-DROP
//
// Ett drag MELLAN widgets — en KÄLLA, en LAST och ett
// SLÄPP på ett MÅL. `setPointerCapture`/`mouseDrag` bär ett drag INOM en
// widget; det här är något annat. Förebilden är WinForms: `AllowDrop` på
// målet, `DragEnter`/`DragOver`/`DragLeave`/`DragDrop` med en args-struct
// (lasten, tillåtna effekter, vald effekt, position), och `DoDragDrop`
// som startar. Översättningen till husets form står vid `Widget` nedan.
//
// RÄCKVIDDEN är INOM FÖNSTRET — men typerna är FORMADE för de två större: "mellan våra
// fönster" (sessionen flyttar från `AppWindow` till `App`) och "till/från
// andra appar" (OLE · NSPasteboard · XDND · wl_data_device). Därför är
// lasten en mängd FORMAT → BYTES och inte ett naket `void*`, och därför
// FÅR `DragArgs::source` vara nullptr. Ingen OS-del är byggd.

// EFFEKTEN — vad ett släpp gör med lasten. En BITMASK, för källan TILLÅTER
// flera och målet VÄLJER en (WinForms `DragDropEffects`). `Move` betyder
// att posten LÄMNAR källan; källan får utfallet i `onDragEnd` och tar bort
// den där (en widget med inbyggd last — `TreeView` — gör det själv).
enum class DragEffect : std::uint8_t {
    None = 0,
    Copy = 1u << 0,
    Move = 1u << 1,
    Link = 1u << 2,
};

constexpr DragEffect operator|(DragEffect a, DragEffect b)
{
    return static_cast<DragEffect>(static_cast<std::uint8_t>(a) |
                                   static_cast<std::uint8_t>(b));
}
constexpr DragEffect operator&(DragEffect a, DragEffect b)
{
    return static_cast<DragEffect>(static_cast<std::uint8_t>(a) &
                                   static_cast<std::uint8_t>(b));
}

// Tillåter masken effekten? `None` tillåts aldrig — den är frånvaron av ett
// släpp, inte ett val. Så skriver ett mål sitt villkor:
//     if (glaze::allows(e.allowed, glaze::DragEffect::Copy))
//         e.effect = glaze::DragEffect::Copy;
constexpr bool allows(DragEffect mask, DragEffect e)
{
    return e != DragEffect::None && (mask & e) == e;
}

// LASTEN — en ordnad mängd FORMAT → VÄRDE. Formatet är en MIME-typ
// (`text/plain`, `application/x-…`), för det är den form alla fyra
// OS-protokollen kan översättas till: wl_data_offer och XDND talar MIME
// rakt av, OLE har registrerade format och NSPasteboard UTI:er som båda
// mappas från MIME. VÄRDET är bytes i en `std::string` — UTF-8 för text.
//
// Inget `void*` och inget `std::any`, med flit: en last som bara kan läsas
// i den egna processen hade gjort de två senare räckvidderna till ett
// API-brott. Behöver ett drag inom appen peka ut ett OBJEKT bär lasten
// dess id (`TreeView` lägger nodens stig i ett eget format), och källan
// finns dessutom i `DragArgs::source`.
//
// ORDNINGEN är källans PREFERENS (den första är bäst), som i OLE.
// `set` på ett befintligt format ersätter värdet PÅ PLATS.
class DragData {
public:
    // Text — UTF-8 enligt kontrakt. Bakändarna som en dag talar med andra
    // appar översätter till sina egna textformat (CF_UNICODETEXT,
    // `text/plain;charset=utf-8`, NSPasteboardTypeString).
    static constexpr const char* kText = "text/plain";

    void set(const std::string& format, std::string value);
    bool has(const std::string& format) const;
    // Värdet, eller TOM sträng när formatet saknas — skilj frånvaro från
    // ett tomt värde med `has`.
    const std::string& get(const std::string& format) const;
    std::vector<std::string> formats() const;   // i preferensordning
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    void               setText(std::string utf8) { set(kText, std::move(utf8)); }
    const std::string& text() const { return get(kText); }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

// Args till KÄLLANS `onDragStart` — tröskeln är passerad och draget ska
// starta. Allt här är UTDATA (CloseArgs::cancel-mönstret): handlern FYLLER
// `data`. Lämnas den tom startar inget drag, och trycket fortsätter som ett
// vanligt tryck — det är så en källa avstår.
struct DragStartArgs : EventArgs {
    DragData   data;
    // Vilka effekter källan tillåter. Default ALLA — målet väljer, och en
    // källa som bara klarar en (en omordning är bara `Move`) säger det.
    // `None` startar inget drag: inget mål hade kunnat ta emot det.
    DragEffect allowed = DragEffect::Copy | DragEffect::Move | DragEffect::Link;
    // DRAGBILDEN — det som följer pekaren. Tom text ⇒ lastens `text()`;
    // ogiltig bild ⇒ ingen ikon. Den hör till KÄLLAN och inte till lasten:
    // ett mål läser aldrig hur draget såg ut. Bilden är ett `ImageSource` —
    // en asset eller pixlar; sidan är assetens storleksklass, annars
    // `Style::iconFallback`.
    std::string text;
    ImageSource img{};
    // VAR TRYCKET BÖRJADE — fysiska px i fönstret (`ContextMenuArgs`
    // form) respektive LOGISKA px relativt källans eget hörn (`Draw`s
    // form), så en handler kan välja post ur trycket utan ett Ctx.
    float x = 0.0f, y = 0.0f;
    float localX = 0.0f, localY = 0.0f;
};

// Args till MÅLETS fyra events. `effect` är UTDATA och den enda
// tillståndsbiten: ett mål ACCEPTERAR genom att sätta den till en effekt ur
// `allowed` och AVVISAR genom att lämna `None`. En effekt utanför `allowed`
// räknas som `None` — WinForms regel, och skälet att ingen behöver jämföra
// själv.
//
// Värdet BÄRS MELLAN eventen: `onDragOver` börjar med det `onDragEnter`
// (eller förra `onDragOver`) lämnade, så ett mål som bara bryr sig om VAD
// som dras svarar en gång i enter och låter resten rulla. `onDrop` börjar
// med det accepterade valet och får ändra det (t.ex. till `None` när
// släppet misslyckades) — det är DET källan får i `onDragEnd`.
//
// `onDrop` körs FÖRE widgetens inbyggda släpp: appen
// frågas innan något ändras, så den kan NEKA (`effect = None`) eller UTFÖRA
// släppet själv (`handled`). Annars utför det inbyggda samma plats efteråt.
// Tar handlern MÅLET ur trädet utan `handled` blir utfallet `None` — ingen
// utförde släppet, och källan ska inte ta bort det som drogs.
//
// `data` är en REFERENS till sessionens last — giltig under anropet,
// aldrig sparad. Args skapas av ramverket och inte av appen.
struct DragArgs : EventArgs {
    explicit DragArgs(const DragData& d) : data(d) {}

    const DragData& data;
    DragEffect      allowed = DragEffect::None;   // källans mask (läs)
    DragEffect      effect  = DragEffect::None;   // UTDATA — målets val
    // Pekaren — fysiska px i FÖNSTRET (exakt vad `Menu::popupAt` tar, så
    // ett släpp kan fråga "kopiera eller flytta?" med en meny) och LOGISKA
    // px relativt MOTTAGARENS eget hörn (det layoutfälten talar, så
    // `barn.x = e.localX;` placerar där släppet skedde).
    float   x = 0.0f, y = 0.0f;
    float   localX = 0.0f, localY = 0.0f;
    // Källan. FÅR vara nullptr: ett drag från en ANNAN app har ingen
    // widget, och formen ska klara den räckvidden utan API-brott.
    Widget* source = nullptr;
    // UTDATA i `onDrop` (`KeyArgs::handled`s namn):
    // true = "jag har utfört släppet själv" — widgetens inbyggda släpp
    // hoppas över, och `effect` är fortfarande utfallet källan får. Att neka
    // är `effect = None`, som vinner över `handled`. Läses bara efter
    // `onDrop`; i de andra eventen betyder fältet ingenting.
    bool    handled = false;
};

// Args till KÄLLANS `onDragEnd` — draget är slut, på ett eller annat sätt.
// `effect` är målets slutliga val (WinForms returvärde ur `DoDragDrop`):
// `None` betyder avbrutet (Escape, fokusförlust), avvisat eller släppt
// utanför varje mål. `Move` betyder att posten ska LÄMNA källan.
struct DragEndArgs : EventArgs {
    DragEffect effect = DragEffect::None;
    // Där lasten släpptes; nullptr när den inte togs emot. Giltig under
    // anropet.
    Widget*    target = nullptr;
};

// ---------------------------------------------------------------- Handler

class Widget;

// HANDLER-FÄLTET: fullformen (sender, args) OCH den parameterlösa formen,
// sida vid sida.
//
// Varför en wrapper och inte två fält: fält kan inte överlagras, och att
// ERSÄTTA fullformen är uteslutet — kontraktet i regel 6 (ny
// event-data blir ett fält i args, aldrig en ny parameter) är det som gör
// att befintliga handlers aldrig behöver skrivas om. Men fullformen tvingar
// `sender` på VARJE användningsställe, med en `static_cast` från
// basklassen så fort man vill åt widgetens egna fält:
//
//     stang.onClick = [this](glaze::Widget*, glaze::ClickArgs&) { close(); };
//     stang.onClick = [this] { close(); };            // ← den korta
//
// LAGRINGEN är EN normaliserad fullform: den parameterlösa adapteras VID
// TILLDELNING, inte vid varje avfyrning. Avfyrningsställena är därför
// enkla (`if (onClick) { ClickArgs e; onClick(this, e); }`).
// Genererad kod emitterar alltid fullformen; den korta är för handskriven
// kod — och GRAMMATIKEN känner igen den och skriver
// tillbaka den som den stod (`Stmt::shortForm`).
//
// TVÅ FÄLLOR, båda avsiktligt lämnade som kompileringsfel:
//   - En GENERISK lambda (`[](auto...){}`) är tvetydig: den matchar båda
//     formerna. Skriv ut parametrarna.
//   - Nollställning är **`onClick = nullptr;`**. `onClick = {}` är
//     TVETYDIG (verifierat med g++) — `{}` konverterar till både en tom
//     `Handler` och till `std::nullptr_t`.
//
// DET ANROPSBARA LEVER GENOM SITT ANROP. Det ligger i en
// `shared_ptr`, och `operator()` tar en EGEN referens innan den anropar: en
// handler som river sin widget (och därmed det här fältet), eller som
// tilldelar om sitt eget fält, kör färdigt med sina captures intakta — utan
// att lambdan kopieras. Det gäller VARJE event, fönstrets livscykel och
// menyerna inräknade.
//   Valet har en följd för KOPIOR, och den är avsiktlig: en kopierad
// `Handler` DELAR det anropsbara i stället för att klona det. En muterbar
// lambdas tillstånd är alltså ETT, var den än anropas ifrån — menyernas
// ögonblicksbild (`Menu::activatePath` kopierar postens handler innan den
// kör) räknar i samma räknare som posten, och inte i en klon som sedan
// kastas. Tilldelning skapar alltid ett NYTT anropsbart
// och rör aldrig en tidigare kopia.
//
// AVSÄNDAREN ÄR DEN SOM AVFYRADE — regel 6:s
// mening, gjord till en typparameter: `Sender` är `Widget` som förval, och en
// klass som inte är en widget men har egna events är sin EGEN avsändare
// (`Handler<ClickArgs, TrayIcon>` på systemfältets ikon). Värdena läses då ur
// sender där också; `nullptr` eller en intern widget som sender hade brutit
// båda halvorna av regeln. Varje befintlig `Handler<X>` är `Handler<X,
// Widget>` och kompilerar oförändrad.
//
// EN HÄRLEDD ARGS-TYP. Ett fält vars args är
// POLYMORFA (`MouseArgs`) tar också fullformen med en ÄTTLING —
// `karta.onMouseDown = [](glaze::Widget*, glaze::CanvasMouseArgs& e) { … };`
// på basens `Handler<MouseArgs>`. Typen läses ur det anropsbaras ENDA
// anropsoperator (en lambda, en `std::function`, en fri funktion eller en
// funktionspekare, också `noexcept`; en generisk lambda har ingen och tas av
// fullformen) och PRÖVAS VID
// ANROPET: widgeten som avfyrar bygger sina egna args (ritytan en
// `CanvasMouseArgs`), och kom en annan typ körs handlern INTE — loggat EN gång
// per handler. Så kompilerar en handler som väntar sig ritytans args också på
// en knapp, men den körs bara där argsen finns.
namespace detail {
// Den andra parametern i en signatur med två — tom för allt annat. En FRI
// FUNKTION når hit som pekare (`HandlerArgOf` låter den förfalla), också en
// `noexcept`-funktion, vars pekartyp är en egen typ sedan C++17.
template <class T>
struct SecondParam {};
template <class R, class A, class B>
struct SecondParam<R (*)(A, B)> {
    using type = B;
};
template <class R, class A, class B>
struct SecondParam<R (*)(A, B) noexcept> {
    using type = B;
};
template <class C, class R, class A, class B>
struct SecondParam<R (C::*)(A, B)> {
    using type = B;
};
template <class C, class R, class A, class B>
struct SecondParam<R (C::*)(A, B) const> {
    using type = B;
};
template <class C, class R, class A, class B>
struct SecondParam<R (C::*)(A, B) noexcept> {
    using type = B;
};
template <class C, class R, class A, class B>
struct SecondParam<R (C::*)(A, B) const noexcept> {
    using type = B;
};
// Det anropsbaras anropsoperator — en klass med EN (`&F::operator()`, också
// `std::function`s), annars typen själv (en funktionspekare).
template <class F, class = void>
struct CallOperator {
    using type = F;
};
template <class F>
struct CallOperator<F, std::void_t<decltype(&F::operator())>> {
    using type = decltype(&F::operator());
};
// `std::decay_t` och inte `remove_cvref_t`: en
// fri funktion tilldelas som FUNKTION (`karta.onMouseDown = nedtryck;`), och
// först förfallen till en pekare har den en signatur `SecondParam` känner.
template <class F>
using HandlerArgOf = std::remove_reference_t<
    typename SecondParam<typename CallOperator<std::decay_t<F>>::type>::type>;
// Loggraden när en handler väntade sig en ättling som inte kom (widget.cpp).
void logHandlerArgs(const char* väntad, const char* fick);
}

template <class Args, class Sender = Widget>
class Handler {
public:
    using Full = std::function<void(Sender* sender, Args& e)>;

    Handler()                          = default;
    Handler(const Handler&)            = default;
    Handler(Handler&&)                 = default;
    Handler& operator=(const Handler&) = default;
    Handler& operator=(Handler&&)      = default;

    // FULLFORMEN — kontraktet. Tar även en färdig `std::function`.
    template <class F,
              std::enable_if_t<std::is_invocable_v<F&, Sender*, Args&>,
                               int> = 0>
    Handler& operator=(F&& f)
    {
        fn_ = std::make_shared<Full>(std::forward<F>(f));
        return *this;
    }

    // PARAMETERLÖSA formen — adapteras här, en gång.
    template <class F,
              std::enable_if_t<std::is_invocable_v<F&> &&
                                   !std::is_invocable_v<F&, Sender*, Args&>,
                               int> = 0>
    Handler& operator=(F&& f)
    {
        fn_ = std::make_shared<Full>(
            [g = std::forward<F>(f)](Sender*, Args&) mutable { g(); });
        return *this;
    }

    // FULLFORMEN MED EN HÄRLEDD ARGS-TYP (se ovan) — bara för ett fält
    // vars args är polymorfa, och bara för en ÄTTLING till dem.
    template <class F, class D = detail::HandlerArgOf<F>,
              std::enable_if_t<!std::is_invocable_v<F&, Sender*, Args&> &&
                                   !std::is_invocable_v<F&> &&
                                   std::is_polymorphic_v<Args> &&
                                   std::is_base_of_v<Args, D> &&
                                   !std::is_same_v<Args, std::remove_cv_t<D>> &&
                                   std::is_invocable_v<F&, Sender*, D&>,
                               int> = 0>
    Handler& operator=(F&& f)
    {
        fn_ = std::make_shared<Full>(
            [g = std::forward<F>(f), loggad = false](Sender* s, Args& e) mutable {
                if (D* d = dynamic_cast<D*>(&e)) {
                    g(s, *d);
                    return;
                }
                if (!loggad) {
                    loggad = true;
                    detail::logHandlerArgs(typeid(D).name(), typeid(e).name());
                }
            });
        return *this;
    }

    // Nollställning: `onClick = nullptr;` (se fällan ovan).
    Handler& operator=(std::nullptr_t)
    {
        fn_.reset();
        return *this;
    }

    explicit operator bool() const { return fn_ && *fn_; }

    // Den EGNA referensen först (se ovan): fältet får dö under anropet.
    void operator()(Sender* sender, Args& e) const
    {
        const std::shared_ptr<Full> hållen = fn_;
        if (hållen && *hållen)
            (*hållen)(sender, e);
    }

private:
    std::shared_ptr<Full> fn_;
};

// ---------------------------------------------------------------- Widget

class AppWindow;   // ägar-pekaren i Widget (damage-bokföringen)

class Widget {
public:
    virtual ~Widget() = default;

    // ---------------------------------------------- LAYOUT-FÄLTEN är RÅA
    //
    // KONTRAKTET, skrivet och inte underförstått: fälten i
    // det här blocket — `name`, `x`/`y`, `width`/`height`, `right`/`bottom`,
    // `dock`, `anchors`, `anchorLeft`/`anchorTop`/`anchorRight`/
    // `anchorBottom`, `tabIndex` — är PUBLIKA och saknar settrar med
    // flit. Allt ANNAT tillstånd (text, checked, value, items …) är privat
    // med `setX()`/`x()`, eftersom settern är den som INVALIDERAR.
    //
    // Skälet till undantaget är verktygsvägen: de här nio är den
    // GENERERADE kodens yta — fem rader per widget i varje `.glazeui` —
    // och som settrar hade hela fältrads-grammatiken dött till ingen
    // användarnytta. En layoutändring i drift är dessutom sällsynt och
    // hör ihop med en omräkning som ändå invaliderar.
    //
    // PRISET, utskrivet: ändrar du ett av dem UTANFÖR en händelse-handler
    // syns det inte förrän något annat ritar om — kalla `invalidate()`
    // själv då (regel 1).

    // WIDGETENS NAMN — **objektets namn, speglat**. Det är INTE ett
    // fält man sätter separat: det
    // är samma identitet som C++-medlemsnamnet, gjord synlig för runtime.
    //
    //     glaze::Button Button1;        // objektets namn
    //     Button1.name = "Button1";     // ALLTID samma — emitterat
    //
    // Döper man om objektet följer namnet med, för de ÄR samma sak;
    // GlazeBuilder emitterar raden på varje nod och skriver om båda vid en
    // omdöpning. C++ har ingen reflektion över medlemsnamn, så HANDKODADE
    // fönster sätter raden själva (`btn.name = "btn";`) där ett namn
    // behövs — syskonankare (`SiblingAnchor::to`) och `find<T>()` slår upp
    // exakt den strängen.
    //
    // Namnet är `name` och inte `id`: `id` antyder ett
    // ogenomskinligt handtag man tilldelar, medan det här är WinForms
    // `Name` — samma sak som variabeln heter.
    std::string name;             // valfri identitet; slås upp med find()
    float x = 0, y = 0;           // offset från vänster/topp (logiska px)
    float width = 0, height = 0;  // 0 = auto (t.ex. Label mäter sin text)

    // Placeringen (prioritet i resolve): dock > anchors (default left+top
    // = rena x/y). right/bottom är offsets från höger-/bottenkanten och
    // används av höger-/botten-ankaret; vid dubbelankare (sträckning)
    // gäller båda offsetsen samtidigt (w = area.w − (x + right)·scale).
    Dock    dock = Dock::None;
    Anchors anchors;
    float   right = 0, bottom = 0;   // logiska px

    // SYSKONANKARE per kant (se `SiblingAnchor` ovan). Satt kant VINNER
    // över förälderankaret på samma kant; tom `to` = orört. Prioriteten i
    // `resolve()` är: placesChildren > dock > (syskon-/
    // förälderankare per kant), och två bundna kanter på samma axel
    // sträcker precis som `anchors.left && anchors.right` gör.
    SiblingAnchor anchorLeft, anchorTop, anchorRight, anchorBottom;

    // TAB-ORDNINGEN som ett VÄRDE (WinForms-modellen). **-1 = AUTO**:
    // widgeten tar sin plats i trädordningen, alltså
    // djupet-först. Ett värde >= 0 är satt av användaren eller
    // GlazeBuilder och jämförs mot de auto-sattas LISTPOSITIONER, så en
    // 3:a hamnar där man menar.
    //
    // Numret LAGRAS aldrig och delas aldrig ut: auto-numret ÄR widgetens
    // position i fokuslistan och räknas fram vid varje Tab-tryck
    // (AppWindow::moveFocus). Därför kan det aldrig glida isär från
    // trädet — flyttas eller adopteras något i drift stämmer ordningen
    // vid nästa tryck, utan cache och utan invalidering.
    //
    // Konsekvenser värda att känna till:
    //   - Ett värde HÖGRE än antalet fokuserbara hamnar sist, inte
    //     utanför — det är en sortering, inte täta index.
    //   - `tabIndex = 0` betyder "först BLAND DEM SOM BÄR 0", inte "före
    //     allt". Den auto-satta widget som redan står först i listan bär
    //     nyckeln 0 den också, och den STABILA sorteringen låter den
    //     behålla platsen. Vill man verkligen ligga före allt annat
    //     numrerar man de andra också — precis som i WinForms.
    //   - `tabIndex` på något som inte är fokuserbart (Label), på en dold
    //     eller på en inaktiverad widget är INERT: den finns aldrig i
    //     listan som sorteras.
    //   - En Radio-GRUPP bidrar med exakt ett element (den ikryssade), så
    //     gruppen flyttas genom att SAMMA tabIndex sätts på medlemmarna.
    //     Dubbletter är harmlösa — sorteringen är stabil.
    int tabIndex = -1;

    // Synlig/aktiverad sätts via settrarna nedan — de invalidaterar vid
    // ändring så bytet syns direkt (händelsestyrd rendering, regel 1).
    void enable()  { setEnabled(true); }
    void disable() { setEnabled(false); }
    bool isEnabled() const { return enabled_; }

    void show() { setVisible(true); }
    void hide() { setVisible(false); }
    bool isVisible() const { return visible_; }

    // Event-handlers enligt (sender, args)-kontraktet ovan.
    Handler<ClickArgs> onClick;
    Handler<HoverArgs> onHover;

    // HÖGERKLICKET. Sätts på den widget som ska SVARA —
    // routingen letar uppåt i `parent()`-kedjan från widgeten under
    // pekaren till första som har en satt handler, och den blir `sender`.
    // Sätt den på fönstrets `root()` för "hela fönstret", på en panel för
    // "panelen och allt i den". Noll befintliga widgets påverkas: utan
    // satt handler händer ingenting.
    Handler<ContextMenuArgs> onContextMenu;

    // ------------------------------------- TANGENTERNA
    //
    // WinForms `KeyDown`/`KeyUp`, på VARJE widget. `onKeyDown` går till
    // FOKUSENHETEN FÖRE widgetens egen hantering: den fokuserade widgeten och
    // de närmaste förfäderna i rad som är kompositer i Tab-mening
    // (`focusesChildren() && focusable() && isEnabled() && isVisible()` —
    // talfältet, datumväljaren, datagriden), INNERST FÖRST. Utan fokuserad
    // widget är enheten fönstrets rot, så `root().onKeyDown` är fönstrets
    // tangenter (WinForms `Form.KeyDown`). Därefter den fokuserade
    // widgetens `keyDown`, förfädernas `descendantKeyDown`, acceleratorerna och
    // Tab. En förfader som INTE är en komposit (en panel, ritytan med ett
    // fält på sig) ser inte barnets tangenter: förstapersonens WASD på ritytan
    // hade annars tagit W ur fältet.
    //
    //   handled   stoppar HELA vägen — ingen `keyDown`, inget mer. Ett TextArea
    //             vars handler tar Ctrl+Enter skickar utan radbrytning; ett
    //             TextInput vars handler tar Enter committar inte vid Enter
    //             (utan vid fokusförlusten).
    //   fokus     en handler som FLYTTAR fokus utan `handled` avslutar också
    //             vägen — den inbyggda hanteringen fortsätter aldrig i en
    //             annan widget än den enheten räknades ur.
    //   tecknen   `handled` stoppar tangenten som KOMMANDO, inte tecknet:
    //             skrivtecken kommer som `TextInput` (gfx/events.h), så en
    //             bokstav som tas här skrivs ändå i fältet.
    //   popupen   har någon i enheten en ÖPPEN POPUP (dropdownens lista,
    //             datumväljarens kalender) hoppas handlarna över — tangenterna
    //             hör popupen till, och på Wayland når de den direkt — och då
    //             också vid släppet.
    //   före      en öppen meny och grab-toppen (menyns tangenter) får
    //             tangenten före enheten, appens `event()`-krok före allt.
    //
    // `onKeyUp` går till SAMMA KEDJA som tryckets `onKeyDown` — också om fokus
    // flyttats sedan trycket eller en widget i den inaktiverats: ett läge som
    // börjar i A slutar i A, som `mouseUp`. OS:ets upprepning (`repeat`) går
    // också till tryckets kedja: tangentslaget är dess från trycket till
    // släppet, och den som får en upprepning får släppet (den inbyggda vägen,
    // `keyDown`, följer fokus). Kom trycket aldrig hit (det gjordes i
    // ett annat fönster, en meny eller `event()` åt det) blir den första
    // upprepningen slagets början i enheten som har fokus då, och utan
    // upprepning går släppet till enheten som har fokus nu — ett `onKeyUp`
    // utan föregående `onKeyDown` är TILLÅTET, och en handler ska tåla det. Tappar
    // fönstret fokus, eller döljs det, medan tangenten hålls kommer ett eget
    // släpp med `cancelled` (mods 0), och det kan komma MEDAN en
    // `onKeyDown`-handler kör: en modal dialog som handlern öppnar tar fokus
    // från fönstret. Modifierarna (Ctrl, Shift …) kommer som `Key::Unknown`
    // och paras aldrig — ett läge knutet till en modifierare läser
    // `AppWindow::modifierKeys()`.
    //
    // Efter en körd tangenthandler ritas hela fönstret om, som efter ett
    // klick — också för en tangent den inte tog. I `.glazeui` är båda vanliga
    // event-kopplingar (`falt.onKeyDown = [this] { … };`).
    Handler<KeyArgs> onKeyDown;
    Handler<KeyArgs> onKeyUp;

    // ------------------------------- APPENS MUS
    //
    // WinForms `MouseDown`/`MouseUp`, på VARJE widget, för appen (en egen
    // komponent har krokarna nedan). FÖNSTRET fyrar dem, en väg för alla
    // widgets. MÅLET är widgeten trycket gäller: den som håller explicit
    // capture, annars den AKTIVERADE widgeten under pekaren, och på tom
    // fönsteryta fönstrets rot (`root().onMouseDown` = fönstrets mus, som
    // `root().onKeyDown`). INGEN bubbling: en inaktiverad widget får inget, och
    // inte heller dess förälder (`onHover`s regel — snabbmenyns routade uppslag
    // är en annan sak).
    //
    //   onMouseDown   vid trycket, FÖRE widgetens eget (vänsterknappen EFTER
    //                 fokusflytten — fokus är fönstrets, inte widgetens). Med
    //                 `handled` TAR APPEN TRYCKET: widgeten får ingenting av
    //                 det. Vänster: inget `mouseDown`/`mouseDrag`/`mouseUp`,
    //                 inget klick (`onClick`), inget dubbelklick, inget
    //                 drag-and-drop, och den är inte nedtryckt (`pressed()`
    //                 falsk). Mitten och höger: ingen `middleDown`/`rightDown`
    //                 och ingen snabbmeny — trycket hör till målet. Pekaren
    //                 följer målet (`cursorAt`) tills släppet, och under trycket
    //                 får målet sina rörelser genom `appPress` och aldrig genom
    //                 hover-kroken `mouseMove`.
    //   onMouseUp     när knappen släpps, till SAMMA widget som fick tryckets
    //                 `onMouseDown`, FÖRE widgetens eget släpp och med
    //                 `pressed()` redan släckt. Med `handled` TAR APPEN
    //                 SLÄPPET: höll widgeten själv trycket (vänster: den
    //                 tryckta; mitten och höger: den vars krok tog det) uteblir
    //                 dess släpp och klicket, och trycket slutar för den som ett
    //                 AVBROTT (`mouseCancel`) — delarens skikt återgår, och ett
    //                 reglage committar inte (`onCommit` uteblir; värdet det
    //                 dragits till står kvar). Explicit capture är
    //                 widgetens egen: den fångande får sitt `mouseUp` ändå, och
    //                 en FÖRFADER som tog ett mitten- eller högertryck rörs inte.
    //                 För ett tryck appen tog är `handled` utan verkan.
    //   onMouseCancel i stället för `onMouseUp` när trycket slutar utan släpp:
    //                 fokusförlusten, `MouseCaptureLost`, ett annat av appens
    //                 fönster som tar trycket, ett nytt tryck med samma knapp —
    //                 och för HÖGERKNAPPEN ett otaget tryck som blev
    //                 snabbmenyn (en `onContextMenu`-handler kördes: menyn tar
    //                 pekaren, och släppet kommer olika på olika plattformar).
    //                 Efter widgetens egen `mouseCancel`; `handled` läses inte.
    //
    // Varje `onMouseDown` följs alltså av EXAKT ETT `onMouseUp` eller
    // `onMouseCancel` till samma widget — utom när widgeten lämnar trädet mitt
    // i trycket (ingen användarkod under en trädmutation) och när fönstret
    // stängs. Fältens args är `MouseArgs`; en widget med rikare args (ritytans
    // `CanvasMouseArgs`) bygger sina egna (`fireMouse`), och en handler som
    // väntar sig dem kan sättas här (`Handler`, B86-3). Efter en körd
    // `onMouseDown`/`onMouseUp` ritas hela fönstret om. I `.glazeui` är de
    // vanliga event-kopplingar i båda formerna
    // (`knapp.onMouseDown = [this](glaze::Widget* s, glaze::MouseArgs& e) { … };`).
    Handler<MouseArgs> onMouseDown;
    Handler<MouseArgs> onMouseUp;
    Handler<MouseArgs> onMouseCancel;

    // --------------------------------- STORLEKEN ÄNDRADES
    //
    // `onResize` (WinForms `Resize`, WPF `SizeChanged`): widgetens storlek i
    // LOGISKA px — `actualSize()` — är en annan än vid förra leveransen.
    // `ResizeArgs::previous` är den förra, `first` den första i fönstrets liv.
    // UPPTÄCKTEN sker i ritningen (layouten är tillståndslös och storleken
    // finns bara där), LEVERANSEN i pumpens svans — aldrig mitt i en ritning,
    // aldrig med användarkod i render-callbacken. Följderna:
    //   * framen där storleken ändrades ritas med det gamla storleksberoende
    //     innehållet; handlern körs före nästa frame. Det som måste följa i
    //     SAMMA bildruta räknas i ritningen (Dock/Anchors, `draw()`).
    //   * mellanlägen SAMMANFOGAS: två ritningar före svansen ger EN händelse
    //     med den senaste storleken, och A → B → A ger ingen.
    //   * den FÖRSTA leveransen kommer före fönstrets första synliga bildruta
    //     (efter `onLoad`, före `onShow`); en popup och ett offscreen-fönster
    //     får sin efter första framen. Varje öppning ger `first` igen.
    //   * en dold widget och en widget i en inaktiv dockningsflik får ingen
    //     händelse; visad i en ny storlek får den en.
    //   * på WINDOWS och MACOS kommer händelsen under ett FÖNSTERDRAG först
    //     när draget släpps — OS:ets modala loop stoppar all appkod där. En
    //     delare eller dockningsytan drar i vår pump och levererar per
    //     rörelse; Linux levererar per frame.
    //   * KEDJESPÄRREN (regel 1): en handler som ändrar layouten ger en ny
    //     händelse nästa frame. Har leveranserna hållit fönstret ritande i 64
    //     bildrutor i följd utan att någon riktig inmatning kommit, levereras
    //     de inte förrän nästa händelse (loggat en gång) — en svängning eller
    //     en växande handler blir aldrig en evig frame-loop.
    // `root().onResize` är fönstrets yta under namnlisten (menyraden och
    // statusraden ingår); ritytans synliga utsnitt läses i handlern
    // (`karta.onResize = [this] { ladda(karta.visibleRegion(plan)); };`).
    // Efter en körd handler ritas fönstret om. I `.glazeui` en vanlig
    // event-koppling i båda formerna.
    Handler<ResizeArgs> onResize;

    // --------------------------------------------- FOKUSHÄNDELSERNA
    //
    // FOKUS INOM, WinForms `Enter`/`Leave`: `onFocus` när fokus kommer IN i
    // mig eller i något i mitt delträd, `onFocusLost` när det LÄMNAR mig och
    // hela mitt delträd. Flyttas fokus från A till B fyras `onFocusLost` på
    // varje widget i A:s förfäderskedja (A inräknad) som INTE också står i
    // B:s, INNERST först, och sedan `onFocus` på varje widget i B:s kedja som
    // inte stod i A:s, YTTERST först. Så får en komposit (`SpinBox`,
    // `DatePicker`, `FilePicker`) sina egna events när fokus går in i eller
    // lämnar den, utan en rad kod i kompositen — och en flytt INOM den
    // (filväljarfältets fält → dess "…"-knapp) fyrar ingenting på den.
    // Fönstrets rot fyrar aldrig.
    //
    // POSTADE, aldrig synkrona: `AppWindow::setFocus` byter fokus, kör
    // krokarna (`focusGained`/`focusLost`, som aldrig får köra användarkod)
    // och POSTAR sedan eventen till pump-svansen (`glaze::app().post`), med
    // en vakt per widget (se ORDNINGEN nedan) — en widget som dör emellan
    // får ingenting.
    // En handler ser alltså fokus som det ÄR (`focused()` stämmer redan), och
    // efter den ritas hela fönstret om, som efter ett klick. Två snabba byten
    // i samma pumpvarv ger sina events i ordning.
    //
    // ORDNINGEN MOT KLICKET är det som gör den vanligaste användningen möjlig:
    // vid ett klick flyttas fokus FÖRE `mouseDown`, och fältets `mouseDown`
    // lägger careten där man klickade — men det postade `onFocus` körs i
    // pump-svansen efter hela trycket. Därför fungerar
    //     hojd.onFocus = [this] { hojd.selectAll(); };
    // både vid Tab och vid klick. Ett tryck som GAV fokus väntar dessutom ut
    // dragtröskeln (`pressGaveFocus()`), så en darrig hand mellan tryck och
    // släpp rör inte markeringen; ett riktigt drag markerar från trycket.
    //
    // NÄR DE INTE FYRAS: när FÖNSTRET tappar OS-fokus (fokus står kvar i
    // fönstret och kommer tillbaka dit), och när en fokuserad widget göms
    // eller inaktiveras utan att fokus flyttas — ramverket flyttar inte
    // fokus vid `hide()`/`disable()`. Det som flyttar fokus fyrar: klicket,
    // Tab, `focus()`, en komposit som flyttar det när den döljer sin
    // inredning (flikkontrollen, datagridens editor) och `unadopt` (fokus
    // till ingenting).
    //
    // ORDNINGEN ÄR GARANTERAD: varje fönster har EN kö för sina fokus-
    // händelser, och de levereras i den ordning bytena skedde — också när en
    // handler pumpar nästlat (en modal dialog ur `onFocusLost`) och ett nytt
    // byte sker under tiden: den nästlade pumpen fortsätter där den yttre
    // var, så ingen widget får sitt `onFocus` efter ett senare byte. Och
    // ingen leverans sker till en widget som dött, lossats eller vars
    // fönster dött sedan bytet (`HandlerGuard`s tre villkor) — fönstrets liv
    // frågas före VARJE leverans, för en tidigare handler i samma kö får
    // riva det.
    Handler<FocusArgs> onFocus;
    Handler<FocusArgs> onFocusLost;

    // ------------------------------------------------- TOOLTIPEN
    //
    // wx `SetToolTip` sitter på `wxWindow`, och här sitter den på BASEN av
    // samma skäl: VARJE widget kan behöva förklaras — verktygsknappen först
    // av alla, en ikon utan text. Texten är en `TrText`, så
    // `setToolTip(glaze::tr(id))` följer språket som all annan text, och
    // formen är ett setter-fält (`spara.setToolTip("Spara filen");`) som
    // `.glazeui` bär och previewn utför. Tom text = ingen tooltip.
    //
    // VISNINGEN ÄGS AV FÖNSTRET (`AppWindow`, src/tooltip.cpp): när pekaren
    // VILAR över widgeten i `Style::toolTipDelayMs` öppnas ett popup-fönster
    // strax under pekaren — popup-kontraktet, så det fungerar även på
    // Wayland, som saknar skärmrymd — och det göms vid rörelse ut, klick,
    // tangent, hjul och fokusförlust. Efter ett klick eller en tangent visas
    // den inte igen förrän pekaren lämnat widgeten (Windows). Gick pekaren
    // DIREKT från en visad tooltip till nästa väntar den bara
    // `toolTipReshowMs` — man läser sig längs ett verktygsfält.
    //
    // ROUTED LOOKUP, `onContextMenu`s form: en widget utan egen tooltip
    // visar sin närmaste förfaders. En komposits inredning (talfältets inre
    // fält) har ingen egen, och då är det kompositens som gäller. En
    // INAKTIVERAD widget visar sin tooltip — det är ofta där den behövs
    // mest ("kräver en öppen fil").
    //
    // Byts texten medan tooltipen VISAS stängs den, och nästa vila visar den
    // nya: en popup kan inte byta storlek efter att den visats (Wayland), och
    // en tooltip som stod kvar med gammal text vore värre än ingen.
    void               setToolTip(TrText t);
    const std::string& toolTip() const { return toolTip_.str(); }
    const TrText&      toolTipToken() const { return toolTip_; }

    // TOOLTIP PER DEL — tooltipen vid (x, y), FYSISKA
    // fönsterkoordinater som mus-krokarnas, med widgetens behavior-Ctx
    // (`ctxFor`). Basen svarar med widgetens egen (`toolTipToken()`) var
    // pekaren än står. En widget vars DELAR är data och inte barn —
    // dockningsytans handtag och flikar, och listornas rader om de vill —
    // overridar och svarar med delens text, och med sin egen (eller tomt)
    // utanför delarna. Tom text = ingen tooltip, och då frågas förfadern
    // (det routade uppslaget ovan).
    //
    // Fönstret jämför KÄLLA OCH TOKEN: pekaren som går från en del till en
    // annan i samma widget gömmer den visade tooltipen och väntar den korta
    // tiden (`toolTipReshowMs`), precis som mellan två widgets. Ren fråga:
    // inget tillstånd, ingen invalidering. Texten i en del som BYTS medan
    // den visas syns först vid nästa vila (`setToolTip`-regeln ovan gäller
    // bara widgetens egen text).
    virtual TrText toolTipAt(const Ctx&, float, float) const
    {
        return toolTip_;
    }

    // ------------------------------------- PEKARENS FORM
    //
    // En form APPEN sätter på widgeten (B17; WinForms `Control.Cursor`) — den
    // slår widgetens egen `cursorAt` över HELA widgeten, och den ÄRVS INTE av
    // barnen: ett textfält i en panel med handen behåller sin I-balk, och en
    // ärvd form hade krockat med varje widgets egna former. `resetCursor()`
    // lämnar tillbaka formen åt `cursorAt`; `cursor()` svarar med den satta,
    // eller inget. Fönstrets vänta-pekare (`AppWindow::setUseWaitCursor`)
    // slår den, och en inaktiverad widget visar pilen.
    //
    // Syns DIREKT när pekaren står över widgeten — fönstret prövar om formen
    // med den senast kända pekarpositionen, utan att musen behöver röra sig.
    // Formen är ett setter-fält (`btn.setCursor(gfx::PointerShape::Hand);`)
    // som `.glazeui` bär och previewn utför; `setProp`-nyckeln `cursor` tar
    // `gfx::PointerShape::Hand` eller bara `Hand`.
    void                             setCursor(gfx::PointerShape s);
    void                             resetCursor();
    std::optional<gfx::PointerShape> cursor() const { return cursor_; }

    // ------------------------------ RULLNINGENS RIKTNING
    //
    // Vänd hjulet över widgeten och dess barn, lodrätt och i sidled, för
    // musens hjul och för styrplattan var för sig. Det en widget inte satt
    // ÄRVS: från närmaste förfader som satt något (till och med fönstrets rot
    // — `AppWindow::setWheelInvert`), annars från appen
    // (`App::setWheelInvert`). En popup — en meny, rullgardinens lista,
    // kalendern — är ett eget fönster och ärver från appen, inte från
    // widgeten som öppnade den.
    //
    // Vändningen görs på EN plats, fönstrets hjuldispatch, för varje widget
    // hjulet prövas på: listorna, tabellerna, textfälten, `Panel`,
    // `Scrollbar`, `Slider`, `SpinBox`, menyerna och dockningens paneler får
    // redan vända rader. Den är RELATIV MOT OS:ET — det OS:et redan vänt
    // (naturlig rullning) vänds härifrån, aldrig tillbaka. Ritytan följer sina
    // bindningar i stället (`followsWheelDirection`). Sättarna lagrar bara;
    // inget ritas om. `Keyboard` har inget hjul — loggas och ingenting ändras.
    // I `.glazeui`: `lista.setWheelInvert(glaze::InputDevice::Mouse, true,
    // false);`.
    void setWheelInvert(InputDevice d, bool vertical, bool horizontal);
    void clearWheelInvert(InputDevice d);   // ärv igen
    // Den UPPLÖSTA vändningen — egen, förfädernas eller appens.
    bool isWheelInverted(InputDevice d, bool horizontal) const;

    // ------------------------------------------------ DRAG-AND-DROP
    //
    // WinForms i husets form. TVÅ EGENSKAPER och SEX EVENTS, och allt går att
    // uttrycka i `.glazeui` (regel 8): egenskaperna är setter-fält
    // (`lista.setAllowDrop(true);`) och eventen är vanliga kopplingar i båda
    // formerna.
    //
    //   allowDrop   jag är ett SLÄPPMÅL (WinForms `AllowDrop`)
    //   allowDrag   jag är en KÄLLA. WinForms har ingen sådan egenskap — där
    //               startar appen draget själv med `DoDragDrop` ur sin
    //               MouseMove. Här är tröskeln ramverkets (`Style::
    //               dragThreshold`), så källan måste kunna SÄGAS — och en
    //               sägbar egenskap är en som går att sätta i designern.
    //
    // FLÖDET: vänstertryck på en källa → pekaren rör sig MER än tröskeln →
    // `onDragStart` (fyll lasten; tom last = inget drag) → pekaren över ett
    // mål: `onDragEnter` och DIREKT därefter `onDragOver`, sedan `onDragOver`
    // per rörelse, `onDragLeave` när den lämnar → släpp med accepterad
    // effekt: `onDrop` på målet → alltid sist `onDragEnd` på källan med
    // utfallet. Escape och fokusförlust avbryter (leave + end med `None`).
    // Ett tryck som BLEV ett drag ger inget `onClick`.
    //
    // VARJE PLATS MÖTER `onDragOver` (HTML5:s ordning —
    // OLE och WinForms fyrar ingen DragOver vid inträdet): också den första,
    // så en regel per plats står på ETT ställe och håller för ett släpp utan
    // en enda rörelse efter inträdet. Effekten bär: `onDragOver` börjar med
    // det `onDragEnter` lämnade.
    //
    // VAR ETT DRAG FÅR BÖRJA avgör widgeten (`canDragFrom` bland krokarna
    // nedan): en kolumnavdelare, en kryssruta eller en rullningslist är inget
    // grepp om lasten, och där förblir trycket ett tryck — också när appens
    // `onDragStart` fyller lasten.
    //
    // ETT UNDANTAG från "alltid", skrivet: lämnar källan eller målet TRÄDET
    // mitt i draget (`unadopt` — typiskt en handler som bygger om en lista)
    // körs INGEN apphandler, varken `onDragLeave` eller `onDragEnd`.
    // Ramverket kör ingen användarkod under en trädmutation, och handlern
    // bor i en widget som kan vara förstörd. Målet får sin `dragLeave`-KROK
    // (widgetens egen städning), och en källa som lämnar avslutar draget
    // tyst. Koden som tar bort widgeten vet om det — `AppWindow::
    // isDragging()` svarar.
    //
    // MÅLET ÄR FÖRSTA FÖRFADER (eller jag själv) under pekaren som har
    // `allowDrop` och är aktiverad — `onContextMenu`s routing. KÄLLAN är
    // däremot den TRYCKTA widgeten och ingen förfader: en Slider i en
    // dragbar panel ska aldrig starta panelens drag.
    //
    // INBYGGT BETEENDE går FÖRE appens handler i varje steg UTOM SLÄPPET
    // (krokarna nedan, `Checkbox::clicked`-ordningen: widgeten gör sitt,
    // sedan berättas det). Start, inträde, rörelse och slut FÖRESLÅR, och
    // appen justerar förslaget: en `TreeView` fyller sin nodlast själv och
    // föreslår platsen, och appens handler kan ändra lasten eller lägga in
    // sitt veto (`e.effect = None` i `onDragOver`). SLÄPPET är det enda steg
    // som ÄNDRAR något, och där frågas appen FÖRST —
    // `onClose`-mönstret: frågan före handlingen. `onDrop` kan neka
    // (`e.effect = None`) eller utföra släppet själv (`e.handled = true`),
    // och widgetens `drop`-krok körs bara när den gjorde varken eller.
    // AUTOSKROLLEN är också inbyggd: nära kanten
    // av en rullande yta rullar den med fönstrets timer, och efter varje
    // steg görs målvalet om på samma punkt — hjulets regel — så platsen och
    // appens `onDragOver` följer med utan att pekaren rört sig (kroken
    // `dragScroll` nedan).
    //
    // OMRITNINGEN (regel 1): ett drag animerar ingenting. Dragbilden ligger
    // i OVERLAY-lagret (`invalidateOverlay` — ingen damage, bara en
    // present), och en körd handler ger den vanliga fulla omritningen —
    // UTOM `onDragOver`, som körs per musrörelse. Den handlern ska sätta
    // `e.effect`; ändrar den något synligt gör settrarna det själva,
    // och ett RÅTT layoutfält kräver `invalidate()` — samma pris som
    // layoutfältens kontrakt ovan redan skriver ut. Prejudikatet är
    // Sliderns `onChange` under en musdragning, som inte heller ger en full
    // omritning: regeln "efter en handler ritas allt om" gäller de
    // diskreta händelserna, inte de som kommer per rörelse.
    void setAllowDrop(bool on) { allowDrop_ = on; }
    bool allowDrop() const { return allowDrop_; }
    void setAllowDrag(bool on) { allowDrag_ = on; }
    bool allowDrag() const { return allowDrag_; }

    Handler<DragStartArgs> onDragStart;   // KÄLLAN: fyll e.data
    Handler<DragEndArgs>   onDragEnd;     // KÄLLAN: e.effect = utfallet
    Handler<DragArgs>      onDragEnter;   // MÅLET: sätt e.effect
    Handler<DragArgs>      onDragOver;    // MÅLET: per rörelse
    Handler<DragArgs>      onDragLeave;   // MÅLET: lämnat/avvisat/avbrutet
    Handler<DragArgs>      onDrop;        // MÅLET: utför släppet

    bool hovered() const { return hovered_; }
    bool pressed() const { return pressed_; }
    bool focused() const { return focused_; }

    // HÅLLER JAG MITTENKNAPPEN? — sant från att min
    // `middleDown` tog trycket till släppet (och preliminärt INNE i den, se
    // `middleDown`), och `pressed()`s parhäst för
    // mittenknappen. Släcks också när fokusförlusten eller borttagningen
    // avbryter trycket UTAN `middleUp`, så en widget som visar ett pågående
    // mittendrag (ritytans flytta-pekare i `cursorAt`) läser den här och
    // inte en egen flagga, som hade stått kvar efter ett avbrott. Läses ur
    // fönstret — en sanning, ingen kopia; false utan ägarfönster.
    bool middlePressed() const;

    // HÅLLER JAG HÖGERKNAPPEN? — `middlePressed()`s
    // tvilling: sant från att min `rightDown` tog trycket till släppet, och
    // släckt också när trycket avbryts (då kommer `mouseCancel(Right)`) eller
    // widgeten lämnar trädet. Läses ur fönstret; false utan ägarfönster.
    bool rightPressed() const;

    // HÅLLER JAG FÖNSTRETS EXPLICITA CAPTURE? — sant från
    // `setPointerCapture`/`takePointerCapture` till `releasePointerCapture`,
    // och falskt så snart fönstret tagit den ifrån mig (fokusförlusten, OS:ets
    // capture-förlust — se `mouseCancel`). Dockningsvärden läser den
    // för att veta att ett drag den äger lever. Läses ur fönstret — en
    // sanning; false utan ägarfönster. Ingen `.glazeui`-form (drift, inte
    // tillstånd).
    bool hasPointerCapture() const;

    // Begär tangentfokus (Tab/klick-vägens programmatiska syskon) — en
    // värd flyttar fokus till sin hostade editor (PropertyGrid) utan ett
    // extra klick. Kräver focusable(); no-op utan ägarfönster.
    void focus();

    // COMMITTA DET SOM REDIGERAS — SYNKRONT, som Enter: en text som
    // skrivits men inte bekräftats blir värdet nu, och widgetens `onChange`/
    // `onCommit` fyras som vanligt, bara om värdet ändrats. Basen gör
    // ingenting; TextInput, TextArea, SpinBox, DatePicker, ColorPicker (det
    // skrivna i hexfältet), FilePicker, PropertyGrid och DataGrid overridar.
    // Till för Spara-handlern som nås UTAN att fokus flyttas — en
    // verktygsknapp, ett menyval, en accelerator, som alla lämnar fokus i
    // fältet: `AppWindow::commitEdits()` anropar den på hela den fokuserade
    // widgetens kedja (WinForms `ValidateChildren`). Ett anrop i handlerkod
    // och inget tillstånd, så den har ingen sats i `.glazeui`.
    virtual void commitEdit() {}

    // Sätter en egenskap som nyckel/värde-text (GlazeBuilders modell- och
    // preview-väg). Basen tar size/pos/dock/anchor/right/bottom/visible/
    // enabled; en subklass overridar för sina egna egenskaper och anropar
    // basen sist. Returnera true när nyckeln KÄNNS IGEN (även om värdet
    // var ogiltigt — logga då själv och ignorera värdet); false = okänd
    // egenskap, då loggar + ignorerar anroparen (regel 4:s arvtagare —
    // äldre verktyg ska kunna öppna nyare kod utan att förstöra den).
    virtual bool setProp(const std::string& key, const std::string& value);

    // ÖVERSATT textfält — `<id>.setText(glaze::tr(1042));`.
    //
    // SKILD från `setProp` MED FLIT: `setProp` tar en STRÄNG, och en
    // widget vars sättare tar `std::string` kan aldrig ta emot ett
    // `glaze::tr(...)` i den kompilerade C++-koden heller. Lät man
    // tr-raden falla ner i `setProp` med den UPPLÖSTA texten skulle
    // previewn visa en rad som appen inte kan kompilera — previewn hade
    // gjort MER än C++-vägen, vilket är precis den asymmetri regel 8
    // förbjuder.
    //
    // Default är `false` = "jag har inget översättbart fält med det
    // namnet", och då blir raden OPAK (regel 4) i stället för utförd.
    // Textwidgetarna overridar; en EGEN widget som vill delta i
    // regel 8 overridar den likadant och ger sin sättare en
    // `TrText`-parameter. Returnera true bara när fältet FAKTISKT tog
    // emot tokenet.
    //
    // BASEN HAR ETT EGET FÄLT — `toolTip` — och en override
    // ska därför anropa basen för de nycklar den inte känner, precis som
    // `setProp` (konventionen: basen SIST). Annars hade
    // `knapp.setToolTip(glaze::tr(5));` blivit opak just på de widgets som
    // har en egen text.
    virtual bool setTextToken(const std::string& key, const TrText& value)
    {
        if (key == "toolTip") {
            setToolTip(value);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------- träd
    // parent() är INNEHÅLLS-relationen (widget i container): koordinater,
    // klippning, hit-test och fokus vandrar genom den. Kedjan SLUTAR VID
    // FÖNSTERROTEN — vandra aldrig uppåt över fönstergränsen; fönster äger
    // fönster via AppWindow::owner(), som ALDRIG bär koordinater.
    Widget* parent() const { return parent_; }

    // Barnen i z-ordning: sist i listan ritas överst (adopt-ordningen ÄR
    // z-ordningen). Listan blandar ÄGDA barn (add/adopt(uptr)) och LÅNADE
    // (adopt(Widget&) — värde-medlemmar i fönsterklassen); ägandet bor i
    // ownedChildren_, trädvandringen ser bara pekarna.
    const std::vector<Widget*>& children() const { return children_; }

    // Skapa och äg ett barn: auto* b = panel->add<glaze::Button>("Spara");
    // Barn resolvar mot containerns childArea (förälder-relativa
    // koordinater) och klipps om containern klipper.
    template <class T, class... Args>
    T* add(Args&&... args)
    {
        auto w = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = w.get();
        adopt(std::move(w));
        return raw;
    }

    // Tar ägarskap av ett färdigbyggt (del)träd — add<T> i generisk form.
    // Sätter parent-pekaren och propagerar fönster-kopplingen rekursivt.
    Widget* adopt(std::unique_ptr<Widget> w);

    // ICKE-ägande adoption: widgeten är ett
    // VÄRDE-objekt någon annan äger — typiskt en medlem i fönsterklassen
    // (genererad kod: root().adopt(okBtn)). Parent/owner/z-ordning som
    // ägda barn, men trädet raderar den aldrig. OBS destruktionsordningen:
    // subklassens medlemmar dör FÖRE basens rot — roten rör aldrig lånade
    // barn i rivning. Dubbeladoption är no-op (loggas).
    Widget* adopt(Widget& w);

    // ---------------------------------------------------- BORTTAGNINGEN
    //
    // TVÅ NAMN, för C++ kan inte överlagra på returtyp och BÅDA formerna
    // identifierar barnet med samma `Widget&`:
    //
    //   `unadopt(w)`      symmetrin till `adopt`: ett LÅNAT barn släpps
    //                     (ägaren behåller objektet), ett ÄGT barn
    //                     FÖRSTÖRS. Svarar true om något togs bort.
    //   `releaseOwned(w)` ger ÄGANDET tillbaka till anroparen —
    //                     flik-fallet "stäng nu, öppna igen". Kräver att
    //                     `w` ligger i `ownedChildren_`.
    //
    // Båda kräver att `w` är MITT DIREKTA barn.
    //
    // VALIDERINGEN ÄR FYRA KONTROLLER, ALLA FÖRE VARJE MUTATION — en nekad
    // operation lämnar trädet EXAKT som det var:
    //
    //   1. Direkt barn? Annars false/tom ptr + logg.
    //   2. RÄTT ÄGANDEKATEGORI? `children_` BLANDAR lånade och ägda barn;
    //      bara `ownedChildren_` bär ägandet. `releaseOwned` på ett LÅNAT
    //      direkt barn nekas med tom ptr och INGEN mutation. Det måste stå
    //      skrivet: formen "lossa lånat barn men returnera tomt" är direkt
    //      farlig, eftersom tom retur normalt betyder MISSLYCKANDE och
    //      anroparen då skulle tro att trädet är orört.
    //   3. `canReleaseChild(w)` på MIG — en komposit som håller RÅA pekare
    //      till sin inredning nekar här.
    //   4. Öppen POPUP i delträdet? `hasOpenPopup()` frågas REKURSIVT över
    //      hela målträdet (en `Dropdown` kan sitta godtyckligt djupt), plus
    //      en fråga om delträdet är VÄRD för en menypresentation.
    //
    // NEKANDET ÄR HELA HANTERINGEN av popup-fallet: false / tom ptr +
    // `gfx::log`, ingen mutation. Anroparen stänger popupen först och låter
    // pumpen gå ETT VARV — `Dropdown::close()` verkställs i pump-svansen,
    // så ett `unadopt` direkt efter `close()` nekas fortfarande. Det är
    // skrivet beteende och inte en bugg.
    //
    // ALDRIG EN RIVNINGSVÄG. `~Widget` gör ingen auto-`unadopt`: lånade
    // barn är värde-medlemmar i fönsterklassen och dör FÖRE basens rot, så
    // roten får aldrig röra dem i rivning. `unadopt` är ett DRIFTSANROP.
    bool                    unadopt(Widget& w);
    std::unique_ptr<Widget> releaseOwned(Widget& w);

    // FLYTTA ETT BARN TILL EN ANNAN FÖRÄLDER (WinForms
    // `control.Parent = annan`) — också i ett ANNAT fönster. `w` måste vara
    // mitt DIREKTA barn, och `to` en annan widget än jag och än `w`, som inte
    // ligger i `w`s delträd (det hade varit en cykel). ÄGANDET FÖLJER MED: ett
    // lånat barn förblir lånat, ett ägt förblir ägt — `to` tar över
    // `unique_ptr`. Samma fyra kontroller som `unadopt` och målets, ALLA FÖRE
    // VARJE MUTATION: en nekad flytt (öppen popup, inredning, cykel) lämnar
    // båda träden orörda och loggar. Fönsterstädningen i det gamla fönstret
    // är `unadopt`s (fokus, tryck, capture, grab, drag, tooltip), och `to`s
    // fönster tar emot delträdet som vid en vanlig `adopt` — bildcachen och
    // ritytans resurser är per fönster och byggs upp där. Svarar om flytten
    // skedde.
    bool transferChild(Widget& w, Widget& to);

    // STRUKTURELLA BARN — vakten mot dinglande komposit-pekare. En komposit
    // som håller RÅA pekare till sina egna barn (`PropertyGrid::editor_`/
    // `picker_`, `TopBar`s fyra) måste kunna NEKA att de tas bort utifrån;
    // föräldern är den enda som vet vilka barn som är dess inredning.
    //
    // Skälet är inte hypotetiskt: `children()` är publikt, så en anropare
    // kan få tag i griden editor och skriva `grid.unadopt(editor)`. Utan
    // vakten förstörs objektet medan `editor_` ligger kvar — use-after-free
    // vid nästa redigering. Frågas i VALIDERINGSFASEN, före all mutation.
    virtual bool canReleaseChild(const Widget&) const { return true; }

    // Tab-stopp? Fokuserbara widgets nås med Tab/Shift+Tab (djupet-först
    // i adopt-ordningen) och aktiveras med Space/Enter;
    // klick flyttar också fokus. Fokusringen ritar widgeten själv
    // (Draw::frame i temats accent). Default false — Label m.fl.
    // hoppas över.
    virtual bool focusable() const { return false; }

    // KOMPOSIT-STOPPET — parallell till
    // `placesChildren()`/`clipsChildren()`: *"jag är ETT tabbstopp, mina
    // barn är inredning"*. En spinner byggd av en `TextInput` och två
    // `Button` ska vara EN Tab-station, inte tre. Tab-vandringen
    // (`AppWindow::focusOrder`) rekurserar därför inte förbi en widget som
    // svarar true.
    //
    // KONTRAKTET, utskrivet därför att det är lätt att läsa in för mycket:
    // det betyder *"jag ERSÄTTER mina barn som tabbstopp"* — INTE *"jag
    // döljer dem"*. Stoppet gäller alltså bara när widgeten SJÄLV blev
    // kandidat (`focusable() && isEnabled()`). En komposit som inte är
    // fokuserbar, eller är inaktiverad, låter sina barn synas som
    // vanligt; annars hade en `!focusable()`-komposit gjort hela sitt
    // underträd otabbbart. Att en DISABLED container skulle inaktivera hela
    // sitt underträd är en ANNAN fråga (ett ancestor-enabled-kontrakt som
    // gäller varje container — en disabled `Panel` har tabbbara barn i
    // dag), och den ingår inte här.
    //
    // KLICKFOKUS ÄNDRAS INTE. Spinnerns inre fält ska kunna få caret av ett
    // klick; det är bara Tab-vandringen som ser kompositen som en enhet.
    //
    // FÖRSTA användaren är `SpinBox` — ett fält och en
    // stegare, ETT stopp. `ListBox` och `Radio`-gruppen är redan ETT stopp
    // via data respektive `focusable()`, och `PropertyGrid`s editorer är
    // dolda utom vid redigering.
    virtual bool focusesChildren() const { return false; }

    // VISAR JAG MINA BARN? — `focusesChildren`s syskon, och
    // ett ANNAT kontrakt: *"mina barn syns inte just nu, fast jag själv
    // gör det"*. Default true. Tab-vandringen (`AppWindow::focusOrder`)
    // rekurserar inte in i en widget som svarar false — oavsett om widgeten
    // själv är ett tabbstopp, och det är skillnaden mot komposit-stoppet
    // ovan, som bara verkar när widgeten själv blev kandidat.
    //
    // Till för en container som GÖMMER innehåll utan att gömma sig själv:
    // dockningspanelen som är en inaktiv flik eller fälld in till ett
    // handtag. Den placeras i en tom rekt och klipper sina barn, så de
    // varken ritas eller träffas — men utan kroken hade Tab gått in i en
    // panel ingen ser. `hide()` är inget alternativ där: dold betyder
    // STÄNGD för en panel, och `isVisible()` hade fått två betydelser.
    //
    // Kroken frågas bara av Tab-vandringen. Ritning och träff följer
    // geometrin (den tomma rekten och klippet).
    virtual bool showsChildren() const { return true; }

    // DET FRÄMRE BARNET — barnet som RITAS SIST och TRÄFFAS
    // FÖRST, oavsett barnordningen. nullptr = inget (förval). Barnordningen
    // är z-ordningen och ingen API flyttar om den; det här är vägen för en
    // container som ibland måste visa ett barn OVANPÅ de andra utan att röra
    // ordningen — dockningsvärdens öppna overlay (en panel som glider fram
    // över huvudytan), en utfälld låda. Ett svar som inte är ett av mina
    // barn ignoreras. Frågas av ritvandringarna (`AppWindow::drawTree`/
    // `drawTreePartial`, previewns `drawPreviewTree`) och träffprovet
    // (`widgetAtRec`); Tab-ordningen följer fortfarande barnordningen.
    virtual Widget* frontChild() const { return nullptr; }

    // FOKUSOMBUDET — den widget som TAR EMOT
    // tangentfokus i mitt ställe. Default nullptr = jag själv.
    //
    // Komposit-stoppet ovan gjorde kompositen till EN Tab-station, men
    // stannade där: fick kompositen fokus hamnade tangenterna i KOMPOSITEN
    // och inte i dess fält, och fältets caret ritas bara när FÄLTET har
    // fokus. Talfältet visade hålet. Med ett ombud flyttar
    // `AppWindow::setFocus` fokus vidare dit — vid Tab, vid ett klick på
    // kompositens egen yta (stegaren) och vid `focus()` — precis som Qts
    // `setFocusProxy` och WinForms aktiva kontroll i en UserControl.
    //
    // KONTRAKTET: ombudet måste vara en ÄTTLING till mig. Det är både det
    // enda som betyder något (fokus inuti kompositen) och det som gör
    // kedjan ändlig — ett ombud som inte är en ättling ignoreras, så en
    // felkonfigurerad cykel kan aldrig hänga `setFocus`. Fokuserat ombud
    // gör att JAG inte står som fokuserad (`focused()` är ombudets);
    // Tab-vandringen hittar ändå rätt plats, för den letar uppåt från det
    // fokuserade till närmaste stopp (`AppWindow::nextFocus`).
    virtual Widget* focusProxy() const { return nullptr; }

    // Slutlig rektangel i fönstret (fysiska px), löst ur dock/anchors/x/y.
    gfx::Rectf resolve(const Ctx& c) const;

    // Ytan ett ICKE-DOCKAT barn får: `c.area` minus ALLA SYNLIGA dockade
    // syskons remsor (se Dock-kommentaren ovan). STATELESS — inget läses
    // eller skrivs utanför argumenten.
    //
    // `except` utesluter en widget ur vandringen (typiskt den som frågar)
    // och FÅR vara null. Nullfallet är inte en bekvämlighet utan ett krav:
    // GlazeBuilder räknar en placerings koordinater INNAN preview-widgeten
    // existerar (`appendNode` → `setAdoption` → skriv `x`/`y` →
    // `rebuildPreview`), så det finns inget `this` att utesluta där.
    //
    // PUBLIK STATISK BASMEDLEM och inte en fri funktion — av två skäl som
    // båda är hårda: en fri funktion kan inte både ligga i anonymt
    // namespace och deklareras här (olika linkage), och den når inte
    // `contentSize()` som är `protected`. Prejudikatet är `measure()`
    // nedan, som finns av exakt samma skäl. PUBLIK eftersom
    // GlazeBuilder måste invertera koordinaterna med samma uträkning som
    // layouten använder.
    static gfx::Rectf carveSiblingDocks(const Widget& parent,
                                        const Widget* except, const Ctx& c);

    // Deltar widgeten i förälderns DOCK-FLÖDE? Default true.
    //
    // `TopBar` svarar **false**, och det är inget undantag för
    // bekvämlighets skull: namnlisten resolvas mot ett EGET chrome-Ctx
    // (hela klientytan) medan menyrad och statusrad skär i INNEHÅLLSytan,
    // alltså en annan rymd. Utan hakan hade menyradens remsa tryckt ner
    // namnlisten med `menuRowH` — en widget i en area som inte är dess
    // egen. TopBar är redan särhanterad i alla fyra trädvandringarna
    // (ritning, damage, hit-test, `ctxFor`); det här är den femte platsen
    // och den enda som ligger i `Widget` självt.
    // INSTANS och inte TYP — en virtuell form vore fel:
    // undantaget gäller `AppWindow::topBar_`, som får ett
    // eget chrome-Ctx, INTE klassen `TopBar`. En vanlig nästlad TopBar (som
    // galleriets grupp 8) är ett helt vanligt barn och SKA skäras av sina
    // dockade syskon. En virtual hade dessutom lovat en generell mekanism
    // som inte finns: en användarsubklass som svarar false får ingen
    // chrome-Ctx, bara oskuren föräldrarea.
    //
    // Fältet sätts av AppWindow (som är `friend`) på exakt den ena
    // instansen, och har ingen publik setter med flit.
    bool inDockFlow() const { return inDockFlow_; }

    // -------------------------------------------------- container-krokar
    // Trädets koordinatmodell (EN sanning — ritning, hit-test och damage
    // använder samma vandring): en container ger sina barn en egen area
    // genom att Ctx kopieras och area byts mot childArea. Klipper
    // containern (clipsChildren) klipps både ritning och hit-test mot
    // clipRect. Defaults: barn resolvar mot widgetens egen rect, ingen
    // klippning. (Panel overridar alla tre.) PUBLIKA: GlazeBuilder ritar/
    // hit-testar sitt preview-träd med samma vandring utanför AppWindow.
    virtual gfx::Rectf childArea(const Ctx& c) const { return resolve(c); }
    virtual bool       clipsChildren() const { return false; }
    virtual gfx::Rectf clipRect(const Ctx& c) const { return resolve(c); }

    // Barnklippets HÖRNRADIE i FYSISKA px — parhäst till `clipRect`, och
    // meningslös utan den (0 = fyrkantigt klipp, det som gäller
    // som förval). En container som ritar sin yta rundad MÅSTE
    // override:a den, annars läcker barnen ut i hörnen; radien ska vara
    // KONCENTRISK mot den faktiska ytan, alltså ytans radie minus den
    // inset clipRect gör (se `Panel::clipRadius`).
    //
    // PUBLIK av exakt samma skäl som de tre ovan: GlazeBuilders parallella
    // vandring (`bld::drawPreviewTree`) måste nå den, och därför är
    // `drawAfterChildren` också publik.
    //
    // HIT-TESTEN FÖLJER INTE RADIEN — den följer widgetens REKTANGEL, som
    // allt annat beteende i ramverket (se `AppWindow::widgetAt`). De
    // bortmaskade hörnytorna är alltså träffbara för ett klippt barn. Det
    // är en SKRIVEN GRÄNS, inte en bugg: att runda
    // hit-testen på ett enda ställe hade brutit en konsekvent regel.
    virtual float clipRadius(const Ctx&) const { return 0.0f; }

    // Ritas EFTER barnen (ovanpå dem), i widgetens EGET Ctx — overlay-
    // detaljer på containrar (Panels rullningslister). Klipps av
    // förälderns klipp som vanligt men INTE av widgetens eget barnklipp.
    //
    // PUBLIK av EXAKT samma skäl som de tre ovan: `AppWindow::drawTree`
    // avslutar med kroken, och GlazeBuilders parallella vandring måste nå den
    // — annars syns en Panels overlay-remsor i den byggda appen men
    // aldrig i builderns canvas. Subklassernas override får förbli
    // `protected`: åtkomsten prövas mot den STATISKA typen `Widget`.
    // Anropas INTE när widgetens rekt är tom (se `draw()`).
    virtual void drawAfterChildren(const Ctx&) {}

    // BILDENS STEG — fönstret anropar den EN gång per bild för
    // varje synlig widget, FÖRE skadeinsamlingen och oavsett skadan och ytan
    // (previewn före `draw()`): det tillstånd en container håller i takt med
    // bilden (dockningens modellsynk, en väntande flytt, glidningens postade
    // steg) får inte vänta på att en inaktiv flik får en yta. Den får INGEN
    // Ctx: stegvandringen resolvar ingenting, så den kostar en pekarvandring
    // per bild också i stora träd — det som behöver geometrin hör hemma i
    // `draw()`. Ska vara billig och idempotent och får INTE invalidera utan
    // en ändring — den körs varje bild, så en ovillkorlig invalidering är en
    // render-loop (regel 1). Intern krok — en widget med bara ritning behöver
    // den aldrig. PUBLIK av `drawAfterChildren`s skäl.
    virtual void frameStep() {}

    // En container som PLACERAR sina barn själv (Bar-flexen): resolve()
    // frågar föräldern först och barnets egna dock/anchors ignoreras då.
    // Stateless som dock-carvingen — placeChild räknas fram per anrop mot
    // c.area (= containerns childArea). placeChild anropas BARA när
    // placesChildren() är true (paret följer clipsChildren/clipRect-mönstret).
    virtual bool       placesChildren() const { return false; }
    virtual gfx::Rectf placeChild(const Widget&, const Ctx&) const
    {
        return gfx::Rectf{};
    }
    // Placerar containern JUST DET HÄR barnet? Förvalet är
    // `placesChildren()` — alla eller inga. En container som placerar BARA
    // vissa (ritytans förankrade barn) svarar per barn, och de andra går
    // dock/anchors-vägen som vanligt. `resolve()` frågar den här.
    virtual bool placesChild(const Widget&) const { return placesChildren(); }

    // Flex-vikt (Bar-flexen): hur stor andel av barens LEDIGA utrymme
    // barnet suger upp utöver sin mätta bredd. 0 = ingen tillväxt
    // (default); Spacer överrider med sin grow-vikt. Flera växande barn
    // delar luckan efter vikt.
    virtual float flexGrow() const { return 0.0f; }

    // Begär omritning av ENBART den här widgeten (per-widget-damage:
    // unionen av senast ritade ytan och den nya). Anropas när widgetens
    // eget tillstånd ändrats utanför ramverkets vägar — t.ex. en timer.
    // OBS: en handler för en DISKRET händelse (ett klick, en tangent, ett
    // val) behöver aldrig kalla detta; efter den ritar AppWindow om hela
    // fönstret (handlers får mutera vilka fält som helst). De LÖPANDE
    // händelserna gör det inte: ett drags rörelser (reglagets `onChange`)
    // och — ett medvetet kontraktsbrott —
    // ritytans `onTick`, `onCameraChange`, hjul och gest, efter vilka bara
    // RITYTAN ritas om. Där ändrar en handler en annan widget genom dess
    // sättare (som invaliderar den), och den som ändrat ett eget fält som en
    // annan widgets ritning läser (en annan ritytas `onPaint`) anropar den
    // widgetens `invalidate()`. Fönster-invalidate (AppWindow::invalidate) =
    // full omritning. Stegar också widgetens `damageStamp()` (se den).
    void invalidate();

    // LIVSTECKNET — en svag referens som går ut när widgeten dör, och
    // grunden för `HandlerGuard` nedan, som är den fråga kod efter en handler
    // eller en nästlad pump (`FilePicker::browse`) ska ställa. Skapas LAT vid
    // första frågan — de flesta widgets frågar aldrig och bär då ingen
    // allokering. PUBLIK: den som minns en widget den inte äger
    // (fönstrets standardknapp, ritytans förankrade barn) läser pekaren bara
    // medan tecknet lever.
    std::weak_ptr<char> lifeToken();

    // Rita widgeten mot en godtycklig Ctx. Används av GlazeBuilder för
    // förhandsvisning — appar behöver aldrig anropa detta själva.
    void render(const Ctx& c)
    {
        if (visible_)
            draw(c);
    }

    // Widgetens fönster (null före adoption, och null när fönstret DÖTT —
    // en lånad widget kan överleva fönstret den satt i, se `ownerLife_`) —
    // WinForms FindForm-motsvarigheten. Behövs av widgets som äger POPUP-fönster
    // (Dropdownens lista visas med `show(*appWindow())`); subklasser når
    // inte AppWindows friendship, därför en accessor i stället för rå
    // `owner_`-åtkomst.
    //
    // PUBLIK, av samma skäl som `behaviorCtx()`
    // nedan: `Menu::popupAt(host, …)` visar sitt popupfönster ÄGT av
    // värdens fönster och måste alltså kunna fråga vilket det är, utan
    // att vara en subklass till värden.
    AppWindow* appWindow() const { return liveOwner(); }

    // Ctx för beteende UTANFÖR event-flödet (öppna en lista
    // programmatiskt) — SAMMA `ctxFor`-vandring som event-krokarna får;
    // rå `makeCtx` ger fel koordinater inne i en panel (fallgropslistan).
    //
    // PUBLIK, och skälet är skrivet: `Menu` är
    // ingen widget och popupas på en VÄRD utifrån
    // (`Menu::popupAt(host, x, y)`). Den måste MÄTA sina rader innan
    // fönstret visas — en xdg-popup får sin storlek vid mappningen och
    // kan inte växa efteråt — och når som icke-subklass ingen skyddad
    // yta på värden.
    //
    // PRECONDITION (utskriven eftersom fler anropar
    // den): widgeten måste sitta i ett ÖPPET fönster. Renderern och
    // fonten skapas först i `show()`, och Ctx:t byggs ur dem.
    Ctx behaviorCtx() const;

    // ------------------------ STORLEKEN OCH PUNKTEN I FÖNSTRET
    //
    // Frågor UTANFÖR ritningen, utan `behaviorCtx`s precondition: utan ett
    // ÖPPET fönster med font svarar de tomt ({0, 0} respektive nullopt).
    //   actualSize  widgetens storlek i LOGISKA px, ur samma `resolve` som
    //               ritningen (WPF `ActualWidth`). Roten är fönstrets yta
    //               under namnlisten. `onResize` rapporterar den.
    //   toWindow    en punkt i widgetens LOGISKA lokala px (`MouseArgs::local`)
    //               → FYSISKA px i fönstrets klientyta — `ContextMenuArgs`,
    //               `DragArgs` och `Menu::popupAt`s rymd:
    //               `if (auto p = w.toWindow(e.local)) meny.popupAt(w, p->x, p->y);`
    //   fromWindow  inversen — exakt den formel `MouseArgs::local` räknas med,
    //               så `toWindow(e.local)` är trycket.
    gfx::Sizef                 actualSize() const;
    std::optional<gfx::Pointf> toWindow(Point local) const;
    std::optional<Point>       fromWindow(gfx::Pointf window) const;

    // TAR WIDGETEN TEXT? — sann för de widgets som tar emot skrivtecken
    // (`TextInput`, `TextArea`). PUBLIK:
    // "har ett textfält i fönstret fokus" är
    // `w->focusedWidget() && w->focusedWidget()->wantsTextInput()`. Fönstret
    // läser den också för textregeln (ett tecken i ett textfält når ingen
    // genväg) och för att slå på OS:ets textinmatning.
    virtual bool wantsTextInput() const { return false; }

protected:
    // Storlek i LOGISKA px; default `{width, height}` rakt av. Widgets med
    // auto-storlek (Label, Checkbox) mäter sin text här.
    //
    // ENHETEN ÄR LOGISK: ingen implementation multiplicerar med `c.scale`
    // själv. Ritvägen är logisk (`Draw`-koordinaterna är widget-relativa
    // logiska px, `Ctx::bounds()` likaså), så en widget-fil har EN
    // koordinatrymd, och `Draw::measure`/`lineHeight`/`imageSize` — som
    // svarar LOGISKT — används som de är i mätvägen.
    //
    // SKALNINGEN SKER HOS KONSUMENTEN. `Widget::resolve` och
    // `Bar::placeChild` multiplicerar med `c.scale` när de möter `c.area`,
    // som fortsatt är FYSISK. `measure()` nedan är en RÅ FÖRMEDLARE och
    // svarar därför också logiskt — att låta den multiplicera själv hade
    // gjort mätvägen tvåspråkig igen.
    //
    // KVAR I FYSISKA PX (avsiktligt, och inte en glömska): `resolve()`,
    // `paintBounds()`, `childArea()`, `clipRect()`, `Ctx::area`/`box` och
    // BETEENDEKROKARNA (mouseDown/mouseDrag/mouseWheel/caretBlink) — de
    // talar fönsterkoordinater. Bytet gäller `contentSize`-kroppar.
    //
    // KONTRAKTET FÖR EN **DOCKAD** WIDGET: den
    // mäts mot förälderns OSKURNA context, och **remstjockleken får inte
    // bero på `c.area` längs SKÄRAXELN** —
    //
    //     Left/Right  ⇒  `size.w` oberoende av `c.area.w`
    //     Top/Bottom  ⇒  `size.h` oberoende av `c.area.h`
    //
    // Beroende av TVÄRaxeln är legitimt (en Top-rad vars höjd beror på
    // tillgänglig bredd) — och det går då genom
    // `heightForWidth` nedan, med den bredd remsan FÅR, och inte genom
    // `c.area`. För `Fill` är kravet irrelevant — den förbrukar
    // ingen tjocklek. Bryts kontraktet blir syskonens layout beroende av
    // i vilken ordning frågorna ställs, och det finns ingen fixpunkt att
    // konvergera mot. `layout_gate` mäter kontraktet med två areor på
    // skäraxeln plus en anropsräknare mot de förväntade värdena.
    //
    // (Därtill gäller regeln "läs aldrig `c.box`": mätvägen körs INNE i
    // `resolve()`, innan boxen finns.)
    virtual gfx::Sizef contentSize(const Ctx& c) const;

    // HÖJD FÖR BREDD (Qt:s `heightForWidth`) —
    // min höjd när layouten GER mig bredden `width`. LOGISKA px in och ut,
    // som `contentSize`; `std::nullopt` = "min höjd beror inte på bredden"
    // (basens svar, och de flesta widgets).
    //
    // `contentSize` frågas INNAN `resolve()` räknat fram en sträckt eller
    // dockad slutbredd — det är mätordningen, och den är tillståndslös. En
    // radbruten text som får sin bredd av layouten hade därför bara kunnat
    // mätas som EN rad. Kroken är det andra steget: frågad BARA med en bredd
    // layouten faktiskt GAV —
    //   * gren 2 i `resolve`, när widgeten är STRÄCKT vågrätt (båda kanterna
    //     bundna) men inte lodrätt — `heightForWidth(c, pw / c.scale)`;
    //   * en Top/Bottom-DOCKAD widget, med remsans bredd (förälderns yta
    //     minus remsorna före den) — i dock-uppspelningen OCH i
    //     `carveSiblingDocks`, så syskonen ser samma remsa.
    // Inte i en `Bar` (baren ger höjden), inte för Fill/Left/Right (ytan ger
    // höjden) och inte i `childrenExtent(c)` (där är bredden obestämd —
    // skriven gräns: en sträckt radbruten etikett i en komposit med
    // autohöjd räknas som en rad) — UTOM i `childrenExtent(c, bredd)`
    // i vilken bredden är given: där tar Top/Bottom/
    // Fill-remsorna kroken med sin remsbredd, och varje odockat barn med två
    // KÄNDA x-kanter med sin bredd — spänt mot förälderns högerkant eller ett
    // syskon från slutet, eller ett fast intervall.
    //
    // KONTRAKTET: läser aldrig `c.box` (den körs inne i `resolve()`), och en
    // SATT `height` vinner — axelregeln — så kroken svarar då nullopt. Bara
    // `Label` (med `wrap`) svarar i ramverket; en egen widget når kroken
    // precis som `contentSize`.
    virtual std::optional<float> heightForWidth(const Ctx& /*c*/,
                                                float /*width*/) const
    {
        return std::nullopt;
    }

    // HAR JAG EN ÖPPEN POPUP? (borttagningens deny-check.)
    // Default false; `Dropdown` overridar med `isOpen()`. Frågas REKURSIVT
    // över hela det delträd som ska tas bort, aldrig bara på roten: en
    // Dropdown kan sitta godtyckligt djupt i det som lossas.
    //
    // Skälet att den finns i stället för att borttagningen skulle STÄNGA
    // popupen: stängning är uppskjuten till pump-svansen, så en synkron
    // stängning härifrån hade dragit in hela `App`s fönsterlivscykel i ett
    // widget-pass. Nekandet ÄR hanteringen.
    //
    // TANGENTVÄGEN frågar den också: Return och Escape som ingen tog
    // erbjuds den första widget i
    // fönstrets träd som svarar true — via dess `keyDown`, genom fokusombudet
    // som om den haft fokus — FÖRE fönstrets standard- och avbrytknapp. En
    // widget som svarar true ska alltså ta de två tangenterna åt sin popup.
    virtual bool hasOpenPopup() const { return false; }

    // AUTOSTORLEK UR BARNEN — OPT-IN för
    // komposit-författare, i LOGISKA px. En komposit som vill vara precis
    // så stor som sin inredning kräver anropar den ur sin EGEN
    // `contentSize()`, per axel enligt axelregeln:
    //
    //     gfx::Sizef s{width, height};
    //     const gfx::Sizef e = childrenExtent(c);
    //     if (s.w <= 0.0f) s.w = e.w;
    //     if (s.h <= 0.0f) s.h = e.h;
    //
    // `GroupBox` gör det; `Panel`, `ListBox` och `PropertyGrid` har SKRIVNA,
    // 0×0-beslut. Fönstret som följer sitt innehåll
    // (`AppWindow::setSizeToContent`) räknar sin rot med SAMMA kärna.
    //
    // REGELN ÄR EN ORDNINGSKÄNSLIG INVERS AV DOCK-CARVINGEN, och det är
    // det som gör den icke-trivial. Den naiva formen — "summan av
    // remstjocklekar per skäraxel" — är FEL: ett ensamt `Dock::Top`-barn
    // med måttet 100×20 hade gett `0×20`, fast kompositen behöver 100×20.
    // En remsa bär ett TVÄRKRAV. Därför:
    //
    //   need = extenten från ODOCKADE barn (de bor i "resten")
    //   för varje synligt DOCKAT barn i OMVÄND barnordning:
    //     Fill:          need = max(need, mätt)          // vid sin position
    //     Top | Bottom:  need.h += mätt.h; need.w = max(need.w, mätt.w)
    //     Left | Right:  need.w += mätt.w; need.h = max(need.h, mätt.h)
    //
    // **`Fill` behandlas VID SIN POSITION** i baklängespasset och samlas
    // ALDRIG i botten: `carveDock` säger att Fill tar resten UTAN att
    // förbruka den, så ett `Fill` FÖRE en remsa får hela den då-aktuella
    // arean och remsan efter överlappar det. Följden är att ordningen
    // BETYDER något: `Left 30×10, Fill 50×40` ⇒ **80×40**, medan
    // `Fill 50×40, Left 30×10` ⇒ **50×40**. Samma barn, olika svar.
    //
    // DE ODOCKADE: KANTER ÅT BÅDA HÅLLEN. Varje
    // odockat barn i flödet får per axel en NÄRKANT (vänster/över) och en
    // FJÄRRKANT (höger/under), och varje kant räknas FRÅN BÖRJAN (en
    // koordinat från min vänster-/överkant), FRÅN SLUTET (ett avstånd inåt
    // från min höger-/nederkant) eller är OKÄND. Reglerna speglar `resolve`,
    // och syskonet vinner på den kant där båda finns:
    //   · `anchors.left/top` ger närkanten från början (`x`/`y`),
    //     `anchors.right/bottom` fjärrkanten från slutet (`right`/`bottom`).
    //   · `anchorLeft/Top = {mål, g}`: närkanten = målets fjärrkant + g (från
    //     början) eller − g (från slutet); `anchorRight/Bottom` spegelvänt mot
    //     målets närkant. SYSKONANKARE FÖLJS alltså — en kedja `a → b → c` ger
    //     rätt totalmått, och `earlierSibling` kan bara peka BAKÅT, så målet
    //     är alltid färdigräknat när barnet nås.
    //   · En bunden kant: den andra är ± måttet, av samma sort. Två av samma
    //     sort: ett fast intervall. Början + slut: SPÄND. Ingen: MITT.
    // Bidragen per axel, bara från SYNLIGA barn:
    //   · fjärrkant från början:  behov ≥ fjärrkanten + LUFTEN (nedan)
    //   · närkant från slutet:    behov ≥ närkanten
    //   · SPÄND:                  behov ≥ närkant + MÄTT mått + fjärrkant —
    //                             måttet är barnets MINSTA (WPF:s desired
    //                             size); en `Panel` utan mått bidrar med 0
    //   · MITT:                   behov ≥ måttet (positionen beror på min
    //                             storlek, men ytan måste rymma barnet)
    // LODRÄTT ÄR DET TVÅ FLÖDEN: höjden ≥ max(fjärrkant + luft) över raderna
    // från början PLUS max(närkant) över raderna från slutet — allt som står
    // mot nederkanten står UNDER allt som flödar uppifrån (en knapprad nere
    // till höger under fälten). I SIDLED läggs bara de par ihop som INTE står
    // över varandra: "Namn" uppe till vänster och en "Hjälp"-länk uppe till
    // höger delar rad (summan), fälten uppifrån och OK nere till höger gör det
    // inte (max). Två barn står över varandra när det ena står i flödet
    // uppifrån och det andra i flödet nedifrån, eller när det enas fjärrkant
    // och det andras närkant är av samma sort och i ordning (en lista bunden
    // ovanför knapparna med `anchorBottom`). Ett MITT-barn skiljs aldrig.
    //
    // LUFTEN PÅ DEN FRIA SIDAN: ett barn bundet BARA i början (vänster/upp,
    // mot mig eller ett syskon) räknar sin `right`/`bottom` som luft efter
    // sig — avståndet till min kant eller till det som står mot slutet.
    // `resolve` läser inte fälten på en fri sida, så de syns bara när mitt
    // mått räknas ur innehållet: `fält.bottom = 12` ger 12 px ovanför
    // knappraden (WPF:s `Margin`, med husets fält). `x`/`y` på ett barn bundet
    // bara mot SLUTET räknas INTE — designern lämnar ett gammalt `y` kvar på
    // en knapp som ankrats om nedåt.
    //
    // ETT DOLT BARN I EN KEDJA LÄMNAR SITT HÅL: kanterna räknas för ALLA
    // odockade i flödet, dolda eller inte, men bara de synliga bidrar — för
    // `resolve` placerar en följare efter hålet (ett dolt mål räknas ändå).
    //
    // DET SOM FÖRBLIR OBESTÄMT (utan bidrag på axeln): ett ankare mot ett
    // DOCKAT syskon (de dockade finns inte i passet), ett OMVÄNT par
    // (närkanten från slutet, fjärrkanten från början — bredden beror på
    // min), och allt som kedjar vidare från en okänd kant. Kedjor från ett
    // MITT-barn räknar med [0, mått] — en approximation. Och företrädet i
    // höjdled förutsätter att höjden RÄCKER: räknas bara bredden (`Width`
    // ensamt, eller en komposit med satt höjd) kan raderna mot nederkanten
    // överlappa dem uppifrån.
    //
    // Läser ALDRIG `c.box` och anropar ALDRIG barnens `resolve()` — det
    // vore cirkulärt, eftersom barnens area härleds ur min egen rect.
    // Mäter enbart via `measure()` och layoutfälten.
    gfx::Sizef childrenExtent(const Ctx& c) const;

    // MED BREDDEN GIVEN — `width` i LOGISKA px
    // är kompositens bredd, känd av den som frågar (ett fönster som följer
    // innehållet i höjd). Samma regel som ovan, men höjden kan nu bero på
    // bredden: varje Top/Bottom- och Fill-remsa tar `heightForWidth` med
    // sin remsbredd (bredden minus Left/Right-remsorna FÖRE den, i
    // barnordning — dock-uppspelningens), och varje odockat barn med två
    // KÄNDA x-kanter tar den med sin bredd: SPÄNT (restbredden minus
    // närkanten och fjärrkanten — mot min högerkant eller ett syskon från
    // slutet) eller ett FAST INTERVALL (avståndet mellan kanterna). Allt
    // annat mäts som i `childrenExtent(c)`.
    gfx::Sizef childrenExtent(const Ctx& c, float width) const;

    // Mät en godtycklig widget — containers behöver barnens mått (Bar-
    // flexen). Statisk basmedlem eftersom C++ inte låter en subklass nå
    // skyddade medlemmar via en Widget-referens direkt.
    //
    // RÅ FÖRMEDLARE: svarar i samma enhet som `contentSize`, alltså
    // LOGISKA px. Anroparen multiplicerar med `c.scale` när svaret ska
    // möta `c.area` (`Bar::placeChild` gör det, `Widget::resolve` gör det).
    static gfx::Sizef measure(const Widget& w, const Ctx& c)
    {
        return w.contentSize(c);
    }

    // Har widgeten, eller någon i dess delträd, en ÖPPEN POPUP
    // (`hasOpenPopup()`)? Statisk basmedlem och inte en fri funktion:
    // `hasOpenPopup()` är protected, och en fri funktion når den inte (samma
    // skäl som `measure`). PROTECTED — dockningsvärdens
    // overlay får inte fällas in medan en lista eller meny i den är öppen
    // — liksom borttagningsvägen (`validateRelease`).
    static bool subtreeHasOpenPopup(const Widget& w);

    // RIVNINGSVÄGEN — släpp
    // barnet `w` UR MIN LISTA utan att röra dess DELTRÄD: ingen
    // fönsterstädning, inga krokar, ingen invalidering. LEVER barnet
    // (`alive`) nollas också dess EGEN förälderlänk och fönsterpekare — men
    // inte ättlingarnas, för bland dem kan lånade barn redan ha dött; ett
    // dött barn derefereras inte alls (pekaren jämförs bara). Ett ÄGT barn
    // lämnas ut, så anroparen bestämmer när det förstörs. För en container
    // som rivs medan ett barn vars livstid den inte äger kanske redan dött —
    // dockningsvärdens flytande fönster. ALDRIG en driftsväg: den lämnar
    // ättlingarnas fönsterpekare stående tills barnet adopteras igen.
    std::unique_ptr<Widget> severChild(Widget* w, bool alive);

    // `measure`s PARHÄST för `heightForWidth` — samma skäl (en subklass når
    // inte en annan widgets skyddade krok), samma enhet (logiska px).
    static std::optional<float> measureHeightForWidth(const Widget& w,
                                                      const Ctx& c,
                                                      float width)
    {
        return w.heightForWidth(c, width);
    }

    // Tillstånd -> färgROLL. De två kombinationerna varenda kontroll gör
    // likadant: text som dämpas när widgeten är inaktiverad, och en kant
    // som tänds i accentfärgen vid hover. Färgerna kommer ur temat (aldrig
    // hårdkodade) — det här är bara valet mellan rollerna, och det bor på
    // basen så en EGEN widget får det gratis.
    gfx::Color textColor(const Ctx& c) const
    {
        return isEnabled() ? c.theme->colors.text : c.theme->colors.textDim;
    }
    gfx::Color hotBorder(const Ctx& c) const
    {
        return hovered() && isEnabled() ? c.theme->colors.accent
                                        : c.theme->colors.border;
    }

    // Ytan widgeten faktiskt RITAR på (fysiska px): resolve() uppblåst med
    // stilens `overdraw`-marginal (skugga, fokusring). Damage-beräkningen
    // utgår från den här — en egen widget som ritar ännu längre utanför
    // sin rect överrider och utökar, annars lämnas spökpixlar kvar.
    virtual gfx::Rectf paintBounds(const Ctx& c) const;

    // DEN TOMMA REKTEN (kontrakt): `draw()` anropas inte när
    // widgetens rekt är tom — bredden eller höjden ≤ 0, som en dold
    // överflödsknapp, en inaktiv flik eller en infälld dockningspanel får.
    // Barnen vandras ändå. Lägg alltså inget tillstånd här som måste stegas
    // utan yta (det hör hemma i `frameStep`); det som stegas per BILD — en
    // ritytas tick, en obestämd förloppsindikator — pausar med flit (regel 1).
    virtual void draw(const Ctx& c) = 0;

    // WIDGETENS OVERLAY-LAGER — det som ändras oftare än
    // innehållet och inte ska kosta en omritning av det: ritytans gummiband
    // och HUD (`onPaintOverlay`). Fönstrets overlay-lager ritas efter
    // compositen, direkt i backbuffern, vid VARJE present och är
    // självstädande (`AppWindow::overlay`-kontraktet): rita och glöm. En
    // widget som flyttat sitt gummiband begär bara en ny present —
    // `appWindow()->invalidateOverlay()`, ingen damage.
    //
    // Svarar `hasOverlay()` true anropar fönstret `drawOverlay` för varje
    // present, efter appens `overlay()` och före drag-återkopplingen, i
    // trädordning (ett senare syskons lager överst). Ctx:t är exakt `draw()`s
    // — `box` är widgetens rect, `Draw::` ritar i widgetens logiska px — och
    // klippet är FÖRFÄDERNAS, med samma rektanglar och radier som
    // ritvandringen använder, inom ytan trädet ritas i (innehållsytan;
    // klientytan för namnlistens). Widgetens egen rect klipper INTE: den som
    // bara vill rita inom sig gör `Draw::clip` själv. En dold widget, eller
    // en i ett dolt delträd, frågas inte, och en med tom rekt (se `draw()`)
    // får inget `drawOverlay`.
    //
    // REN RITNING som `draw()`: inget tillstånd ändras, ingen invalidering —
    // lagret ritas om vid varje present, och en ändring i det skulle
    // annars mata sig själv.
    virtual bool hasOverlay() const { return false; }
    virtual void drawOverlay(const Ctx&) {}

    virtual void clicked()
    {
        if (onClick) {
            ClickArgs e;
            onClick(this, e);
        }
    }

    // Hjul över widgeten: routas till widgeten under pekaren och bubblar
    // via parent()-kedjan tills någon konsumerar (true). Ctx:t är
    // widgetens containers (samma som vid ritning). (x, y) är PEKAREN i
    // fysiska fönsterkoordinater, som de andra mus-krokarnas — hjulhändelsen
    // bär den (ritytans zoom mot pekaren). wheelX/wheelY i rader.
    //
    // TVÅ SORTERS KONSUMENTER, och regeln skiljer dem:
    // det som RULLAR (Panel, listorna, tabellen, trädet, det
    // flerradiga fältet) tar hjulet under pekaren utan fokus och bubblar i
    // kanten, medan en
    // VÄRDEWIDGET (Slider, SpinBox) tar det BARA MED FOKUS — utan fokus
    // svarar den false, så att en sida som rullas förbi den aldrig ändrar
    // ett värde av misstag. En egen värdewidget följer samma regel.
    virtual bool mouseWheel(const Ctx&, float, float, float, float)
    {
        return false;
    }
    // Hjulet har två sorter: ett MUSHJUL och STYRPLATTANS
    // TVÅFINGERSDRAG, vars rader är logiska px / `gfx::kPrecisePxPerRow` —
    // så den här kroken rullar lika långt för båda. Den som vill skilja dem
    // (ritytan panorerar på draget och zoomar på hjulet) läser sorten ur
    // `appWindow()->isPreciseWheel()` — `modifierKeys()`s mönster, ingen ny
    // parameter i kroken.
    //
    // RADER → RÖRELSE går genom två hjälpare, så styrplattan beter sig
    // likadant i varje widget. Det som RULLAR tar `wheelScrollPx(c, rader,
    // steg)`: ett mushjul flyttar `steg` logiska px per rad (widgetens egen
    // enhet — Panels `wheelStep`, listans radhöjd), medan styrplattans rader
    // ÄR px, så innehållet följer fingrarna 1:1. Det som STEGAR (reglaget,
    // talfältet, kalendern) tar `wheelNotches(c, rader)`: mushjulets rader
    // rakt av, styrplattans omräknade så att `wheelStep` px fingerrörelse är
    // ett klick — annars bläddrar ett svep på en decimeter tio månader.
    float wheelScrollPx(const Ctx& c, float rows, float stepPx) const;
    float wheelNotches(const Ctx& c, float rows) const;
    // FÖLJER WIDGETEN RULLNINGENS RIKTNING? — true:
    // fönstret byter Shift+mushjul till sidled och vänder raderna efter
    // `isWheelInverted` innan kroken ovan får dem. false: kroken får det RÅA
    // hjulet och läser modifierarna själv — ritytan, som har sina egna
    // bindningar, och en egen widget med en egen hjultolkning. Tar en sådan
    // widget inte hjulet bubblar det, och föräldern får SIN vändning.
    virtual bool followsWheelDirection() const { return true; }

    // STYRPLATTANS NYPNING OCH VRIDNING — en SEKVENS Begin →
    // Update… → End (eller Cancel). `scale` är den MULTIPLIKATIVA ändringen
    // sedan förra anropet (> 1 = isärdragna fingrar = zooma in), `rotationDeg`
    // grader sedan förra, POSITIVT = MEDURS på skärmen; det ena är 1
    // respektive 0 när steget bara bär det andra. (x, y) är PEKAREN i fysiska
    // fönsterkoordinater — den står still under en gest och är dess ankare.
    //
    // MÅLET väljs vid Begin som hjulets: widgeten under pekaren och sedan
    // förfäderna (`parent()`-kedjan till men inte med fönstrets rot),
    // inaktiverade hoppas över, och den FÖRSTA som svarar true HÅLLER
    // sekvensen — Update och End/Cancel kommer sedan till den, var pekaren än
    // står, tills End/Cancel. Svarar ingen händer ingenting. Bara svaret på
    // Begin räknas; senare steg svarar vad som helst. En widget som
    // inaktiveras under sekvensen får inga fler Update men End/Cancel
    // (`middleUp`s regel), och en som lämnar trädet släpps tyst
    // (`detachTree` — ingen Cancel under en trädmutation). En widget ser
    // alltid sekvensen BÖRJA med Begin: AppWindow levererar en Update utan
    // hållen sekvens som en Begin.
    //
    // Ingen användar-handler i kroken självt (hjulets regel): widgeten
    // invaliderar sitt eget tillstånd, och en som ändå kör en handler
    // bestämmer vad som ritas om efter den — gesten är en LÖPANDE händelse,
    // så ritytans `onGesture` och `onCameraChange` ritar om ritytan och inte
    // fönstret (`invalidate()` ovan), medan en widget vars
    // handler kan ha ändrat vad som helst ber fönstret om full omritning
    // (`appWindow()->invalidate()`). Ctx:t är `ctxFor`s.
    virtual bool gesture(const Ctx&, float, float, gfx::GesturePhase, float,
                         float)
    {
        return false;
    }

    // Tangent till FOKUSERAD widget — routas av AppWindow efter appens
    // event()-hook och efter fokusenhetens `onKeyDown`-handlare (ingen av dem
    // tog den). true = konsumerad (då provas inte Tab-vandringen); AppWindow
    // antar att något ändrats och begär omritning.
    virtual bool keyDown(gfx::Key, std::uint32_t) { return false; }

    // EN TANGENT SOM EN ÄTTLING TACKADE NEJ TILL.
    // Tangenten går till den FOKUSERADE widgeten och BUBBLAR INTE genom
    // `keyDown` — en container som fick sina barns tangenter i sin egen
    // `keyDown` hade ändrat vad varje befintlig komposit gör (datagridens
    // PageDown medan en cell redigeras hade flyttat den aktuella cellen).
    // Det här är en ANNAN fråga, ställd EFTER att den fokuserade widgeten
    // svarat false: AppWindow frågar dess FÖRFÄDER, närmast först, och den
    // första som svarar true tar tangenten. Först därefter kommer menyernas
    // acceleratorer och Tab-vandringen — fokus närmast vinner, samma
    // kollisionsregel som acceleratorerna redan har.
    //
    // Första användaren är `TabControl`: Ctrl+Tab byter flik var i sidan
    // fokus än står (WinForms `ProcessKeyPreview`, Qt:s uppbubblande
    // tangent). En förfader som är dold eller inaktiverad frågas inte.
    // Tangenten är den VIKTA (numpadens navigationsläge — `keyDown`s).
    virtual bool descendantKeyDown(gfx::Key, std::uint32_t) { return false; }

    // TANGENTSLÄPPET — `keyDown`s parhäst. Anropas EFTER
    // `onKeyUp`-handlarna, om ingen satte `handled`, på den widget i
    // tryckets kedja som TOG tangenten: den vars `onKeyDown`-handler satte
    // `handled`, annars den innersta — den fokuserade, som fick `keyDown`.
    // Också för fokusförlustens släpp (`KeyArgs::cancelled`), som kroken inte
    // kan skilja från ett riktigt: läget ska avslutas ändå. Ett släpp vars
    // tryck inte kom hit frågar den som har fokus nu, så svara false för en
    // tangent du inte känner. true = något ändrades ⇒ fönstret ritas om.
    // Tangenten är den VIKTA, som `keyDown` fick den.
    virtual bool keyUp(gfx::Key, std::uint32_t) { return false; }

    // EN HANDLER TOG MIN TANGENT — min `onKeyDown` satte
    // `handled`, så tangenten nådde aldrig `keyDown`. Anropas för varje
    // KeyDown handlern tar (upprepningarna också), med den VIKTA tangenten,
    // innan fönstret ritas om. Till för det som lever så länge en tagen tangent
    // hålls: ritytans tickning (`Canvas::onTick`), som sedan frågar
    // `AppWindow::isKeyHeld`. Ingen användar-handler här (fokuskrokarnas
    // regel) — handlern har redan kört.
    virtual void keyTaken(gfx::Key) {}

    // Mus-krokar: mouseDown körs vid vänstertryck ÖVER widgeten (efter att
    // fokus/pressed satts), mouseDrag vid musrörelse medan widgeten hålls
    // nedtryckt (även utanför dess yta — auto-capture, förvalet).
    // Koordinater i fysiska px. Ingen användar-handler körs här — widgetens
    // eget tillstånd (t.ex. caret/selektion) invalidateras av widgeten själv.
    virtual void mouseDown(const Ctx&, float, float) {}
    virtual void mouseDrag(const Ctx&, float, float) {}

    // DUBBELKLICKET — det ANDRA vänstertrycket på
    // samma widget inom OS:ets tid (`gfx::doubleClickMs`) och dragtröskeln
    // från det första. Anropas EFTER det andra trycket har gått genom
    // `mouseDown` som vanligt (WinForms och Qt gör likadant), alltså med
    // tillståndet det trycket lämnade — en lista har redan valt raden.
    // Ramverket räknar trycken (`AppWindow`), så varje widget får samma
    // definition av ett dubbelklick; en widget som inte bryr sig ser två
    // vanliga tryck. Samma regler som `mouseDown`: fysiska px, `ctxFor`-Ctx.
    virtual void mouseDoubleClick(const Ctx&, float, float) {}

    // Musrörelse ÖVER widgeten utan knapp (hover-rad i en lista m.m.) —
    // anropas bara på widgeten under pekaren. Hård regel 1: kroken ska
    // bara invalidatera när något faktiskt ändrats (rörelse utan ändring
    // ritar inget).
    virtual void mouseMove(const Ctx&, float, float) {}

    // SLÄPPET — vänsterknappen släpptes, och det här
    // är parhästen till `mouseDown`. Utan den hade släppet bara nått
    // `clicked()`, och bara när pekaren stod kvar över samma widget — så ett
    // skikt som dragits kunde inte committa sitt drag, och en widget med
    // explicit capture fick aldrig veta att knappen släppts.
    //
    // KONTRAKTET: anropas EN gång per tryck som SLÄPPS, på widgeten som TOG
    // EMOT trycket (auto-capture) — och på den som håller EXPLICIT capture,
    // om det är en annan. Det gäller ÄVEN när släppet sker utanför widgeten
    // och ÄVEN när den inaktiverats under trycket: ett påbörjat drag måste
    // kunna avslutas. Körs FÖRE `clicked()`, med `pressed()` redan släckt.
    // Ctx:t är `ctxFor`s, positionen fysiska px som de andra mus-krokarna.
    //
    // ETT TRYCK SOM INTE SLÄPPS får ingen `mouseUp`. Ett AVBRUTET tryck —
    // fönstrets fokusförlust, OS:ets capture-förlust (`MouseCaptureLost`), ett
    // tryck som ett annat av appens fönster tog över (`takePointerCapture` —
    // släppet går dit) och ett nytt vänstertryck innan förra släppet kom —
    // får `mouseCancel(Left)` i stället. Tyst slocknar
    // bara ett tryck vars widget lämnar trädet och ett som blev ett
    // drag-and-drop (källan får `dragEnd`). I alla fallen slocknar
    // `pressed()`, så en widget som visar ett pågående drag ska läsa sitt
    // drag-läge TILLSAMMANS med `pressed()` och aldrig ensamt.
    //
    // SVARET (keyDown-formen): true = släppet ändrade något UTANFÖR
    // widgeten själv — typiskt att den fyrade en egen handler (skiktets
    // `onChange` efter ett drag) — och då ritar AppWindow om hela fönstret,
    // samma garanti som efter ett klick. false = bara eget tillstånd, som
    // widgeten invaliderar själv (regel 1: ett släpp som inte ändrade något
    // ska inte kosta en full omritning).
    virtual bool mouseUp(const Ctx&, float, float) { return false; }

    // MITTENKNAPPEN — ritytans panorering med mittenknappen.
    // Tre krokar som speglar vänsterknappens, med EN skillnad:
    // trycket BUBBLAR som hjulet i stället för att gå till widgeten under
    // pekaren och stanna där.
    //
    // middleDown  trycket. Frågar widgeten under pekaren och sedan dess
    //             förfäder (`parent()`-kedjan till men inte med fönstrets
    //             rot), närmast först; en inaktiverad hoppas över. Den första
    //             som svarar true TAR trycket — och får ensam draget och
    //             släppet, var pekaren än står. Svarar ingen händer
    //             ingenting. Ingen fokusflytt och ingen `pressed()`: att
    //             panorera en yta är inte att trycka på den. Håller en widget
    //             EXPLICIT capture frågas bara den, som för hjulet.
    // middleDrag  musrörelse medan knappen hålls — även utanför widgeten och
    //             fönstret (backenden håller mus-capture), och FÖRE allt
    //             annat musrörelsen gör. Bara när widgeten är aktiverad, som
    //             `mouseDrag`.
    // middleUp    släppet, EN gång, även när widgeten inaktiverats under
    //             trycket (ett påbörjat drag ska kunna avslutas) — med
    //             `middlePressed()` redan släckt, som `mouseUp` har
    //             `pressed()`. Svaret är `mouseUp`s: true = en
    //             användar-handler kördes ⇒ hela fönstret ritas om.
    //
    // ETT TRYCK SOM INTE SLÄPPS får ingen `middleUp` — samma regel som för
    // vänsterknappen: fönstrets fokusförlust, OS:ets capture-förlust och ett
    // nytt mittentryck (det förra släppet tappades) ger `mouseCancel(Middle)`,
    // och att widgeten lämnar trädet (`unadopt`) avbryter tyst. Läs därför ett
    // pågående mittendrag ur `middlePressed()`, aldrig ur en egen flagga ensam.
    //
    // UNDER SIN EGEN `middleDown` står den tillfrågade som PRELIMINÄR mottagare
    // (`middlePressed()` är sann där): öppnar kroken en modal dialog och
    // fönstret tappar fokus kommer `mouseCancel(Middle)` medan kroken ännu kör,
    // och ett tryck som tog slut så återställs aldrig — vad kroken än svarar.
    // En krok som börjar något EFTER en handler frågar `middlePressed()` först.
    //
    // Ingen användar-handler i `middleDown`/`middleDrag` (mouseDown-
    // regeln): widgeten invaliderar sitt eget tillstånd själv, och en som
    // ändå kör en handler ber fönstret om en full omritning
    // (`appWindow()->invalidate()`). Positionerna är fysiska
    // fönsterkoordinater och Ctx:t `ctxFor`s, som för de andra krokarna.
    virtual bool middleDown(const Ctx&, float, float) { return false; }
    virtual void middleDrag(const Ctx&, float, float) {}
    virtual bool middleUp(const Ctx&, float, float) { return false; }

    // HÖGERKNAPPEN — en tredje krokfamilj i
    // MITTENKNAPPENS form, med samma regler rad för rad: `rightDown` BUBBLAR
    // (widgeten under pekaren, sedan förfäderna; inaktiverade hoppas över,
    // roten frågas inte, explicit capture frågar bara den fångande), och den
    // som svarar true får `rightDrag` (bara aktiverad) och `rightUp` (EN gång,
    // också inaktiverad, med `rightPressed()` släckt — svaret är `mouseUp`s)
    // var pekaren än står. Ingen fokusflytt och ingen `pressed()`: ett
    // högertryck flyttar inte fokus i någon app. Avbrottet är
    // `mouseCancel(Right)`, och den preliminära mottagaren under kroken är
    // mittenknappens (`rightPressed()` sann i `rightDown`). Ett tryck som tog
    // slut under kroken ger ingen snabbmeny.
    //
    // SNABBMENYN: ett högertryck som en widget TAR fyrar inte `onContextMenu`
    // — widgeten äger draget och släppet, och den som vill ha en meny efter
    // ett klick utan drag öppnar den själv i sitt släpp (`Menu::popupAt`;
    // tröskeln för vad som är ett drag hör till verktyget). Tar ingen trycket
    // fyras snabbmenyn vid trycket.
    virtual bool rightDown(const Ctx&, float, float) { return false; }
    virtual void rightDrag(const Ctx&, float, float) {}
    virtual bool rightUp(const Ctx&, float, float) { return false; }

    // DUBBELKLICKET MED MITTEN- OCH HÖGERKNAPPEN —
    // `mouseDoubleClick`s regel (samma widget, `gfx::doubleClickMs`,
    // dragtröskeln; trycket efter ett dubbelklick börjar en ny följd), räknad
    // på den widget som TOG trycket (`middleDown`/`rightDown` svarade true) och
    // inte på den under pekaren — trycket bubblar, och en ram runt ritytan
    // hade annars kunnat få dubbelklicket. Anropas efter det andra trycket.
    virtual void middleDoubleClick(const Ctx&, float, float) {}
    virtual void rightDoubleClick(const Ctx&, float, float) {}

    // ETT TRYCK SOM AVBRÖTS (DOM `pointercancel`) — släppet
    // kommer ALDRIG. Varje widget som hade fått ett släpp får beskedet, EN
    // gång per avbrutet tryck: den tryckta `Left`, en ANNAN som håller
    // explicit capture `Left` när vänsterknappen är nere och annars `None`
    // (capturen togs utan knapp), den som tog mittentrycket `Middle` och den
    // som tog högertrycket `Right`. Utlösarna är fönstrets fokusförlust, OS:ets
    // capture-förlust (`gfx::EventType::MouseCaptureLost`), ett annat av
    // appens fönster som tar över trycket (`takePointerCapture`), ett NYTT
    // tryck med samma knapp innan förra släppet kom — och ett släpp som appens
    // `onMouseUp` TOG (`handled`) från den som höll trycket: ett skikt
    // eller ett reglage ska då återgå, inte spara.
    //
    // BOKFÖRINGEN ÄR REDAN NOLLAD: `pressed()`, `middlePressed()`,
    // `rightPressed()` och `hasPointerCapture()` är falska, och inget `…Up`
    // kommer — varken nu eller vid ett senare släpp. Ett drag ska AVBRYTAS
    // här, inte committas: ett avbrott är "avbryt" och ett släpp "spara", två
    // handlingar och därför två krokar. Inget besked när widgeten lämnar
    // trädet (ingen widgetkod under en trädmutation), när drag-and-drop tar
    // över trycket (källan får `dragEnd`), när fönstret stängs, när widgeten
    // själv släpper sin capture — och inte till en fångande som tog capturen
    // igen medan avbrottet pågick.
    //
    // Kroken FÅR köra en användar-handler, och fönstret frågar sitt livstecken
    // och nästa mottagares vakt efter varje anrop. SVARET: true = en handler
    // kördes ⇒ hela fönstret ritas om. Appens `onMouseCancel` fyrar FÖNSTRET
    // efter kroken — en widget fyrar den inte själv.
    virtual bool mouseCancel(gfx::MouseButton) { return false; }

    // ETT TRYCK APPEN TOG — `onMouseDown` satte
    // `handled`, så widgeten får varken `mouseDown`, `middleDown` eller
    // `rightDown` för det. Kroken säger vad som händer med trycket: `Begin`
    // vid trycket (efter handlern), `Move` vid varje musrörelse medan knappen
    // hålls (var pekaren än står, bara när widgeten är aktiverad) och
    // `DoubleClick` när trycket var det andra i en följd (räknat på målet,
    // `mouseDoubleClick`s regel). Släppet är `onMouseUp` och avbrottet
    // `onMouseCancel` — fönstrets. Default ingenting: ett tryck appen tog är
    // inte widgetens. Ritytan levererar sina EGNA apphändelser härifrån
    // (`onMouseMove` och `onDoubleClick` med knappen), så ett verktyg som drar
    // med höger knapp fungerar. Kroken FÅR köra en användar-handler;
    // fönstret frågar sitt livstecken och widgetens vakt efter varje anrop, och
    // en handler som ska rita om hela fönstret ber om det själv
    // (`appWindow()->invalidate()`). Positionen är fysiska px, Ctx:t `ctxFor`s.
    enum class AppPressStep : std::uint8_t { Begin, Move, DoubleClick };
    virtual void appPress(const Ctx&, float, float, gfx::MouseButton, AppPressStep)
    {
    }

    // APPENS MUSHANDLER, ANROPAD: bygg args, kör `h` och svara
    // med `handled`. Fönstret anropar den för `onMouseDown`/`onMouseUp`/
    // `onMouseCancel` när fältet är satt, med `ctxFor`s Ctx och pekaren i
    // fysiska fönster-px (`pos` tom när den inte är känd — ett avbrott efter
    // att pekaren lämnat fönstret). Basen bygger en `MouseArgs`; en widget med
    // rikare args overridar och bygger en ättling (ritytans `CanvasMouseArgs`
    // med världen och strålen) — en handler som väntar sig den körs bara då.
    virtual bool fireMouse(const Handler<MouseArgs>& h, const Ctx& c,
                           std::optional<gfx::Pointf> pos, gfx::MouseButton b);

    // MUSPEKAREN — formen pekaren ska ha över MIG vid
    // (x, y), fysiska fönsterkoordinater som mus-krokarnas. AppWindow frågar
    // vid varje musrörelse och sätter svaret på fönstret
    // (`gfx::Window::setCursor`): den widget som håller explicit capture
    // eller trycket svarar först (ett skikt som dras behåller ↔ fast pekaren
    // hunnit före), annars widgeten under pekaren. En INAKTIVERAD widget
    // frågas inte — den får pilen, som den svarar på allt annat.
    //
    // Default är pilen. Positionen finns för widgets där bara en DEL är
    // greppbar (skiktet i en delare, kanten på en dockad panel), och svaret
    // ska komma ur SAMMA geometri som träffen använder — aldrig två mått.
    // Ren fråga: ingen invalidering, inget tillstånd.
    virtual gfx::PointerShape cursorAt(const Ctx&, float, float) const
    {
        return gfx::PointerShape::Arrow;
    }

    // ------------------------------------ DRAG-AND-DROP-krokarna
    //
    // Widgetens INBYGGDA beteende i ett drag. Varje krok körs FÖRE appens
    // motsvarande handler och får SAMMA args, så det widgeten skriver i
    // `e.data`/`e.effect` är vad appen ser — och får ändra. UNDANTAGET är
    // släppet: `drop` körs EFTER `onDrop`. Ctx:t är
    // widgetens behavior-Ctx (`ctxFor`), positionerna fysiska px som
    // mus-krokarnas. Default gör ingenting, så en vanlig widget med
    // `allowDrop` är ett mål vars HELA beteende är appens handlers.
    //
    // canDragFrom KÄLLA: får ett drag börja från den här punkten — TRYCKETS,
    //            inte där tröskeln passerades? Frågas EN gång, FÖRE
    //            `dragStart` och `onDragStart`; false = trycket förblir ett
    //            tryck (mouseDrag och klicket kommer som vanligt). Ingen
    //            användarkod körs. Default ja; listvyn svarar nej i huvudet,
    //            i kryssrutan och i remsorna, trädet på pilen, i rutan och i
    //            remsan — så en avdelare aldrig blir
    //            ett raddrag bara för att appen fyller lasten.
    // dragStart  KÄLLA: tröskeln passerad. Fyll `e.data` för en inbyggd
    //            last (anropas bara när `allowDrag()` är sann).
    // dragEnd    KÄLLA: utfallet. `Move` till ett ANNAT mål betyder att
    //            posten ska lämna mig.
    // dragEnter/ MÅL: pekaren kom in respektive rör sig över mig. Sätt
    // dragOver   `e.effect` för det inbyggda svaret. Vid inträdet körs båda
    //            (enter, sedan over).
    // dragLeave  MÅL: pekaren lämnade, draget avbröts eller släppet
    //            avvisades — släck det som `dragOver` tände. Får INGA args
    //            med flit: den anropas också när ramverket städar (källan
    //            lämnade trädet mitt i ett drag), och då finns ingen last
    //            att tala om.
    // drop       MÅL: utför det inbyggda släppet — EFTER `onDrop`, och bara
    //            när jag fortfarande är målet, appen inte satte `handled`
    //            och den validerade effekten inte är `None`. `e.effect` är
    //            appens val, redan validerat mot källans mask, och får
    //            sänkas till `None` om det inte gick. Platsen är den APPEN
    //            såg: räkna inte om den ur pekaren, för handlern kan ha
    //            ändrat det som ligger under den. `e.source` är källan
    //            EFTER handlern — nullptr om den rev källan.
    virtual bool canDragFrom(const Ctx&, float, float) const { return true; }
    virtual void dragStart(const Ctx&, DragStartArgs&) {}
    virtual void dragEnd(DragEndArgs&) {}
    virtual void dragEnter(const Ctx&, DragArgs&) {}
    virtual void dragOver(const Ctx&, DragArgs&) {}
    virtual void dragLeave() {}
    virtual void drop(const Ctx&, DragArgs&) {}

    // Ritar jag släppmarkeringen SJÄLV? Ramverket ramar annars in ett
    // accepterande mål i overlay-lagret (fokusringens släkting). En widget
    // som visar VAR i sig lasten hamnar (`TreeView`s insättningslinje)
    // svarar true — en ram runt hela trädet ovanpå den hade sagt mindre.
    virtual bool ownsDropIndicator() const { return false; }

    // AUTOSKROLLEN under ett drag: kan jag rulla
    // mot pekaren i (x, y)? Punkten är i FYSISKA px och Ctx:t `ctxFor`s, som
    // varje beteendekrok. `apply == false` är en REN FRÅGA; `apply == true`
    // rullar ett steg (`Style::dragScrollStep`). Svaret true = kan rulla
    // (respektive rullade). Ingen användarkod körs här. Sessionen frågar
    // kedjan från widgeten under pekaren UPPÅT, som hjulet (inaktiverade och
    // dolda hoppas över, roten frågas inte), och första true vinner — så en
    // inre lista vid sitt slut lämnar till den yttre panelen. Default nej;
    // `Panel` svarar i `Style::dragScrollZone` från sin rullningsytas kant,
    // så varje rullande yta (TreeView, ListView, ListBox, PropertyGrid,
    // DataGrid, en rullande panel) har den utan en egen gren.
    virtual bool dragScroll(const Ctx&, float /*x*/, float /*y*/, bool /*apply*/) { return false; }

    // Den effekt ett drag ÖVER mig just nu är accepterat med — `None` när
    // inget drag är här eller när det avvisas. Satt av ramverket EFTER
    // appens handler, och widgeten invalideras när den ändras, så `draw()`
    // kan läsa den rakt av för sin egen markering.
    DragEffect dropEffect() const { return dropEffect_; }

    // ÄR JAG KÄLLAN TILL ETT DRAG? —
    // sant från tröskeln, alltså redan i min `dragStart`-krok och i appens
    // `onDragStart`, genom hela draget till och med mitt slut (`dragEnd` och
    // `onDragEnd`). Falskt före tröskeln, efter slutet och när starten
    // AVBÖJDES (tom last, `allowed = None`) — också när en widget lämnar
    // trädet mitt i draget. En widget med inbyggd last läser den för att
    // veta när det den bär är giltigt (`TreeView::draggedPaths`): sessionen
    // äger svaret, så det kan aldrig stå kvar efter ett drag som tog slut
    // utan att kroken frågades. Läses ur fönstret; false utan ägarfönster.
    bool isDragSource() const;

    // ---------------------------------------------------- capture & grab
    // CAPTURE: musen fortsätter till MIG oavsett var den är —
    // mouseDrag/mouseDown/mouseWheel/mouseUp routas hit tills release.
    // Auto-capture vid tryck är default (mouseDrag under press, mouseUp vid
    // släppet) och räcker för ett drag inom ETT tryck och ETT fönster —
    // `Splitter` använder bara den. Explicit capture ÖVERLEVER knappsläppet
    // och ett objekt som byter fönster mitt i trycket: widgeten får `mouseUp`
    // och släpper själv när den är klar — dockningsvärden håller den när en
    // panel lossnar till ett eget fönster. Fönstret tar
    // den ifrån widgeten vid sin fokusförlust, när OS:et tar musens capture
    // (`gfx::EventType::MouseCaptureLost`) och när ett annat fönster tar över
    // trycket, och då kommer `mouseCancel` — `Left` med vänsterknappen nere,
    // annars `None`.
    // Medan capturen hålls får widgeten EN rörelse per rörelse och knapp: har
    // den redan fått rörelsen som `middleDrag`/`rightDrag` kommer ingen
    // `mouseDrag` till, utom med vänsterknappen nere. No-op utan ägarfönster.
    void setPointerCapture();
    void releasePointerCapture();

    // CAPTURE ÖVER FÖNSTERGRÄNSEN: som
    // `setPointerCapture`, men för ett tryck som började i ett ANNAT av
    // appens fönster — mitt fönster tar musens OS-capture
    // (`gfx::Window::captureMouse`), resten av trycket (rörelserna och
    // släppet) kommer hit, och fönstret där trycket började släpper sitt
    // tryck och sin capture: där kommer `mouseCancel` och inget `mouseUp` —
    // släppet kommer hit.
    // Svarar true när trycket nu levereras hit; false när plattformen låter
    // trycket stanna där det började (macOS, X11, Wayland — där får det
    // fönstret hela trycket oavsett fokus), när en annan app äger förgrunden
    // (Windows — då ger ingen capture hela trycket), eller inget tryck pågår
    // — då är ingenting ändrat, inte heller den explicita capturen. Också
    // false när en krok i ett annat fönster under övertagandet stängde mitt
    // fönster, flyttade mig eller tog trycket vidare: övertagandet städas då
    // Skälet är Windows: ett permanent
    // `noActivate`-fönster (det flytande verktygsfältet) är aldrig
    // förgrunden och får bara rörelserna över sin synliga yta;
    // dockningsvärden i förgrundsfönstret tar draget.
    bool takePointerCapture();

    // GRAB (menyer): en STACK — toppen får KeyDown/TextInput FÖRE
    // fokuserad widget, och mouseDownOutside() när ett tryck landar
    // utanför grabbens delträd (klicket går sedan vidare som vanligt —
    // grabben väljer själv att stänga sig). Stacken töms vid
    // WindowFocusLost. No-op utan ägarfönster.
    void grabInput();
    void releaseInput();

    // Tryck utanför mitt delträd medan jag håller grab (meny: stäng dig).
    virtual void mouseDownOutside(MouseDownOutsideArgs&) {}

    // Skrivtecken (UTF-8) till FOKUSERAD widget — routas som keyDown men
    // ur EventType::TextInput. true = konsumerad (AppWindow antar att
    // något ändrats och begär omritning).
    virtual bool textInput(const char*) { return false; }

    // Textwidget-kontraktet: sant `wantsTextInput()` (publik, ovan) =
    // widgeten tar textinmatning. AppWindow aktiverar då OS:ets
    // textinmatning (Window::setTextInputActive) och caret-timern medan
    // widgeten har fokus, och driver blinkfasen via caretBlink(c, on) —
    // widgeten lagrar fasen och invalidaterar ENBART sin caret-rect
    // (finkornig damage; appen sover mellan blinkningarna).
    virtual void caretBlink(const Ctx&, bool) {}

    // Fokus fått/tappat (sätts av AppWindow via klick/Tab). Krokarna får
    // bara röra widgetens EGET tillstånd (caret-timer m.m.) — omritningen
    // av fokusbytet sköter AppWindow. Användarkod körs ALDRIG här: de körs
    // mitt i `AppWindow::setFocus`, som inte tål att en handler flyttar fokus
    // igen. Ett event som fokusbytet utlöser (fältets `onCommit`) postas med
    // `postEvent` nedan, och `onFocus`/`onFocusLost` postar AppWindow själv.
    virtual void focusGained() {}
    virtual void focusLost() {}

    // `enable()`/`disable()` ÄNDRADE tillståndet —
    // anropas EFTER bytet och bara när det faktiskt bytte. `enabled`
    // KASKADAR INTE ner i trädet, och det är fortfarande regeln; kroken är
    // till för en KOMPOSIT som bär sin inredning som riktiga barn och måste
    // låta dem följa med — annars gick ett inaktiverat talfälts inre fält
    // att klicka i, fokusera och skriva i. `enable()`/`disable()` är inte
    // virtuella (de är settrar ovanpå ett privat fält), så utan kroken
    // fanns ingen väg för kompositen att få veta. Samma regel som
    // fokuskrokarna: bara widgetens EGET tillstånd (och dess inrednings),
    // aldrig en användar-handler.
    virtual void enabledChanged() {}

    // SPRÅKET KAN HA BYTTS sedan widgeten senast fick veta.
    // Anropas på varje widget i ett ÖPPET fönsters träd vid ett språkbyte
    // (`glaze::setLanguage`, `loadLanguage` …), på hela trädet när ett
    // fönster VISAS (språket kan ha bytts medan det var stängt) och på ett
    // delträd som ADOPTERAS in i ett öppet fönster (en flik som hölls
    // utanför trädet under bytet). Kroken ska alltså tåla att anropas när
    // ingenting bytts — den är idempotent.
    //
    // En `TrText` behöver den INTE: den löser upp sig själv vid ritning.
    // Kroken är till för det en widget HÄRLEDER ur språket och LAGRAR —
    // talfältets fälttext bär språkets decimaltecken. Att synka den
    // i `draw()` vore en tillståndsändring mitt i
    // ritningen. Samma regel som fokuskrokarna: bara widgetens EGET
    // tillstånd (och dess inrednings), aldrig en användar-handler. En
    // FRISTÅENDE widget utan fönster (GlazeBuilders preview) får den aldrig
    // — buildern bygger om previewn vid ett språkbyte i stället.
    virtual void languageChanged() {}

    // BARNLISTAN ÄNDRADES — `child` kom in (`adopt`,
    // båda formerna) respektive gick ut (`unadopt`/`releaseOwned`). Anropas
    // EFTER mutationen: i `childAdded` står barnet sist i `children()` med
    // föräldern och fönstret satta; i `childRemoved` är det redan borta ur
    // listan, utan förälder och utan fönster — men det LEVER (ett ägt barn
    // förstörs först efteråt), så kroken får läsa och röra det.
    //
    // Kroken är till för en container som håller TILLSTÅND om sina barn:
    // flikkontrollen vet vilken sida som är vald och vilka som ska synas, och
    // en sida som tas bort mitt i listan flyttar valet. Samma regel som de
    // andra krokarna — bara widgetens EGET tillstånd (och barnets synlighet),
    // aldrig en användar-handler: trädet är mitt i en mutation.
    virtual void childAdded(Widget&) {}
    virtual void childRemoved(Widget&) {}

    // ETT BARN VISADES ELLER DOLDES — `childAdded`/
    // `childRemoved`s tredje syskon. Anropas på FÖRÄLDERN av barnets
    // `show()`/`hide()`, bara när synligheten faktiskt bytte och EFTER
    // bytet: `child.isVisible()` svarar redan det nya, och syskonen är redan
    // omritade (`relayoutSiblings`). Till för en container som håller
    // tillstånd om sina barn — dockningsytan gör en panel som tas fram med
    // `show()` till den aktiva fliken i sin flikgrupp. Samma regel som de
    // andra krokarna: bara widgetens EGET tillstånd (och barnens), aldrig en
    // användar-handler — kroken körs mitt i en sättare appen anropade.
    virtual void childVisibilityChanged(Widget&) {}

    // ETT BARNS PLATS I FLÖDET ÄNDRADES — det fjärde syskonet. Anropas på FÖRÄLDERN av barnets
    // `relayoutSiblings()`, alltså av samma sättare som flyttar syskonen:
    // barnet visades eller doldes, eller dess remstjocklek eller EGNA mått
    // ändrades (en verktygsknapps text eller bild). Bara i ett ägarfönster,
    // som omritningen, men oavsett om syskonen flyttar. Till för en container
    // vars EGNA mått beror på barnens och som annars hade sett ändringen först
    // vid nästa ritning — ett flytande verktygsfälts fönster har fältets fasta
    // storlek, och ett dolt fönster ritas aldrig. En sättare som ändrar ett
    // mått utan att kalla `relayoutSiblings()` når inte kroken. Samma regel
    // som krokarna ovan: bara eget tillstånd, aldrig en användar-handler.
    virtual void childLayoutChanged(Widget&) {}

    // FOKUS KOM IN I ELLER LÄMNADE ETT BARN — det femte syskonet.
    // Anropas på FÖRÄLDERN när `child` eller något i dess delträd fick eller
    // tappade tangentfokus (FOKUS INOM — `onFocus`/`onFocusLost`s kedjor),
    // SYNKRONT i `AppWindow::setFocus` och EFTER bytet: `focused()` och
    // fönstrets `focusedWidget()` svarar redan det nya. Ett byte mellan två
    // ättlingar till samma barn når inte föräldern, och fönstrets rot är
    // aldrig `child`.
    // Till för en container vars LAYOUT beror på var fokus står —
    // verktygsfältet visar ett barn ur sitt överflöde så länge det har fokus
    // (tool_bar.h). Samma regel som krokarna ovan, och strängare: bara eget
    // tillstånd (invalidera), aldrig en användar-handler och aldrig trädet —
    // kroken körs mitt i fokusbytet, också när en fokuserad widget lossas.
    virtual void childFocusChanged(Widget&) {}

    // SYNLIGHETEN PÅ SKÄRMEN KAN HA ÄNDRATS
    // — jag själv eller en FÖRFADER visades eller doldes (`setVisible`), eller
    // fönstret öppnades, visades eller doldes. (Inte när det STÄNGS: då kan
    // trädet bära lånade barn som redan dött — fallgropen i rot-CLAUDE.md —
    // och den som frågar fönstret ser att det inte är öppet.) Frågas på HELA det
    // berörda delträdet, EFTER bytet (WinForms `VisibleChanged` når barnen på
    // samma sätt). `isVisible()` är fortfarande bara den egna flaggan: den som
    // vill veta om den SYNS frågar förfäderna och fönstret själv. Till för den
    // som håller något igång bara medan den syns — dockningsvärdens
    // lämna-prövning är en timer som annars hade väckt appen i ett dolt
    // fönster. Inte vid adoption eller lossning. Samma regel som krokarna
    // ovan: bara eget tillstånd, aldrig en användar-handler.
    virtual void shownChanged() {}

    // Finkornig damage: begär omritning av en godtycklig yta (fysiska px)
    // — billigare än invalidate() när bara en delyta rörts (caret-blinket
    // ritar t.ex. bara caret-recten). No-op-säker utan ägarfönster.
    void invalidateRect(const gfx::Rectf& physRect);

    // DAMAGE-STÄMPELN — ett tal som ÄNDRAS varje gång något
    // kan ha ändrat det widgeten ritar. Till för en widget som CACHAR sin
    // ritning: ritytans 3D-mål är miljoner trianglar, och ett blinkande
    // caret i ett textfält ovanpå ritytan ska inte rita om dem. Widgeten
    // sparar stämpeln den ritade vid och ritar om när den inte längre stämmer.
    //
    // VAD SOM STEGAR DEN:
    //   * widgetens EGNA `invalidate()` — sättarna, och widgeten själv;
    //   * fönstrets FULLA invalideringar (`AppWindow::invalidate()`), också
    //     den efter en körd användar-handler: kontraktet säger att en handler
    //     får ha ändrat vad som helst, så en `onPaint` som läser en variabel
    //     en knapp ändrade måste ritas om. Utseendebyten och språkbyten går
    //     samma väg (`App::invalidateAll`).
    // VAD SOM INTE GÖR DET:
    //   * ett BARNS egen `invalidate()` — textfältet på ritytan ritar om sig
    //     självt, inte ritytans innehåll;
    //   * `invalidateRect` och `AppWindow::invalidateOverlay` — de skadar en
    //     yta respektive overlay-lagret och säger inget om innehållet;
    //   * den RITADE vägen: att fönstret ritar om allt (rundat fönster,
    //     animering, för mycket damage, en ny RT) ändrar ingenting.
    //
    // Värdet är en SUMMA av två räkneverk som bara växer, och det är
    // jämförbart med sig självt i SAMMA fönster — en widget som byter fönster
    // byter halva summan. En fristående widget (utan fönster) har bara sin
    // egen del. Varken `dirty_` eller damage-vandringen läser den.
    std::uint64_t damageStamp() const;

    // Widgetens PLATS i förälderns flöde ändrades — den dolde sig, visade
    // sig, eller dess remstjocklek ändrades (en hopfälld dockningspanel) —
    // så SYSKONEN flyttar, och de är inte dirty. Ritar om
    // föräldern (dess yta omsluter barnen), eller hela fönstret när
    // föräldern är fönstrets rot, som inte ingår i damage-vandringen. Bara
    // när widgeten DELTAR i flödet: en remsa vid en kant (`Dock::Fill` tar
    // det som blir över och flyttar ingen) eller ett barn till en förälder
    // som placerar själv (`placesChildren`). `setVisible` kallar den; en
    // widget som ändrar sin egen remstjocklek — eller sin egen längd i en
    // förälder som placerar själv, som verktygsknappens text och bild —
    // kallar den efter `invalidate()`. Föräldern får veta det i
    // `childLayoutChanged`. No-op-säker utan ägarfönster.
    void relayoutSiblings();

    // Starta om caret-blinkets period med synlig fas (anropas när caret
    // flyttats/text ändrats så careten inte blinkar bort mitt i skrivning).
    // No-op om widgeten inte är fokuserad i ett ägarfönster.
    void restartCaretBlink();

    // ETT EVENT I PUMP-SVANSEN — kör `fire` i `glaze::app().post` i
    // stället för nu, och bara om `HandlerGuard`en som togs NU fortfarande
    // står sig då: widgeten lever, fönstret den satt i lever, och den sitter
    // kvar i SAMMA fönster. En widget som lossats (och kanske satts in igen,
    // i samma fönster eller ett annat) är i en ny kontext, och eventet
    // beskriver den gamla — det slopas. Efter `fire` ritas hela fönstret om
    // (en användar-handler har kört — samma garanti som efter ett klick).
    // Till för den handling som AVSLUTAS av att fokus lämnar widgeten —
    // fältets `onCommit`, datagridens sparade cell: `focusLost` körs mitt i
    // `AppWindow::setFocus` och får aldrig köra användarkod. Tillståndet
    // skrivs av anroparen DIREKT; det är bara berättelsen om det som väntar.
    // Kräver ett ägarfönster (ett fokusbyte förutsätter ett) — utan ett är
    // anropet en no-op.
    void postEvent(std::function<void()> fire);

    // VAKTEN — "får jag röra widgeten igen?" efter att en
    // användar-handler har kört. Den fångar tre saker när den skapas, och
    // `ok()` kräver att alla tre står sig:
    //   * widgeten LEVER — en handler får lossa och släppa en ägd widget;
    //   * FÖNSTRET den satt i lever — en LÅNAD widget kan överleva sitt
    //     fönster, och då dinglar `appWindow()`;
    //   * den sitter kvar i SAMMA fönster — anknytningen stegas varje gång
    //     widgeten (eller en förfader) adopteras eller lossas, så en widget
    //     som lossats och satts in igen är i en ny kontext, också i samma
    //     fönster.
    // `ok()` frågar livstecknet FÖRST: de andra två läser widgeten. En
    // widget utan fönster när vakten togs kräver bara de två andra.
    class HandlerGuard {
    public:
        explicit HandlerGuard(Widget& w);
        bool ok() const;

    private:
        Widget*             widget_;
        std::weak_ptr<char> life_;
        std::weak_ptr<char> window_;
        bool                hadWindow_;
        std::uint64_t       attachment_;
    };

    // ETT EVENT, VAKTAT — kör `handler` med en egen kopia av `args`
    // och svarar om widgeten går att röra efteråt (`HandlerGuard::ok()`). En
    // osatt handler är ingenting och svarar true. Anroparen som fortsätter
    // efter en handler — nästa event i ett par, musbehandlingen, `onClick`
    // efter växlingen — frågar svaret och rör aldrig `this` när det är
    // false. Varje händelsepar i ramverket går hit.
    template <class Args>
    bool fireEvent(Handler<Args>& handler, Args args = Args{})
    {
        if (!handler)
            return true;
        const HandlerGuard guard(*this);
        noteHandlerRun();
        handler(this, args);
        return guard.ok();
    }

    // ETT PAR FÖR SAMMA HANDLING — `first` (`onChange`, `onCheck`)
    // och sedan `second` (`onCommit`), med vakten frågad EMELLAN: en handler i
    // det första får riva eller flytta widgeten, och då fyras inte det andra.
    // Svarar som `fireEvent`. Läser det andra eventets handler widgetens
    // TILLSTÅND (egenskapslistans `changedRow()`), sätter anroparen det igen
    // mellan två `fireEvent` i stället — en handler i det första får ha
    // nollat det.
    template <class Args>
    bool firePair(Handler<Args>& first, Handler<Args>& second,
                  const Args& args = Args{})
    {
        return fireEvent(first, args) && fireEvent(second, args);
    }

    // GAV TRYCKET FOKUS? — sant i `mouseDown` när just det trycket
    // flyttade fokus till widgeten eller dess fokusombud. Textfälten läser
    // den: ett tryck som gav fokus väntar ut dragtröskeln innan det blir ett
    // markeringsdrag, så att `onFocus → selectAll()` står kvar för ett
    // klick med en darrig hand. Sätts av fönstret vid varje vänstertryck och
    // betyder ingenting utanför `mouseDown`.
    bool pressGaveFocus() const { return pressGaveFocus_; }

    bool hovered_ = false;
    bool pressed_ = false;
    bool focused_ = false;

private:
    // Bakom enable/disable/show/hide: invalidatera BARA vid ändring —
    // settrarna ska vara billiga att kalla i onödan.
    void setEnabled(bool on)
    {
        if (enabled_ != on) {
            enabled_ = on;
            invalidate();
            enabledChanged();   // kompositens inredning följer (se kroken)
            // …och PEKAREN: en inaktiverad
            // widget under pekaren visar pilen nu, inte vid nästa rörelse.
            refreshPointer();
            // …och STANDARDKNAPPEN: en inaktiverad knapp
            // är aldrig standard, och rollen kan gå till en annan.
            refreshDefaultButton();
        }
    }
    void setVisible(bool on)
    {
        if (visible_ != on) {
            visible_ = on;
            invalidate();
            // Ett dolt barn skär ingen remsa — så en synlighetsändring kan
            // ändra fönstrets INNEHÅLLSYTA (statusraden är fallet, se
            // AppWindow::setContentSize). Fönstret avgör själv om det
            // spelar roll; utan begärd innehållsstorlek är anropet en
            // ren no-op. Ligger UTANFÖR headern eftersom app_window.h
            // inte får inkluderas här (cirkulärt).
            notifyContentInsets();
            // …och SYSKONENS yta — se `relayoutSiblings()`.
            relayoutSiblings();
            // …och FÖRÄLDERNS tillstånd om sina barn — se
            // `childVisibilityChanged`. Före pekaren, som ska pröva det
            // läge kroken lämnar.
            if (parent_)
                parent_->childVisibilityChanged(*this);
            // …och HELA DELTRÄDET: min synlighet på skärmen,
            // och därmed barnens, har ändrats — se `shownChanged`.
            notifyShownChanged();
            // …och PEKAREN: widgeten under
            // pekaren kan vara en annan nu.
            refreshPointer();
            // …och STANDARDKNAPPEN: en dold knapp — eller
            // en i ett dolt delträd — tar inte Enter och är inte standard.
            refreshDefaultButton();
        }
    }

    // Se setVisible ovan — definierad i widget.cpp.
    void notifyContentInsets();
    // `shownChanged()` på mig och hela delträdet (setVisible ovan, och
    // fönstrets öppning, visning och döljning). Definierad i widget.cpp.
    void notifyShownChanged();

    // Fönstret prövar om pekarens form med den senast kända positionen
    // (`AppWindow::refreshCursor`) — efter `setCursor`/`resetCursor`, en
    // synlighets- eller aktiveringsändring och en borttagning. Definierad i
    // widget.cpp (app_window.h får inte inkluderas här); no-op utan fönster.
    void refreshPointer();
    // STANDARDKNAPPEN — samma form: en synlighets- eller
    // aktiveringsändring, eller en adoption in i fönstret, kan flytta rollen
    // mellan acceptknappen och en fokuserad knapp, och fönstret ritar om dem
    // (`AppWindow::defaultButtonMayChange`). No-op utan fönster.
    void refreshDefaultButton();

    // BORTTAGNINGENS TVÅ FASER — delade med flit, så en nekad
    // operation omöjligt kan lämna trädet halvvägs. `validateRelease` gör
    // de fyra kontrollerna och muterar INGENTING; `detach` gör HELA
    // mutationen (fönsterstädning FÖRST, sedan trädet).
    bool validateRelease(const Widget& w, bool ägdKrävs, const char* op) const;
    bool ownsChild(const Widget& w) const;
    void detach(Widget& w);


    // EN DOCKAD REMSAS MÅTT i FYSISKA px: `contentSize`
    // skalad, och för Top/Bottom höjden ur `heightForWidth` med remsans
    // bredd (`area.w` — förälderns yta minus remsorna före den) när kroken
    // svarar. Dock-uppspelningen i `resolve` och `carveSiblingDocks` frågar
    // båda härifrån, så en remsa är lika tjock för sig själv och för sina
    // syskon. Statisk av `measure`s skäl.
    static gfx::Sizef dockedSize(const Widget& w, const gfx::Rectf& area,
                                 const Ctx& c);

    // SYSKONUPPSLAGET: TIDIGARE syskon med det NAMNET,
    // eller nullptr. Vandringen STANNAR VID `this` — exakt dock-carvingens
    // `if (sib == this) break;` — och det är den raden som gör cykler
    // OUTTRYCKBARA i stället för upptäckta: en framåtreferens och en
    // självreferens ser likadana ut som ett okänt namn. Ingen djupvakt
    // behövs, för syskonindex minskar strikt i varje rekursionssteg.
    const Widget* earlierSibling(const std::string& sname) const;

    bool visible_ = true;
    bool enabled_ = true;
    // Se `inDockFlow()`. Sätts BARA av AppWindow på sina chrome-rötter,
    // `topBar_` och `appIcon`.
    bool inDockFlow_ = true;

    // DRAG-AND-DROP. Egenskaperna har ingen synlig verkan och
    // settrarna invaliderar därför inte; `dropEffect_` skrivs BARA av
    // AppWindow (friend), som invaliderar widgeten när den ändras.
    // TOOLTIPEN — bara texten bor här; visningen är fönstrets.
    TrText     toolTip_;
    // PEKARENS FORM — satt av appen, tom = `cursorAt`.
    // Fönstret läser den (friend) i `AppWindow::updateCursor`.
    std::optional<gfx::PointerShape> cursor_;
    // RULLNINGENS RIKTNING — fyra bitpar, ett per (mus/
    // styrplatta × lodrätt/sidled): "satt" och "vänd". 0 = allt ärvs.
    std::uint8_t wheelInvert_ = 0;

    bool       allowDrag_  = false;
    bool       allowDrop_  = false;
    DragEffect dropEffect_ = DragEffect::None;

    // Trädet: parent_ sätts av adopt(). children_ är z-ordningen (ägda +
    // lånade); ownedChildren_ bär ägandet för de ägda. Rivning raderar
    // ENBART ownedChildren_ — lånade pekare i children_ derefereras aldrig
    // i rivningsväg (de kan redan vara döda: subklassens värde-medlemmar
    // dör före basens rot). Fristående widgets (GlazeBuilders preview) har
    // varken parent_ eller owner_ — alla trädvägar är no-op-säkra där.
    Widget*                              parent_ = nullptr;
    std::vector<Widget*>                 children_;
    std::vector<std::unique_ptr<Widget>> ownedChildren_;

    void setOwnerForTree(AppWindow* owner);   // adopt + AppWindows rot
    // `languageChanged()` på mig och hela mitt delträd — de tre
    // anropsställena (språkbytet, fönstrets visning, adoptionen) går hit.
    void languageChangedTree();

    // Damage-bokföringen (ägs av AppWindow): owner_ = widgetens fönster,
    // propageras rekursivt av adopt() (nullptr för fristående widgets —
    // invalidate() är no-op-säker där). lastRect_ = senast RITADE ytan
    // (paintBounds, FÖNSTER-koordinater i fysiska px, sparad med widgetens
    // faktiska Ctx) — flytt/resize skadar både gammal och ny yta.
    AppWindow* owner_       = nullptr;
    // FÖNSTRETS LIVSTECKEN,
    // satt med `owner_` i `setOwnerForTree`. En lånad widget kan överleva
    // fönstret den adopterades i — dockningsvärden river ett flytande fönster
    // medan panelens barn lever — och `owner_` hade då dinglat. Varje läsning
    // går därför genom `liveOwner()`, som prövar livstecknet: ett dött fönster
    // ger nullptr utan att något delträd vandras när det dör. Valt framför en
    // vandring upp till roten vid varje läsning (O(djup) i `invalidate()`)
    // och framför en vandring NED när fönstret dör (ett lånat barn kan redan
    // vara dött).
    std::weak_ptr<char> ownerLife_;
    AppWindow*          liveOwner() const
    {
        return ownerLife_.expired() ? nullptr : owner_;
    }
    bool       dirty_       = false;
    bool       hasLastRect_ = false;
    gfx::Rectf lastRect_{};

    // `damageStamp()`s egen del: stegas av VARJE `invalidate()` på den här
    // widgeten och nollas aldrig. Ett räkneverk och ingen flagga — `dirty_`
    // ovan förbrukas av ritningen, det här läses av den som cachar.
    std::uint64_t invalidations_ = 0;

    // `lifeToken()`s ägare — LAT (tom tills någon frågar), och den dör med
    // widgeten, så varje utdelad `weak_ptr` går ut i samma ögonblick.
    std::shared_ptr<char> liv_;

    // ANKNYTNINGEN — stegas av `setOwnerForTree`, alltså varje gång
    // widgeten eller en förfader adopteras eller lossas. `HandlerGuard`
    // jämför den: ett postat event, och det andra i ett par, gäller bara den
    // kontext widgeten satt i när handlingen började.
    std::uint64_t attachGen_ = 0;

    // `pressGaveFocus()` — skrivs av AppWindow (friend) vid varje
    // vänstertryck, före `mouseDown`.
    bool pressGaveFocus_ = false;

    // Fönstrets livstecken för vakten. Här och inte i `HandlerGuard`:
    // AppWindow har gjort `Widget` till friend, inte dess nästlade klass.
    static std::weak_ptr<char> windowLife(AppWindow* w);

    // `fireEvent` räknar varje körd handler i fönstret —
    // `AppWindow::commitEdits()` ritar om fönstret bara när räknaren rört sig.
    // Utanför headern: den läser AppWindow.
    void noteHandlerRun();

    // `childrenExtent`s KÄRNA —
    // kanterna från båda hållen, de två flödena, sidledsparen och luften
    // (regeln vid `childrenExtent`); med en bredd tar remsorna och barnen
    // med två kända x-kanter `heightForWidth` (se den publika
    // överlagringen). `skip` hoppar över barn som räknas någon annanstans —
    // fönstret mäter sin rot utan `menuBar`/`statusBar`, som
    // `chromeInsetH()` redan räknar.
    gfx::Sizef extentOf(const Ctx& c, std::optional<float> width,
                        std::span<const Widget* const> skip) const;

    // `onResize`: storleken vid senaste LEVERANSEN (logiska px) — tom före
    // den första i fönstrets liv. Skrivs bara av AppWindow (friend).
    std::optional<gfx::Sizef> resizeSeen_;

    // `actualSize`/`toWindow`/`fromWindow`s gemensamma steg: min FYSISKA rect
    // i fönstrets klientyta och Ctx:t den räknades med (`ctxFor`) — tom utan
    // ett öppet fönster med font. Roten svarar med innehållsytan.
    std::optional<gfx::Rectf> windowBox(Ctx& c) const;

    friend class AppWindow;
};

// Skapar en widget ur registrets typnamn — utan egenskaper, utan
// event-koppling. nullptr vid okänd typ. Byggstenen bakom GlazeBuilders
// palett/preview och headless-tester; egenskaper sätts med setProp.
std::unique_ptr<Widget> makeWidget(const std::string& type);

// Alla registrerade typnamn, SORTERADE (registret är en hash-map, och en
// grind som itererar den ska ge samma utskrift varje körning). Listan finns
// eftersom en grind som ska mäta VARJE widget behöver den, inte en
// handskriven kopia av den som glider isär vid nästa nya typ.
//
// OBS vad listan INTE innehåller: TU-lokala klasser (dropdownens och
// menyns radlistor, meddelanderutans badge) och widgets utanför libglaze
// registrerar sig inte eller länkas inte hit. En grind som svarar för
// "alla implementationer" måste täcka dem separat.
std::vector<std::string> registeredWidgetTypes();

// KLASSEN BAKOM TYPNAMNET — verktygens väg in i
// registret. En fönsterheader bär widgetens C++-KLASS (`glaze::Button ok;`,
// `app::Meter level;`), registret typnamnet ur makrot ("button", "meter"),
// och utan översättningen mellan dem hade en
// egen widget blivit en stand-in i glaze-render och editorn också i ett bygge
// som länkade den.
//
// Klassnamnet är det FULLA namnet med namnrymden, så som kompilatorn känner
// klassen (avmanglat ur `typeid` — GCC och Clang, MSVC:s "class " skalat), och
// matchningen är EXAKT: `Meter` hittar inte `app::Meter`. Ett okvalificerat
// namn gissas aldrig — en egen `Button` hade annars blivit `glaze::Button`.
//
// `makeWidgetOfClass` → nullptr när ingen registrerad klass bär namnet;
// `widgetClassOf` → tom sträng för ett okänt typnamn.
std::unique_ptr<Widget> makeWidgetOfClass(const std::string& cppClass);
std::string             widgetClassOf(const std::string& type);

// ------------------------------------------- öppen widget-registrering
//
// En widget anmäler sig själv i sin EGEN cpp med GLAZE_REGISTER_WIDGET —
// då känner makeWidget (GlazeBuilders palett/preview) till den utan att
// någon central switch behöver röras (visionens punkt 7: en widget bor i
// en fil). Egna widgets i app-target registreras likadant; deras
// objektfiler länkas alltid, så registreringen är säker per konstruktion.
// (libglaze själv länkas WHOLE_ARCHIVE — se Glaze/widgets/CMakeLists.txt.)

using WidgetFactory = std::unique_ptr<Widget> (*)();

// Registrerar en factory för ett typnamn och dess KLASS (`typeid` — namnet
// räknas ut först när ett verktyg frågar). Första registreringen vinner;
// dubbletter loggas och ignoreras. Returvärdet finns bara för att kunna
// initiera en statisk bool (makrot nedan).
bool registerWidgetType(const char* type, WidgetFactory make,
                        const std::type_info& cls);

// Läggs på namespace-nivå i widgetens cpp:
//     GLAZE_REGISTER_WIDGET("mywidget", MyWidget)
#define GLAZE_REGISTER_WIDGET(typeName, Class)                               \
    namespace {                                                              \
    const bool glazeWidgetReg_##Class = ::glaze::registerWidgetType(         \
        typeName,                                                            \
        []() -> std::unique_ptr<::glaze::Widget> {                           \
            return std::make_unique<Class>();                                \
        },                                                                   \
        typeid(Class));                                                      \
    }

} // namespace glaze
