#pragma once
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/platform.h>
#include <gfx/window.h>

#include "glaze/style.h"
#include "glaze/widgets/app_icon.h"
#include "glaze/widgets/menu_bar.h"
#include "glaze/widgets/panel.h"
#include "glaze/widgets/status_bar.h"
#include "glaze/theme.h"
#include "glaze/widgets/top_bar.h"
#include "glaze/widget.h"

// AppWindow — basklassen ett programfönster ärver. Äger widgetarna,
// tema/stil-valet och damage-bokföringen; event-LOOPEN ägs av glaze::App
// (en app kan ha många fönster). Allt backend-maskineri (fönster,
// renderer, chrome) bor bakom gfx-fasaden.

namespace glaze {

class App;
class Button;          // standard- och avbrytknappen
class ToolTipWindow;   // tooltipens popup — intern, src/tooltip.h
class TrayHostWindow;  // systemfältsmenyns värd — intern, src/tray_icon.cpp
class DockFloatWindow; // en flytande dockningspanels fönster — intern,
                       // src/dock/dock_float.h

// `DialogResult` — resultatet ur en modal dialog — bor i widget.h
// (knappen bär ett eget, `Button::setDialogResult`).

// STARTPLATSEN (WinForms `FormStartPosition`)
// — var ett fönster hamnar när det öppnas (`AppWindow::setStartPosition`).
//   Default       OS:et väljer (förvalet)
//   CenterScreen  mitt i arbetsytan på ägarens skärm, utan ägare på
//                 primärskärmen
//   CenterOwner   mitt över ägarens klientyta; utan en SYNLIG ägare (ingen,
//                 dold eller minimerad) som CenterScreen
//   Manual        `setLocation`s position (den sätter läget själv)
// Platsen räknas i glaze ur `gfx::screens()` och KLÄMS in i arbetsytan, så
// en sparad position från en skärm som kopplats ur hamnar på en som finns.
// På Wayland finns ingen skärmrymd: kompositören placerar, och läget är en
// läsbar no-op (`geometry().hasPosition` är false).
enum class StartPosition : std::uint8_t {
    Default,
    CenterScreen,
    CenterOwner,
    Manual
};

// FÖNSTRET FÖLJER INNEHÅLLET (WPF `SizeToContent`)
// — vilka axlar av klientytan som RÄKNAS UR innehållet
// (`AppWindow::setSizeToContent`). Ett ATTRIBUT och inte en engångshandling:
// fönstret följer när en rad visas eller döljs, en text ändras, språket,
// stilen eller skalan byts. En anpassad axel slår `setContentSize`/
// `setClientSize` på den axeln; HUR den gör det säger `SizeToContentMode`.
//   Manual          ingen axel (förvalet)
//   Width           bredden ur innehållet
//   Height          höjden ur innehållet med fönstrets bredd — dialogens fall
//   WidthAndHeight  bredden, sedan höjden med den
enum class SizeToContent : std::uint8_t {
    Manual,
    Width,
    Height,
    WidthAndHeight
};

// INNEHÅLLET SOM LÅS ELLER GOLV (WinForms `Form.AutoSizeMode`
// — exakt det paret för ett formulär) — ortogonalt mot axlarna i
// `SizeToContent` (`AppWindow::setSizeToContentMode`). `Manual` struntar i det.
//   GrowAndShrink  förvalet: en anpassad axel är LÅST till innehållet (min =
//                  max) — den växer och krymper med raderna och går inte att
//                  dra; `WidthAndHeight` ger ett fast fönster.
//   GrowOnly       innehållet är GOLVET: användaren får göra fönstret större
//                  men aldrig så litet att innehåll döljs. Storleken på en
//                  anpassad axel är max(begäran eller aktuell storlek,
//                  innehållet) — fönstret VÄXER när innehållet passerar det
//                  och KRYMPER ALDRIG SJÄLVT (en dold rad sänker golvet men
//                  lämnar storleken). Utan `setContentSize`/`setClientSize`
//                  är begäran konstruktorns 1024 × 640. Appens
//                  `setMaximumSize` gäller, men aldrig under innehållet
//                  (innehållet vinner, som i det låsta läget), och
//                  maximeringen är tillåten.
enum class SizeToContentMode : std::uint8_t {
    GrowAndShrink,
    GrowOnly
};

// FÖNSTRETS GEOMETRI (WinForms `RestoreBounds` +
// `WindowState`) — det en app sparar och sätter tillbaka före nästa öppning.
// Positionen och storleken är NORMALLÄGETS, också för ett maximerat eller
// minimerat fönster, i LOGISKA px: positionen är placeringsrymden delad med
// PRIMÄRSKÄRMENS skala (kontinuerlig över skärmgränser, exakt när skalan är
// ett heltal och ±1 px annars), storleken klientytan som `setClientSize`.
struct WindowGeometry {
    int              x = 0, y = 0;
    int              width = 0, height = 0;
    gfx::WindowState state = gfx::WindowState::Normal;
    bool             hasPosition = false;   // false på Wayland och headless
};

// Basklass för ett programfönster. Ärv, skapa widgets i konstruktorn med
// add<T>(...), visa med show() — pumpen bor i glaze::App (se app.h för
// main-mönstret). TVÅ relationer med TVÅ namn, blanda aldrig ihop dem:
//  - Widget::parent() (widget i panel) = INNEHÅLL: koordinater, klippning,
//    hit-test. Slutar vid fönsterroten.
//  - AppWindow::owner() (fönster äger fönster) = ÄGARSKAP: livstid,
//    Z-ordning, minimering. Bär ALDRIG koordinater — en popups position är
//    skärmkoordinater, aldrig relativ ägarens innehållsyta.
class AppWindow {
public:
    // Alla argument har default: genererad kod (tre-fils-modellen) skriver
    // en tom ctor { initializeComponent(); } och sätter titel/storlek via
    // setTitle/setClientSize i stället.
    explicit AppWindow(TrText title = {}, int width = 1024,
                       int height = 640,
                       gfx::Chrome chrome = gfx::defaultChrome());
    virtual ~AppWindow();
    AppWindow(const AppWindow&)            = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    // Öppnar OS-fönstret (icke-blockerande — pumpen bor i glaze::App, som
    // skapas lat vid första show/run). Redan öppet fönster lyfts bara
    // (raise); dolt (hide()) blir synligt igen. Varianten med ägare knyter
    // ÄGARSKAPET: stängs ägaren kaskad-stängs det här fönstret (barnen
    // före ägaren), och OS:et håller det ovanför ägaren i Z-ordning.
    void show();
    void show(AppWindow& owner);

    // Döljer fönstret utan att stänga det (fyrar onHide) — show() tar
    // fram det igen (fyrar onShow). Dolt fönster ritas inte, och det har
    // ingen knapp i aktivitetsfältet.
    //
    // DOLD START: `hide()` FÖRE `show()`/
    // `run()` öppnar fönstret DOLT — OS-fönstret skapas utan att visas,
    // `onLoad` fyras men inte `onShow`, och ett senare `show()` visar det
    // och fyrar `onShow`. En systemfältsapp som startar med datorn ska inte
    // blinka förbi:
    //     MainWindow w;
    //     if (startadDold && glaze::TrayIcon::isSupported())
    //         w.hide();
    //     return w.run();   // pumpar med fönstret dolt
    // Begäran gäller NÄSTA öppning. En MODAL dialog startar aldrig dold —
    // den hade blockerat all input utan att synas — så där loggas begäran
    // och fönstret öppnas synligt. Previewn utför den MEDVETET inte (ett
    // nakent `hide();` i `.glazeui` bevaras men körs inte): ritbordet ska
    // visa fönstret, och dold start hör hemma i `main`.
    void hide();

    // Visar fönstret och pumpar tills det stängts — WinForms-mönstret,
    // huvudfönstrets hela liv i en rad:
    //     int main() { StartWindow win; return win.run(); }
    // Returnerar processens exit-kod (0 = normal stängning). main driver
    // sekvensen: splash → main är två run-anrop i rad.
    int run();

    // run med ÄGARE — dialogvägen: sätt isModal-
    // attributet (setModal) före anropet så filtreras input till det här
    // fönstret (bara modalstackens topp får mus/tangent/text/hjul;
    // fönsterhändelser och timers släpps igenom så bakgrundsfönster
    // fortsätter ritas om; klick på ett blockerat fönster lyfter modalen).
    // Returnerar resultatet ur closeWithResult (None vid vanlig stängning).
    // Dialogen skrivs som i WinForms — LOKAL variabel runt anropet:
    //     AboutDialog dlg;   // dialogen sätter setModal(true) i sin ctor
    //     if (dlg.run(*this) == glaze::DialogResult::OK) { ... }
    DialogResult run(AppWindow& owner);

    // Modal-ATTRIBUTET (beteende är attribut, inte subklasser): sätt före
    // run() så pumpas fönstret med modal-filtret ovan.
    void setModal(bool on) { isModal_ = on; }
    bool isModal() const { return isModal_; }

    // Stäng med resultat — run(owner) returnerar värdet och `dialogResult()`
    // läser det. Knappens väg dit är `Button::setDialogResult` (ett klick
    // stänger med knappens värde, efter dess `onClick`); en OK som VALIDERAR
    // sätter inget resultat på knappen och anropar den här själv när värdena
    // håller. close() utan resultat ger DialogResult::None, och ett
    // `onClose`-VETO nollställer resultatet — en senare
    // stängning utan resultat svarar alltså None och inte det vetade.
    void closeWithResult(DialogResult r);

    // RESULTATET, LÄST — det `closeWithResult` satte;
    // `None` före och utan, nollställt när `run` börjar och vid ett veto.
    // Samma värde som `run(ägare)` returnerar, och svaret efter den ägarlösa
    // `run()` (som returnerar en exit-kod).
    DialogResult dialogResult() const { return result_; }

    // STANDARD- OCH AVBRYTKNAPPEN (WinForms
    // `AcceptButton`/`CancelButton`). Enter trycker acceptknappen och Escape
    // avbrytknappen (`Button::performClick` — `onClick` och knappens
    // `dialogResult`), men bara när INGET närmare fokus tog tangenten: steget
    // ligger SIST före Tab, efter den fokuserade widgeten, förfäderna,
    // acceleratorerna och reservvägen. En fokuserad knapp tar sitt eget
    // Enter, ett `TextArea` sin radbrytning, en öppen vallista eller popup
    // sitt Escape och en accelerator bunden till Enter vinner. Med Ctrl, Alt
    // eller Cmd är Enter inte vår (`TextArea`s regel — Ctrl+Enter når
    // appen); Shift spelar ingen roll. Escape tas bara OMODIFIERAD.
    //
    // EN knapp per roll och fönster; samma knapp får vara båda
    // (meddelanderutans OK). Pekaren och knappens livstecken lagras — också
    // före `adopt`, så `initializeComponent`s ordning spelar ingen roll — och
    // tangenten prövar att knappen ÄR ANVÄNDBAR: den lever, sitter i DET HÄR
    // fönstret, syns i trädet (`shownInTree` — en knapp i en inaktiv flik
    // räknas inte) och är aktiverad. En oanvändbar knapp tar ingen tangent.
    // `nullptr` nollställer, och en knapp i ett ANNAT fönster loggas (den
    // lagras men tar ingen tangent här). Avbrytknappens `dialogResult` sätts
    // INTE av sättaren — WinForms lovar ingen sådan bieffekt — så den får
    // `setDialogResult(Cancel)` eller en `onClick`.
    //
    // I `.glazeui` ett naket fönsteranrop med MEDLEMMEN som argument:
    //     setAcceptButton(okBtn);
    //     setCancelButton(avbrytBtn);
    // Ett bortdöpt medlemsnamn ger ett kompileringsfel och aldrig en tyst
    // miss. macOS Cmd+. (plattformens "avbryt") ingår inte.
    void    setAcceptButton(Button& b);
    void    setAcceptButton(std::nullptr_t);
    Button* acceptButton() const;   // den satta så länge den lever, annars nullptr
    void    setCancelButton(Button& b);
    void    setCancelButton(std::nullptr_t);
    Button* cancelButton() const;

    // STANDARDKNAPPEN — den knapp Enter trycker just nu, och därför den som
    // RITAS som standard (Win32-modellen): har en ANVÄNDBAR `Button`
    // fokus är DEN standard, för den trycker själv på Enter; annars den
    // användbara acceptknappen. Ett fönster utan acceptknapp har ingen
    // standardknapp och ritas som en vanlig knapp, och en inaktiverad knapp är aldrig
    // standard. Knappens ritning frågar den här.
    bool    isDefaultButton(const Button& b) const;

    // Begär stängning (kopplas gärna till en "Stäng"-knapp). Verkställs
    // UPPSKJUTET av App i pump-svansen — aldrig mitt i en event-dispatch —
    // så det är alltid säkert att kalla ur handlers. C++-objektet lever
    // vidare (användaren äger det); fönstret kan visas igen med show().
    //
    // ATT RIVA FÖNSTRET I EN HANDLER ÄR TILLÅTET OCH SÄKERT — en
    // handler som förstör sitt eget fönster (`delete`, en `unique_ptr` som
    // nollas) eller ett annat: varje dispatchväg (musens tryck, släpp, drag,
    // hjul, dubbelklick och gest, tangenten, texten, drag-and-drop,
    // högerklicket, menyernas aktivering och acceleratorer, appens
    // `event()`-krok) tar fönstrets livstecken FÖRST och frågar det efter
    // varje anrop som kan köra användarkod — vägen slutar där, innan en enda
    // medlem läses. Fokushändelserna och postade händelser vaktas likadant
    // (`Widget::HandlerGuard`). UNDANTAGET är ritningen: `frame()` och
    // `overlay()` körs mitt i en present och får inte riva fönster. `close()`
    // är ändå det rekommenderade: det är uppskjutet, fyrar onClose-vetot och
    // låter fönstret visas igen. Skälet i `onClose` är `UserClosing` — samma
    // som krysset och Alt+F4 (`CloseReason`, widget.h).
    void close();

    bool isOpen() const { return open_; }
    bool isHidden() const { return hidden_; }

    // AKTIVT FÖNSTER (Qt `isActiveWindow`,
    // WinForms `Form.ActiveForm`) — fönstret har tangentbordsfokus: satt av
    // OS:ets fokushändelser, falsk när fönstret döljs eller stängs. En popup
    // blir aldrig aktiv — dess fokus räknas till ägarkedjans rot (Waylands
    // fokusgrupp). Appens aktiva fönster är `glaze::app().activeWindow()`.
    bool isActive() const { return active_; }

    // DEN FOKUSERADE WIDGETEN i fönstret (WinForms `ActiveControl`) — nullptr
    // när ingen har fokus. Kan vara en komposits inre fält (talfältets
    // `TextInput`). "Har ett textfält fokus?" är
    // `focusedWidget() && focusedWidget()->wantsTextInput()`.
    Widget* focusedWidget() const { return focusedWidget_; }

    // FÖNSTRETS GENVÄGAR — menyraden, sedan programmenyn,
    // samma namn som `MenuBar`/`AppIcon`/`Menu::tryShortcut`. Svarar om en
    // genväg tog tangenten; efter ett true kan fönstret vara rivet (en
    // handler körde), så anroparen frågar sitt livstecken innan den rör en
    // medlem. Ett ägt verktygsfönster låter ägarens genvägar svara:
    //   bool acceleratorFallback(gfx::Key k, std::uint32_t m) override
    //   { return owner() && owner()->isOpen() && owner()->tryShortcut(k, m); }
    bool tryShortcut(gfx::Key k, std::uint32_t mods);

    // Fönster-kontrollerna — .NET-formen på fasadens
    // minimize/maximize/restore: maximize() är ren (växling görs med
    // isMaximized() ? restore() : maximize() — CaptionButton-mönstret),
    // restore() tar fönstret ur både minimerat och maximerat läge.
    // No-op-säkra före show() (tvåfasen).
    void minimize();
    void maximize();
    void restore();
    bool isMaximized() const;

    // ---------------------------------------------------- caption-barerna
    // TopBar — den riktiga namnlisten — byggs i
    // konstruktorn när fönstret har eget chrome (Chrome::Custom), eller
    // LATT när `setChrome(Custom)` väljer det efteråt;
    // null vid Native/None. Plattforms-defaultbarnen (Windows: titel +
    // glyf-trio; Mac: traffic lights) byggs där och kopplas till fönster-
    // kontrollerna; baren customizas fritt via topBar()->add<T>() m.m.
    // Transparent fönster (fri geometri) döljer baren. Svaret följer den
    // GÄLLANDE chromen: ett fönster som byggt sin bar och sedan bytt till
    // Native eller None svarar null (baren står kvar dold).
    TopBar* topBar()
    {
        return chrome_ == gfx::Chrome::Custom ? topBar_ : nullptr;
    }

    // Statusraden är ett FÄLT (`statusBar`) och inte en lat metod: den
    // skapas med fönstret, adopteras i
    // konstruktorn direkt efter TopBar och är DOLD. En dold widget skär
    // ingen remsa ur innehållsytan, så den stör ingen layout —
    // trädordningen är DETERMINISTISK i stället för att bero
    // på när något först frågade efter raden.
    //
    // Slå på den med `statusBar.show();`, som är en riktig C++-sats och
    // därför kan stå i `.glazeui`. Ett publikt `visible`-fält
    // hade gått runt `setVisible()`, som är det som INVALIDERAR (regel 1).
    //
    // TOM STATUSRAD ÄR TILLÅTEN och ett medvetet beslut: `statusBar.show()`
    // utan barn ger en remsa som äter sin höjd utan innehåll. Att vägra
    // hade krävt att ramverket gissade vad användaren menade, och en tom
    // rad är dessutom ett fullt rimligt mellansteg medan appen byggs.
    // Deklarerad EFTER `root_` — se noten där.
    // (Deklarationen står längre ner, bland värde-medlemmarna.)

    // .NET-formen för fönsterknapparna: controlBox av = alla tre borta,
    // minimize-/maximizeBox styr sina knappar styckvis (show/hide på
    // CaptionButton-widgetarna — null-säkert utan TopBar).
    void setControlBox(bool on);
    void setMinimizeBox(bool on);
    void setMaximizeBox(bool on);
    bool controlBox() const { return controlBox_; }
    bool minimizeBox() const { return minimizeBox_; }
    bool maximizeBox() const { return maximizeBox_; }

    // Ägarfönstret (nullptr = fristående). ÄGARSKAP — aldrig koordinater.
    AppWindow* owner() const { return owner_; }

    // ------------------------------------------------ livscykel-events
    // Fönstrets fyra livscykel-events, wired som widget-events (regel 6:
    // (sender, args)). Fönstret är ingen widget — sender är fönstrets
    // ROT-panel (&root()), så en delad handler kan skilja fönster åt.
    // Genererad kod kopplar dem i initializeComponent():
    //     glaze::AppWindow::onLoad =
    //         [this](glaze::Widget* s, glaze::EventArgs& e) { onLoad(s, e); };
    // (kvalificerat namn — fönsterklassens handler får heta likadant).
    // Startlogik hör hemma i onLoad/onShow — aldrig i konstruktorn.
    //
    // TYPEN ÄR `Handler`, inte rå `std::function`, så alla sju events tar
    // BÅDA formerna — den fullständiga (sender, args) och den parameterlösa
    // (`glaze::AppWindow::onLoad = [this] { … };`, som `btn.onClick =
    // [this] { … };`). Nollställning är `= nullptr`
    // (`= {}` är tvetydig — se `Handler` i widget.h).
    Handler<EventArgs> onLoad;    // EN gång: efter ctor, före 1:a ritning
    Handler<EventArgs> onShow;    // synligt (show/åter-show)
    Handler<EventArgs> onHide;    // dolt (hide)
    Handler<CloseArgs> onClose;   // e.cancel stoppar — hela kaskaden;
                                  // e.reason säger varför (CloseReason)

    // Fönstertitel/-storlek — ägs av genererad kod (initializeComponent
    // börjar med setTitle/setClientSize) men fria att kalla när som helst;
    // no-op-säkra före show() (tvåfas-fönstret).
    // TITELN ÄR ETT TOKEN — `setTitle("Demo")` och
    // `setTitle(glaze::tr(1042))` går båda, och ett språkbyte trycker om
    // titeln till OS-fönstret och TopBars label (`App::languageChangedAll`).
    void setTitle(TrText t);               // matar även TopBars titel-label
    // Den UPPLÖSTA titeln.
    // `App::find(titel)` jämför mot DEN HÄR och aldrig mot tokenet.
    const std::string& title() const { return title_.str(); }
    // IDENTITETEN — det writern emitterar.
    const TrText&      titleToken() const { return title_; }

    // En anpassad axel (`setSizeToContent`) vinner också här — samma
    // storleksberäkning som `setContentSize`.
    void setClientSize(int w, int h);

    // INNEHÅLLSYTANS storlek — det mått appen faktiskt bryr
    // sig om: ytan UNDER namnlisten och OVANFÖR statusraden, alltså den
    // yta widgetarna placeras mot. `setClientSize` sätter KLIENTYTAN, så
    // för att räkna ut vad som ryms måste appen annars känna till chromets
    // mått — tal som bara går att LÄSA UR RAMVERKETS KÄLLKOD
    // (`Style::barHeight`, `Style::statusBarH`). Varje sådant tal i en app
    // är en plats där Style-axeln tyst slutar fungera.
    //
    // Måtten är LOGISKA px, som allt annat i API:t.
    //
    // UPPSKJUTEN UPPLÖSNING, och det är bärande: önskemålet LAGRAS och
    // räknas om ur det chrome fönstret FAKTISKT har — vid `show()`, vid
    // UI-/stilbyte och när statusraden visas eller döljs. Genererad kod
    // emitterar `setClientSize` FÖRST och `statusBar.show()` senare, så en
    // beräkning på plats hade räknat mot fel chrome. Det var precis det
    // ordningsproblem som den uppskjutna upplösningen löser.
    //
    // De två sättarna utesluter varandra: den som sätts SIST gäller.
    void setContentSize(int w, int h);

    // FÖNSTRET FÖLJER INNEHÅLLET (`SizeToContent`).
    // Måttet är rotens barn utom `menuBar`/`statusBar` (chromet räknar dem),
    // med bredden känd: `Height` mäter höjden med fönstrets bredd — en
    // radbruten etikett bryts om när dialogen görs bredare — och avrundar
    // UPPÅT. Regeln är `Widget::childrenExtent`s: DOCKADE
    // rader (en dold remsa skär ingenting), rader som flödar UPPIFRÅN och
    // rader mot NEDERKANTEN (en knapprad med `anchors.bottom`/`anchorBottom`
    // står under allt som flödar uppifrån), och SPÄNDA barn med sitt mätta
    // mått som minsta (en lista mellan fälten och knapparna med `height = 96`
    // blir aldrig lägre). LUFTEN mellan dem är `bottom` på den sista raden
    // uppifrån (`right` i sidled) — ett barn bundet bara uppifrån räknar
    // fältet som avståndet till det som står under. Ett DOLT led i en
    // syskonkedja lämnar sitt hål, som `resolve` gör, och en `Panel` utan eget
    // mått bidrar med 0 — ge raden en höjd, eller använd en `GroupBox`.
    // `WidthAndHeight` mäter en radbruten etikett som en rad, och `Width`
    // ensamt med rader mot nederkanten kan låta dem överlappa raderna
    // uppifrån (höjden räknas inte) — använd `WidthAndHeight`.
    // En anpassad axel slår `setContentSize`/`setClientSize` på sin axel (de
    // ger den fria), och `SizeToContentMode` avgör hur: LÅST (förvalet, min =
    // max = den anpassade storleken — `isFixedSize()` för `WidthAndHeight`,
    // och då ingen maximeraknapp) eller innehållet som GOLV (`GrowOnly`).
    // `minimumSize()`/`maximumSize()` svarar fortsatt med appens begäran.
    // På den FRIA axeln vinner användarens drag. Fönstret följer i TRE lager:
    // en synlighetsändring i trädet (postad till samma pumpvarv), ritningens
    // fråga (en text, ett språk-, DPI- eller stilbyte) och före den första
    // visningen (efter `onLoad`, så `CenterOwner` centrerar rätt storlek).
    // Maximerat eller minimerat anpassas det inte; återställt följer det
    // igen. Före `show()` lagras attributet och mäts efter init.
    void          setSizeToContent(SizeToContent s);
    SizeToContent sizeToContent() const { return sizeToContent_; }

    // LÄGET för de anpassade axlarna (kontraktet vid
    // `SizeToContentMode`): `GrowAndShrink` låser, `GrowOnly` gör innehållet
    // till golvet — dialogen med en lista som växer med fönstret, som aldrig
    // blir mindre än sina rader men går att dra upp. Ett byte i drift gäller
    // direkt (GrowOnly → GrowAndShrink låser till innehållet), före `show()`
    // lagras det. HÖJDEN i `WidthAndHeight` + `GrowOnly` mäts vid fönstrets
    // FAKTISKA bredd (minst innehållets), så ett brett fönster får vara lågt.
    void              setSizeToContentMode(SizeToContentMode m);
    SizeToContentMode sizeToContentMode() const { return sizeToContentMode_; }

    // Innehållsytans nuvarande storlek i LOGISKA px (chromet borträknat).
    // Läsbar även före `show()` — den räknas ur samma sanning som
    // layouten använder.
    gfx::Sizef contentSize() const;

    // Klientytans storlek i LOGISKA px (chromet INRÄKNAT) — motstycket
    // till `setClientSize`.
    //
    // `initW_`/`initH_` SPÅRAR fönstret: settrarna skriver dem, och
    // `WindowResized` skriver dem också (se app_window.cpp) — annars hade
    // läsaren svarat med ett gammalt önskemål så fort användaren dragit i
    // fönsterkanten, vilket är värre än ingen läsare alls. Före `show()`
    // är de begäran, vilket då ÄR det bästa svaret.
    gfx::Sizef clientSize() const;

    // MINSTA/STÖRSTA klientstorlek i LOGISKA px —
    // `.NET`-namnen (`Form.MinimumSize`/`MaximumSize`), samma namnrymd
    // som `setControlBox`/`setMinimizeBox`/`setMaximizeBox` är hämtade
    // ur. 0 = obegränsat, PER AXEL.
    //
    // **`min == max` betyder "ej ändringsbar"** och det är med flit inget
    // eget attribut: en dialog som ska ha exakt en storlek uttrycker det
    // genom att säga samma tal två gånger. Backenden maskar då bort
    // OS:ets resize-kant, och VÅRT chrome slutar visa resize-pekare
    // (`pushHitRegions` hoppar över kanterna och hörnen) — annars hade
    // namnlisten lovat något OS:et vägrar.
    //
    // Fungerar både före och efter `show()`: före lagras värdena och
    // följer med i `WindowDesc`, efter går de direkt till OS-fönstret.
    void setMinimumSize(int w, int h);
    void setMaximumSize(int w, int h);
    // Gränserna i KLIENTMÅTT, vilken form de än sattes i (nedan).
    gfx::Sizef minimumSize() const;
    gfx::Sizef maximumSize() const;

    // GRÄNSERNA I INNEHÅLLSMÅTT — som `setMinimumSize`/
    // `setMaximumSize`, men i INNEHÅLLSYTANS mått, det `setContentSize`
    // talar: chromets höjd (namnlisten, menyraden, statusraden) räknas på av
    // ramverket, så appen aldrig kopierar in `barHeight`/`statusBarH`.
    // UPPSKJUTET som innehållsstorleken: höjden räknas om ur det chrome
    // fönstret FAKTISKT har varje gång gränserna används — när statusraden
    // visas eller döljs, vid ett stilbyte och vid `show()` — så klientgränsen
    // följer med och innehållsgränsen står still. Bredden är densamma i båda
    // måtten (chromet tar bara höjd). 0 = obegränsat per axel; ett negativt
    // mått loggas och ändrar ingenting.
    //
    // De två formerna UTESLUTER varandra per gräns — den som sätts SIST
    // gäller (`setClientSize`/`setContentSize`s regel): `setMinimumSize`
    // efter `setMinimumContentSize` gör minimigränsen till klientmått igen.
    // Läsarna svarar i sitt eget mått oavsett form: `minimumSize()` i
    // klientmått, `minimumContentSize()` i innehållsmått (en klientgräns
    // lägre än chromet ger 0 — den begränsar inte innehållet). En axel som
    // följer innehållet (`setSizeToContent`) läser dem som appens gränser,
    // precis som klientformen. I `.glazeui` nakna fönsteranrop:
    //     setMinimumContentSize(480, 300);
    //     setMaximumContentSize(1200, 0);
    void       setMinimumContentSize(int w, int h);
    void       setMaximumContentSize(int w, int h);
    gfx::Sizef minimumContentSize() const;
    gfx::Sizef maximumContentSize() const;

    // LÅST STORLEK I INNEHÅLLSMÅTT (WinForms `ClientSize` + en fast
    // ram) — innehållsytan exakt `w` × `h` och inte ändringsbar: samma sak som
    //     setContentSize(w, h); setMinimumContentSize(w, h);
    //     setMaximumContentSize(w, h);
    // i ett anrop. Fönstret är fast (`isFixedSize()` — ingen maximeraknapp,
    // ingen resize-kant), och en statusrad som visas eller ett stilbyte
    // flyttar klientytan men aldrig innehållsytan. Måtten måste vara
    // positiva — annars loggas anropet och ingenting ändras. Lås upp med
    // `setMinimumContentSize(0, 0)` och `setMaximumContentSize(0, 0)`.
    // I `.glazeui`: `setFixedContentSize(360, 140);`.
    void setFixedContentSize(int w, int h);

    // Fast fönster: BÅDA axlarna låsta (min == max > 0). EN sanning —
    // både `pushHitRegions` och backend-valet läser den, så chromet och
    // OS:et aldrig kan tycka olika. Läser de EFFEKTIVA gränserna: en axel
    // som följer innehållet (`setSizeToContent`) är låst — i `GrowOnly`
    // bara när appens max inte når över innehållet.
    bool isFixedSize() const
    {
        return effMinW() > 0 && effMinW() == effMaxW() && effMinH() > 0 &&
               effMinH() == effMaxH();
    }

    // ALLTID ÖVERST — fönstret ligger över ANDRA APPARS
    // fönster tills attributet tas bort. `.NET`-namnet är `Form.TopMost`,
    // och attributmönstret är `setModal`/`setPopup`s.
    //
    // Skillnaden mot `raise()`: den lyfter EN gång, och nästa klick i ett
    // annat fönster lägger oss under igen.
    //
    // Fungerar både före och efter `show()` (`applySizeLimits`-vägen):
    // före lagras värdet och följer med i `WindowDesc`, efter går det
    // direkt till OS-fönstret.
    //
    // DEGRADERING — `topmost()` svarar med vad som FAKTISKT gäller och
    // aldrig med vad som begärdes, så en app kan upptäcka den:
    // **Wayland KAN INTE** (xdg-shell har inget "alltid överst" för
    // toplevels — det är kompositörens och användarens beslut), så där är
    // sättaren en no-op som loggar en gång och gettern svarar alltid
    // false. Windows (`HWND_TOPMOST`), macOS (`NSFloatingWindowLevel`) och
    // X11 (`_NET_WM_STATE_ABOVE`) uppfyller den. Hela tabellen står i
    // `gfx/window.h` vid `Window::setTopmost`.
    void setTopmost(bool on);
    bool topmost() const;

    // AKTIVITETSFÄLTET — `.NET`-namnet är
    // `Form.ShowInTaskbar`: false = ingen knapp i aktivitetsfältet. Ett
    // ATTRIBUT som `setModal`/`setPopup`: sätt det FÖRE `show()`, för det
    // läses när OS-fönstret skapas — Windows kräver dölj–byt–visa för en
    // växling, så den görs aldrig i drift. Efter `show()` loggas anropet och
    // gäller nästa öppning. Står i `.glazeui` som naket fönsteranrop
    // (`setShowInTaskbar(false);`), och previewn utför det.
    //
    // "Stäng till systemfältet" behöver det INTE: ett dolt fönster
    // (`hide()`) har ingen knapp. Per plattform (hela tabellen står vid
    // `gfx::WindowDesc::showInTaskbar`): Windows ger ett ofägt fönster ett
    // dolt ägarfönster, så det står kvar i Alt+Tab (minimerat blir det den
    // klassiska remsan nere till vänster); macOS har ingen knapp per fönster
    // — dockan är appens (`glaze::setShowInDock`); Linux är inte byggt, och
    // fönstret behåller sin post (loggas).
    void setShowInTaskbar(bool on);
    bool showInTaskbar() const { return showInTaskbar_; }

    // ------------------------------- RAMEN, PLATSEN OCH LÄGET
    //
    // RAMEN — vem som ritar fönstrets ram: `Custom` (förvalet,
    // vår namnlist), `Native` (OS:ets) eller `None` (ingen alls — en
    // splashskärm, en egen fri form). Samma val som konstruktorns fjärde
    // argument, men som en PLATT sättare, så den går att uttrycka i
    // `.glazeui` (`setChrome(gfx::Chrome::None);` — den genererade
    // konstruktorn kan aldrig skicka ett argument). Ett ATTRIBUT: det gäller
    // när OS-fönstret skapas. Efter `show()` loggas anropet och gäller nästa
    // öppning (att byta ram i ett öppet fönster kräver att OS-fönstret skapas
    // om). Ett ramlöst fönster har ingen namnlist att dra i — appen flyttar
    // det själv, eller låter det stå där startplatsen la det.
    void        setChrome(gfx::Chrome c);
    gfx::Chrome chrome() const { return chrome_; }

    // STARTPLATSEN (se `StartPosition` ovan). Ett
    // ATTRIBUT som läses när fönstret öppnas.
    void          setStartPosition(StartPosition p);
    StartPosition startPosition() const { return startPosition_; }

    // POSITIONEN i LOGISKA px (`WindowGeometry`s rymd). FÖRE `show()` är
    // den startplatsen och sätter `StartPosition::Manual` — klämd in i
    // arbetsytan när fönstret öppnas. Efter `show()` flyttar den fönstret
    // DIT, utan klämning (ett fönster får dras delvis utanför skärmen, som
    // OS-fönster kan), och bara i normalläge; ett maximerat eller minimerat
    // fönster loggar och står kvar. På Wayland finns ingen skärmrymd —
    // anropet loggas (fasaden).
    void setLocation(int x, int y);

    // LÄGET (WinForms `WindowState`). FÖRE `show()` är det STARTLÄGET,
    // som gäller vid fönstrets första synlighet — också efter en dold start.
    // En MODAL startar aldrig minimerad (den hade blockerat all input utan att
    // synas — loggas, start i normalläge), och en popup och ett
    // offscreen-fönster får det aldrig. Efter `show()` är det
    // `minimize()`/`maximize()`/`restore()`. Wayland kan inte starta
    // minimerat (loggas). `windowState()` svarar med det FAKTISKA läget när
    // fönstret är öppet, med begäran före, och med kortet efter stängning.
    void             setWindowState(gfx::WindowState s);
    gfx::WindowState windowState() const;

    // GEOMETRIN — normallägets position och klientyta och läget (se
    // `WindowGeometry`). Före `show()`: begäran. Öppet: OS-fönstrets spårade
    // normalruta. Stängt: ett KORT taget när fönstret stängdes, så mönstret
    //     const int r = win.run();
    //     sparaGeometri(win.geometry());
    // fungerar, liksom en läsning i `onClose`. Återställ FÖRE `show()`/`run()`
    // — i `main` eller där fönstret skapas — inte i `onLoad`, som fyras när
    // OS-fönstret redan finns (och på Windows redan syns).
    WindowGeometry geometry() const;

    // VÄNTA-PEKAREN över HELA fönstret (WinForms `UseWaitCursor`) —
    // slår varje widgets form. Syns DIREKT, utan att pekaren rör sig.
    void setUseWaitCursor(bool on);
    bool useWaitCursor() const { return useWaitCursor_; }

    // RULLNINGENS RIKTNING I FÖNSTRET — sätts på
    // fönstrets rot, så varje widget i fönstret som inte satt något själv
    // ärver den (`Widget::setWheelInvert`); det fönstret inte satt ärvs från
    // appen. En popup som fönstret öppnar är ett eget fönster och ärver från
    // appen. I `.glazeui` ett naket fönsteranrop:
    // `setWheelInvert(glaze::InputDevice::Touchpad, false, true);`.
    void setWheelInvert(InputDevice d, bool vertical, bool horizontal)
    {
        root_.setWheelInvert(d, vertical, horizontal);
    }
    void clearWheelInvert(InputDevice d) { root_.clearWheelInvert(d); }

    // Popup-attributet (beteende är attribut, inte
    // subklasser). Ett popup-fönster visas utan att ta fokus (noActivate +
    // toolWindow) och STÄNGS AV APP när ett tryck landar i ett annat
    // fönster eller när fokus lämnar appen — klick-utanför för
    // menyer-som-fönster. Sätt FÖRE show(); själva menyn är arsenal.
    void setPopup(bool on) { popup_ = on; }
    bool popup() const { return popup_; }

    // POPUPEN SOM TAR TANGENTBORDET — ett attribut på en popup
    // (`setPopup(true)`; utan den betyder det ingenting): en sökruta, en
    // kommandopalett, en väljare med eget innehåll. En popup tar aldrig
    // fokus — den är `noActivate`, så på Windows, macOS och X11 landar
    // tangenterna hos ÄGAREN (på Wayland ger popupens grab den fokus när den
    // öppnades av ett tryck). Med attributet SKICKAR ägaren varje tangent,
    // tangentsläpp och tecken vidare medan popupen är öppen, genom popupens
    // EGEN ingång — popupens fokuserade widget, Tab-vandringen och
    // `onKeyDown`/`onKeyUp` fungerar alltså som i ett vanligt fönster, på
    // alla plattformar, och ägaren ser inte tangenterna (inte heller dess
    // `event()`-krok, dess fokus eller dess genvägar). Släppet följer trycket:
    // en tangent som hölls när popupen öppnades upprepas i popupen men släpps
    // också hos ägaren (en rityta som tickade på den slutar), och släppet av
    // den som stängde popupen sväljs. Vill popupen låta ägarens genvägar
    // svara är det `acceleratorFallback` som i ett verktygsfönster.
    //
    // ESCAPE STÄNGER popupen när ingenting i den tog tangenten — omodifierad
    // och sist före Tab, som ett fönsters avbrytknapp (en avbrytknapp i
    // popupen vinner). Sätt FÖRE `show()`. I `.glazeui` ett naket
    // fönsteranrop, `setPopupTakesKeys(true);`.
    void setPopupTakesKeys(bool on) { popupTakesKeys_ = on; }
    bool popupTakesKeys() const { return popupTakesKeys_; }

    // Offscreen-attributet: fönstret skapas DOLT —
    // OS-ytan visas aldrig (WindowDesc::hidden hoppar setVisible/
    // ShowWindow) men renderer, font och retained-RT finns som vanligt,
    // så en engångs-frame kan ritas (window().renderFrame()) och läsas
    // tillbaka (window().captureFrame(...)). glaze-render-vägen. Sätt
    // FÖRE show().
    void setOffscreen(bool on) { offscreen_ = on; }
    bool offscreen() const { return offscreen_; }

    // Popup-ankare för nästa show: rect i ÄGARFÖNSTRETS klient-FYSISKA px
    // (samma rymd som resolve/contentArea). Backenden placerar fönstret
    // UNDER rectet och äger flip-uppåt/klamp-i-sidled när skärmen tar
    // slut (Wayland: xdg_positioner — skärmpositioner finns inte där;
    // D3D11/Metal/X11: clientToScreen/screenArea internt). Appkod räknar
    // ALDRIG i skärmrymden. Kräver show(owner) — ankaret är ägar-relativt.
    void setPopupAnchor(const gfx::Rectf& r)
    {
        anchorRect_ = r;
        hasAnchor_  = true;
    }

    // Genomskinligt fönster (per-pixel-alfa — fri geometri): bakgrunden
    // lämnas helt genomskinlig i stället för temats windowBg, chromet
    // tvingas av, och klick i genomskinliga pixlar släpps IGENOM till
    // fönstret under (kontraktet i gfx/window.h — formen avgör vad som är
    // klickbart; Windows/D3D11 är referensen, alfa-hit-test via
    // retained-spegeln). Rita formen i frame()-hooken; widgets fungerar som
    // vanligt ovanpå. Sätt FÖRE show(). Genomskinliga fönster ritas alltid
    // med FULL omritning (bakgrunds-alfa kan inte återställas per
    // damage-rect) — de är händelsestyrda som allt annat, bara utan
    // partiell väg.
    // Fönstrets hörnradie i FYSISKA px, eller 0 när fönstret är fyrkantigt
    // (inte rundat, eller maximerat). PUBLIK för att TopBar måste runda
    // sina ÖVRE hörn med exakt samma tal som bakgrunden — en sanning, inte
    // två uträkningar.
    float cornerRadius() const;

    void setTransparent(bool on)
    {
        transparent_ = on;
        syncTopBar();   // fri geometri = ingen namnlist (dold tar ingen yta)
    }
    bool transparent() const { return transparent_; }

    // Renderingen är händelsestyrd: fönstret ritas bara om när något hänt
    // (input, fönsterhändelser, en handler som kört). Däremellan sover
    // appen i gfx::waitEvent och drar ~0 % CPU.
    //
    // invalidate() begär en FULL omritning manuellt — behövs bara när
    // tillstånd ändras utanför event-flödet (timer, annan tråd, ...).
    // Rör ändringen bara EN widget är Widget::invalidate() billigare
    // (per-widget-damage — bara den ytan ritas om). Varje anrop stegar
    // varje widgets `damageStamp()` — också det som görs
    // efter en körd handler.
    void invalidate()
    {
        needsRedraw_ = true;
        fullDamage_  = true;
        ++fullInvalidations_;
    }

    // Begär en ny present för overlay-lagret (se overlay()-hooken) utan
    // att peka ut något innehåll som ändrat — tooltips/drag-ghosts flyttas
    // utan att röra retained-innehållet (ingen damage, bara composite +
    // chrome + overlay).
    void invalidateOverlay() { needsRedraw_ = true; }

    // Kontinuerlig rendering (vsync-takt) för animationer. Stäng av igen
    // så fort animationen är klar, så återgår appen till att sova.
    // Animation innebär full omritning varje frame (frame()-hooken kan
    // rita vad som helst tidsberoende).
    void setAnimating(bool on) { animating_ = on; }
    bool animating() const { return animating_; }

    // Debug-läge: visualisera damage-rects i overlayen — kontur + svag
    // fyllning per omritad yta, färgcykling per omritning (gul → orange →
    // magenta) så upprepade omritningar av samma yta syns, uttoning på
    // knappt en sekund. Rent diagnostiskt: påverkar aldrig innehållet
    // (ritas efter compositen) och appen sover i vila även med läget på
    // (presents rullar bara medan markörer tonar). Bind gärna till en
    // tangent i event()-hooken (demo: F12) — ramverket stjäl inga
    // tangenter självt.
    void setDebugDamage(bool on)
    {
        debugDamage_ = on;
        if (!on) {
            damageMarks_.clear();
            overlayAnimating_ = false;
        }
        invalidateOverlay();
    }
    bool debugDamage() const { return debugDamage_; }

    // Utseendet är FYRA skilda saker — och APP-GLOBALT: ETT state för alla
    // fönster (bor i App). Vilken fönster-instans en setter kallas på
    // spelar ingen roll — ALLA öppna fönster invalidateras och ritas om
    // ur nya state:t (händelsestyrt utan polling):
    //  - UI: vilken RITNING som körs (form/dekor — grenen i varje widgets
    //    egen draw()). Värdet bärs till Ctx::ui per repaint i makeCtx.
    //  - Theme: paletten (data). En UI kan ha FLERA teman — Slate har tio.
    //    Sätt egna färger per läge med setColorsLight/setColorsDark eller
    //    byt hela familjen med setTheme.
    //  - Style: MÅTTEN (logiska px). En UI kan ha flera storlekar; värdet
    //    bärs till Ctx::style per repaint. Style äger även UI-fontens
    //    storlek, så ett byte laddar om fonten i alla öppna fönster.
    //  - Läge: System (följer OS:ets ljus/mörkt ÄVEN UNDER DRIFT — varje
    //    repaint resolvar om), Light (alltid ljust), Dark (alltid mörkt).
    //
    // **setUI ÅTERSTÄLLER theme och style** till den nya familjens defaults:
    // en Glaze-palett ritad med Minimalistics mått är inte grindad mot
    // princip 0, och en halv kombination är ingen design. Vill man blanda
    // gör man det EXPLICIT efteråt med setTheme/setStyle.
    void setUI(UI u);
    UI   ui() const;

    void setColorsLight(const Theme::Colors& c);
    void setColorsDark(const Theme::Colors& c);

    void setTheme(const Theme& t);
    const Theme& theme() const;

    // Måtten. En Style hör ALLTID till ett UI — hämta den via
    // `styleAt(ui, i)` / `defaultStyle(ui)`, aldrig ur en global lista.
    // State:t bär en KOPIA (ett värde — skälet står i app.h), så en
    // temporär `Style{…}` eller en lokal som går ur scope är säker.
    void setStyle(const Style& s);
    const Style& style() const;

    void setThemeMode(ThemeMode m);
    ThemeMode themeMode() const;

    // Skapa och äg en widget: auto* b = add<glaze::Button>("Spara").
    // Widgeten hamnar i fönstrets osynliga ROT-PANEL och resolvar mot
    // innehållsytan — containers (Panel) läggs likadant och får sedan
    // egna barn med panel->add<T>(...).
    template <class T, class... Args>
    T* add(Args&&... args)
    {
        return root_.add<T>(std::forward<Args>(args)...);
    }

    // Fönstrets widget-träd: rotens barn är fönstrets översta nivå.
    // Roten är osynlig och ritas aldrig — den bär bara barnen.
    Widget& root() { return root_; }

    // Slå upp en widget per NAMN — söker REKURSIVT genom hela trädet:
    //     auto* b = find<glaze::Button>("okBtn");
    // Returnerar nullptr om namnet saknas eller typen inte stämmer.
    // Namnet är `Widget::name` = objektets namn (se widget.h); genererad
    // kod emitterar raden på varje nod, handkodade fönster sätter den
    // själva.
    template <class T = Widget>
    T* find(const std::string& name)
    {
        return findIn<T>(root_, name);
    }

    // COMMITTA DET SOM REDIGERAS — `Widget::commitEdit()` på den
    // fokuserade widgeten och varje förfader upp till roten, innerst först,
    // SYNKRONT: ett talfälts halvskrivna tal, ett fält som inte fått Enter
    // och egenskapslistans öppna editor blir värden nu, och deras
    // `onChange`/`onCommit` fyras som vanligt (bara om något ändrats). Ett
    // klick på en verktygsknapp, ett menyval och en accelerator flyttar inte
    // fokus och committar därför ingenting av sig själva — Spara-handlern
    // börjar här (WinForms `ValidateChildren`):
    //     spara.onClick = [this] { commitEdits(); skrivFil(); };
    // Kedjan räknas före första commiten, och en widget som en tidigare
    // commits handler rivit eller lossat hoppas över. SYNKRONT ÄVEN NÄR EN
    // HANDLER FLYTTAR FOKUS: kompositen ovanför committar då i sin
    // fokusförlust, och den händelsen (som annars postas) levereras ändå
    // innan anropet återvänder. Fönstret ritas om bara om en handler körde.
    // Ett anrop i handlerkod och inget tillstånd, så det har ingen
    // `.glazeui`-form.
    void commitEdits();

    // Fönstret skapas i konstruktorn (utan OS-fönster — det öppnas i
    // show(), tvåfas-init) så referensen alltid är giltig. Fonten skapas
    // först i show() — rör den inte innan dess.
    gfx::Window& window() { return *win_; }
    gfx::Font&   font()   { return *font_; }

    // SKÄRMENS FYSISKA UPPLÖSNING — skärmen fönstret står
    // på, med appens kalibrering (`App::setDisplayCalibration`): fysiska px
    // per mm, VARIFRÅN svaret kommer (`source`: mätt, antaget, kalibrerat)
    // och skärmens id. CACHAD — mäts när fönstret visas, när det byter skärm
    // eller skala och när kalibreringen ändras — och det är den `makeCtx`
    // ger ritytan (`Ctx::pxPerMm`). Före show() är svaret det antagna 96 dpi.
    // Antas måttet loggas det EN gång per skärm-id och process.
    const gfx::DisplayMetrics& display() const { return display_; }

protected:
    // Hooks för appen:
    // frame()   — egen ritning varje frame, under widgetarna. MÅSTE rita
    //             deterministiskt (samma tillstånd → samma pixlar): vid
    //             partiell omritning körs den EN GÅNG PER DAMAGE-RECT med
    //             klipp — icke-deterministisk ritning ger olika innehåll i
    //             olika rutor. Tidsberoende innehåll = setAnimating(true)
    //             (som ändå innebär full omritning per frame).
    // event()   — rå event-access; returnera true för att äta eventet.
    // overlay() — fri ritning OVANPÅ allt (widgets + chrome), varje
    //             present. Kontrakt: rent informativt lager (tooltips,
    //             drag-ghosts, markörer) — ingen hit-test, ingen input,
    //             och det påverkar aldrig omritningslogiken. Lagret
    //             komponeras om varje present och är självstädande: rita
    //             och glöm, ingen städ-omritning behövs. Ny present utan
    //             innehållsändring begärs med invalidateOverlay().
    //             Ordningen ovanpå den: widgetarnas egna overlay-lager
    //             (`Widget::drawOverlay`), sedan
    //             drag-återkopplingen och sist ramverkets damage-debug-lager
    //             (setDebugDamage) — det ska alltid synas.
    virtual void frame(const Ctx&) {}
    virtual bool event(const gfx::Event&) { return false; }
    virtual void overlay(const Ctx&) {}

    // RESERVVÄGEN FÖR EN TANGENT — frågas när INGEN widget,
    // ingen förfader och ingen av fönstrets EGNA acceleratorer (menyraden,
    // programmenyn) tog tangenten, och FÖRE Tab-vandringen. Default false.
    // Ett ägt verktygsfönster — en flytande dockningspanel — låter ägarens
    // acceleratorer svara här, så Ctrl+S fungerar också med fokus i panelen.
    // Svara true när tangenten är tagen. Samma läge som en handler: fönstret
    // kan vara rivet efteråt, och vägen slutar då.
    virtual bool acceleratorFallback(gfx::Key k, std::uint32_t mods)
    {
        (void)k;
        (void)mods;
        return false;
    }

    // ÄGAREN DOLDES ELLER VISADES — anropas på varje ÄGT
    // fönster när ägaren `hide()`:as (false) och när ett dolt ägarfönster
    // visas igen (true). Default ingenting: ett ägt fönster lever sitt eget
    // liv. Ett verktygsfönster som ska följa sin ägare — en flytande
    // dockningspanel — döljer och visar sig här, så en app som gömmer sig i
    // systemfältet inte lämnar paneler svävande kvar.
    virtual void ownerVisibilityChanged(bool shown) { (void)shown; }

    // SPRÅKET BYTTES. Default är TOM: `App` har redan
    // tryckt om titeln och ritar om varje fönster, så ett fönster vars
    // layout är REN dock/anchors behöver inte göra någonting — texterna
    // mäts om vid nästa ritning ändå.
    //
    // Kroken finns för fönster som mäter EN GÅNG och sätter EXPLICITA
    // mått: meddelanderutan gör det i sin `onLoad` ([message_box.cpp]),
    // så utan en omlayout hade en finsk knapptext klippts efter ett byte.
    // Ett sådant fönster overridar och kör om sin mätning.
    //
    // KONTRAKTET för en override: den får öppna och stänga fönster, och
    // `App` prövar därför `open_ && !pendingClose_` på nytt före VARJE
    // anrop i sin snapshot. Anropas ALDRIG för ett stängt fönster.
    virtual void onLanguageChanged() {}

    // Bygger ritkontexten och resolvar aktivt tema OCH aktiv stil — det
    // ENDA stället resolven sker. Appar som ritar mot en egen yta
    // (GlazeBuilder-canvasen) hämtar sin Ctx här och byter bara area.
    Ctx makeCtx();

    // FÖNSTRETS HÄNDELSEINGÅNG — varje event App routar hit (mus, tangent,
    // text, fönsterhändelser), och ordningen härinne ÄR routingen. För en
    // tangent: en öppen ägd popup som tar tangentbordet FÖRST
    // (`setPopupTakesKeys`; den får tangenten genom sin egen ingång, och här
    // händer inget mer), fönstret, appens `event()`-krok, ett pågående drag,
    // menyns tangentkrok, grabben, den fokuserade widgeten, förfäderna,
    // acceleratorerna, reservvägen, för Return/Escape en öppen popups kontroll
    // och sedan standard- och avbrytknappen (`setAcceptButton`),
    // en popup med attributet stänger på Escape, och sist Tab.
    //
    // PROTECTED med flit: det är
    // en PRODUKTYTA, inte en testkrok. En subklass får mata SIN EGEN ingång
    // med ett syntetiskt event (uppspelning, automatisering, ett fjärrstyrt
    // fönster), och eventet går exakt samma väg som ett från OS:et. WinForms
    // ger subklassen samma sak (`WndProc`, `OnMouseDown` … är protected), Qt
    // en publik `QCoreApplication::sendEvent`. Utan den kunde ingen grind
    // mäta ROUTINGEN — tröskeln och målvalet i ett drag, klicket som uteblir
    // efter det, numpadens vikning — bara krokarna, och en `friend` åt
    // grindarna hade varit den verkliga testkroken i produktkoden.
    //
    // KONTRAKTET: UI-tråden, som allt annat här. `windowId` läses inte —
    // eventet gäller DET HÄR fönstret. App:s modalfilter ligger UTANFÖR (i
    // `App::dispatch`), så ett fönster som matar sig självt passerar det;
    // det är fönstrets eget beslut. Mata den INTE inifrån en handler som
    // själv körs härinne — routingens tillstånd (trycket, draget, fokus) är
    // då mitt i en ändring; posta i stället (`glaze::app().post`). INTE
    // virtuell: en app som vill SE eller ÄTA ett event har `event()` ovan,
    // som körs först härinne.
    void handleEvent(const gfx::Event& e);

private:
    // show()-kärnan: öppnar OS-fönstret, laddar fonten, kopplar render-/
    // overlay-callbacken och registrerar i App.
    void    showOwned(AppWindow* owner);

    // `ownerVisibilityChanged` på de ägda fönstren — en kopia av listan, för
    // en override får dölja och visa (aldrig stänga) sig själv.
    void    notifyOwnedVisibility(bool shown);

    // Läser backendens mätning, lägger på kalibreringen och invaliderar när
    // svaret ändrats (se `display()`). App anropar den när kalibreringen byts.
    void    measureDisplay();

    // Laddar (om) fonten i aktuell uiScale × `style().uiFontSize` — vid
    // show, vid WindowDisplayScaleChanged (skal-/DPI-byte i drift: den förra
    // fonten är rastrerad för fel storlek ⇒ suddig/felmätt text) OCH vid
    // byte av UI/Style. No-op när den resulterande storleken är oförändrad.
    // Laddar OCKSÅ rollfonterna — EAGER, i samma svep:
    // mängden storlekar är sluten, så de skapas här och aldrig lat i
    // ritvägen (fontskapande är dyrt i alla tre backends).
    // Chromets höjd i LOGISKA px, räknad ur SAMMA sanning som layouten
    // använder (TopBar när fönstret har eget chrome och baren är synlig;
    // statusraden när den är synlig — bådas höjd via axelregeln: satt
    // höjd vinner, annars stilens mått). Native/None ⇒ 0 upptill.
    float   chromeInsetH() const;

    // Räknar om klientstorleken ur den begärda INNEHÅLLSSTORLEKEN. EN
    // funktion, anropad från tre ställen — `showOwned()` (före init),
    // `setUI`/`setStyle` via `App::applyContentSizeAll()` och
    // `Widget::notifyContentInsets()` när ett barn visas/döljs. No-op utan
    // begärd innehållsstorlek, och `setSize` görs bara när talen faktiskt
    // ändras. Widget är friend och når den.
    void    applyContentSize();

    // FÖNSTRET FÖLJER INNEHÅLLET — se
    // `setSizeToContent`. `contentInsetsChanged` är `notifyContentInsets`s
    // ingång: i `Manual` `applyContentSize` synkront, med en
    // anpassad axel POSTAS anpassningen (`setVisible` kör kroken före
    // förälderns, och en synkron `setSize` hade ritat fönstret mitt i
    // sättaren på D3D11). `refit` är anpassningen — gränserna först, sedan
    // storleken; `fitContent` det ÖNSKADE (oavrundade) innehållsmåttet med en
    // given fri bredd/höjd; `fitDue` ritningens fråga.
    void    contentInsetsChanged();
    void    postRefit();
    bool    canFit() const;
    void    refit();
    gfx::Sizef fitContent(const Ctx& c, float freeW, float freeH) const;
    void    fitDue();
    // De EFFEKTIVA gränserna: appens på de fria
    // axlarna. På en anpassad axel i `GrowAndShrink` den anpassade
    // klientstorleken som min OCH max; i `GrowOnly` innehållet som GOLV — min =
    // max(appens min, innehållet), max = appens (0 = obegränsat) men aldrig
    // under innehållet. I cpp:n (headern har inte `<algorithm>`).
    int     effMinW() const;
    int     effMaxW() const;
    int     effMinH() const;
    int     effMaxH() const;
    // APPENS höjdgränser i KLIENTMÅTT: en gräns satt i
    // innehållsmått får chromets höjd tillagd (`chromeInsetPx`), 0 förblir
    // obegränsat. `effMin*`/`effMax*` och `minimumSize()`/`maximumSize()`
    // läser dem. `chromeInsetPx` är `chromeInsetH` avrundad — EN regel för
    // innehållsstorleken, anpassningen och gränserna.
    int     appMinH() const;
    int     appMaxH() const;
    int     chromeInsetPx() const;
    // En gräns ändrades: till OS-fönstret, knappuppsättningen (min == max
    // tar bort maximeraknappen) och hit-kartan vid nästa ritning.
    void    sizeLimitsChanged();

    // onResize — upptäckten i ritvandringen
    // (`noteSize`, och `noteSizesIn` före den första visningen), EN postad
    // leverans per fönster när kön inte är tom (`scheduleResizes`), och
    // leveransen i pumpens svans med kedjespärren (`deliverResizes`).
    void    noteSize(Widget& w, const gfx::Rectf& box, float scale);
    void    noteSizesIn(Widget& w, const Ctx& c);
    void    scheduleResizes();
    void    deliverResizes();

    // Skickar de lagrade gränserna till OS-fönstret. No-op före show()
    // (tvåfasen) — då bär `WindowDesc` dem i stället.
    void    applySizeLimits();

    // Trycker om den LAGRADE titeln till OS-fönstret och TopBars label.
    // Anropas av `App::languageChangedAll()` efter ett språkbyte, SEPARAT
    // från den virtuella kroken — så en override som glömmer basanropet
    // ändå inte tappar titeln. `App` är friend och når den.
    void    repushTitle();

    // `Widget::languageChanged()` på hela trädet — `App::languageChangedAll`
    // går hit (App når inte `Widget`s privata vandring, fönstret gör det),
    // och `showOwned` gör samma vandring när fönstret öppnas.
    void    languageChangedTree() { root_.languageChangedTree(); }

    // Skriver `initW_`/`initH_` ur fönstrets faktiska yta — anropas från
    // `WindowResized` så läsarna spårar verkligheten och inte en gammal
    // begäran.
    void    trackClientSize();

    void    reloadFont();
    float   fontPx_    = 0.0f;   // storleken font_ faktiskt laddades med
    float   headingPx_ = 0.0f;   // dito för headingFont_

    // run()-kärnan (båda överlagringarna): visa + pumpa tills stängt.
    // true = pumpen kördes (falskt vid init-fel/redan öppet).
    bool    runInternal(AppWindow* owner);

    // Livscykel-avfyrningarna (sender = &root_). fireCloseCancelled
    // returnerar true när handlern satt e.cancel (App::confirmClose).
    // SIGNATUREN ÄR `Handler<EventArgs>` och inte `std::function`: `Handler`
    // har MEDVETET ingen konvertering till
    // `std::function` — bara `operator bool` och anropsoperatorn.
    // `fireCloseCancelled` nedan går generiskt via just de två operatorerna.
    //
    // SVARET är om fönstret LEVER efter handlern: en handler får
    // riva sitt fönster, och `showOwned` läser medlemmar efter
    // `onLoad`. Livstecknet tas FÖRE anropet — efter det får ingen medlem
    // röras förrän svaret är läst.
    bool fireLifecycle(const Handler<EventArgs>& fn)
    {
        if (!fn)
            return true;
        const std::weak_ptr<char> liv = lifeToken();
        EventArgs                 e;
        fn(&root_, e);
        if (liv.expired())
            return false;
        invalidate();   // handlern kan ha ändrat vad som helst
        return true;
    }
    bool fireCloseCancelled(CloseReason skäl)
    {
        if (!onClose)
            return false;
        CloseArgs e;
        e.reason = skäl;
        onClose(&root_, e);
        invalidate();
        return e.cancel;
    }

    // STÄNGNINGENS BEGÄRAN — `close()` med SKÄLET, som App läser i
    // vetorundan (`CloseArgs::reason`). Ett `AppQuit` som kommer medan en
    // annan stängning väntar i samma varv uppgraderar skälet: ett appavslut är
    // starkare, och krysset + Cmd+Q i samma varv hade annars vetats som ett
    // kryss. `App` (Quit) och fönstrets egen `close()` går hit.
    void requestClose(CloseReason skäl);

    // ETT DOLT FÖNSTER VISAS — `show()` efter `hide()` eller en dold
    // start, och den första visningen efter `onLoad`. Den första räknar om
    // startplatsen med fönstrets storlek just då. Fyrar `onShow` sist.
    void revealHidden();
    // Ett fönster som aktiveras medan ÄGAREN kör sin `onLoad` blir ägarens
    // `loadActivated_` (se medlemmen) — utom de som aldrig tar aktiveringen.
    void noteLoadActivation(bool förstaVisningen);
    // STARTPLATSEN i placeringsrymden för `owner`, eller
    // inget svar när OS:et eller popup-ankaret väljer platsen.
    std::optional<gfx::Pointf> startPlacementFor(const AppWindow* owner) const;

    // Vill fönstret ha en frame? App:s sov-villkor: waitEvent bara när
    // INGET fönster vill (hård regel 1 över N fönster). Dolda fönster
    // ritas inte — deras damage väntar tills de visas igen.
    bool wantsFrame() const
    {
        return open_ && !hidden_ &&
               (animating_ || overlayAnimating_ || needsRedraw_);
    }

    // ÖPPEN MENYS TANGENTKROK. Sätts av `MenuPresentation`
    // medan en meny hänger i det HÄR fönstret och nollas när kedjan
    // stängs. Den frågas FÖRST i `KeyDown` — före grab-stacken och före
    // den fokuserade widgeten — eftersom en öppen meny är modal för
    // tangentbordet: pilar och Enter tillhör menyn så länge den är uppe.
    //
    // Den behövs på ÄGARSIDAN på grund av ett plattformsdelta: popupen är
    // `noActivate` på Windows/macOS/X11, så tangenterna landar här; på
    // Wayland flyttar `xdg_popup_grab` fokus till popupytan och menyns
    // egen grab tar dem i stället. Båda vägarna ropar samma navigering,
    // och dubbel dispatch kan inte uppstå (ett event, ett `windowId`).
    //
    // ACCELERATORER går INTE genom den här kroken utan genom `menuBar`
    // längre ner i KeyDown, efter att fokuserad widget tackat nej — en
    // permanent grab hade gått före fokuserad widget och stulit pilar och
    // text från t.ex. en `TextInput`.
    void setMenuKeyHook(std::function<bool(gfx::Key, std::uint32_t)> fn)
    {
        menuKeyHook_ = std::move(fn);
    }
    std::function<bool(gfx::Key, std::uint32_t)> menuKeyHook_;

    // HÖGERKLICKETS routing: ROUTED LOOKUP uppåt i
    // `parent()`-kedjan efter första satta `onContextMenu`. `over` FÅR
    // vara nullptr (tom fönsteryta) — då startar sökningen i roten. Svaret:
    // om en handler kördes (då har snabbmenyn tagit trycket).
    bool    fireContextMenu(Widget* over, float x, float y);

    // MITTEN- OCH HÖGERTRYCKETS routing: BUBBLAR som hjulet — från `over`
    // uppåt till första aktiverade
    // widget vars `middleDown`/`rightDown` svarar true, som blir
    // `auxWidget(b)`. Explicit capture frågar bara den fångande. `over` FÅR
    // vara nullptr (tom fönsteryta — ingen tar trycket). Ett kvarhängande
    // tryck med samma knapp har anroparen redan avbrutit (`cancelLostPress`).
    // Den tillfrågade står som PRELIMINÄR
    // mottagare under sin krok, så ett avbrott där (en modal dialog i kroken ger
    // fokusförlust) når den; `auxSeq(b)` är tryckets identitet, och ett tryck
    // som tog slut under kroken återställs aldrig. false = trycket tog slut
    // under en krok, eller fönstret dog — då ingen snabbmeny och ingen räkning.
    bool     routeAuxDown(gfx::MouseButton b, Widget* over, float x, float y);
    // ETT TAPPAT SLÄPP (det tredje avbrottet): ett
    // nytt tryck medan förra trycket med samma knapp står kvar. Widgetens eget
    // tryck (den tryckta, eller `auxWidget(b)`) nollas och får `mouseCancel(b)`,
    // och sedan får appens mål `onMouseCancel` (`dropPointer`s ordning). Svaret:
    // om något avbröts — då räknar anroparen om widgeten under pekaren, för en
    // handler kan ha ändrat trädet — och fönstrets liv frågar anroparen efteråt.
    bool     cancelLostPress(gfx::MouseButton b);

    // ------------------------------------ APPENS MUS
    // `Widget::onMouseDown`/`onMouseUp`/`onMouseCancel` — kontraktet står vid
    // fälten i widget.h. Bokföringen per knapp, [0] vänster, [1] mitten,
    // [2] höger (`clicks_`s ordning): MÅLET som fick tryckets `onMouseDown`
    // (nollat när trycket slutar, och tyst i `detachTree`, `forgetWidgets` och
    // när fönstret visas på nytt), om appen TOG trycket, och tryckets
    // SEKVENSIDENTITET (`auxSeq`s mönster: stegad när ett tryck börjar och
    // slutar, så att ett avbrott under en handler märks efteråt). Bokföringen
    // gäller VARJE tryck med ett mål, satt handler eller inte — en `onMouseUp`
    // utan `onMouseDown` fungerar.
    struct AppPress {
        Widget*       w     = nullptr;
        bool          taken = false;
        std::uint64_t seq   = 0;
        // Trycket är SLUT — målet släpps och identiteten stegas (också för ett
        // tryck vars `onMouseDown` kör just nu). Svaret är målet det hade.
        Widget* end()
        {
            Widget* const förut = w;
            w                   = nullptr;
            taken               = false;
            ++seq;
            return förut;
        }
    };
    AppPress  appPress_[3];
    AppPress* appPressFor(gfx::MouseButton b);   // nullptr för None
    // Tryckets MÅL: den fångande (om den är aktiverad), annars widgeten
    // under pekaren om den är aktiverad, och på tom fönsteryta roten. nullptr =
    // inget mål (en inaktiverad widget under pekaren — ingen bubbling).
    Widget*   mouseTarget(Widget* over);
    // `onMouseDown` på målet: bokföringen sätts FÖRE handlern (målet står som
    // PRELIMINÄRT, så ett avbrott under handlern når det), och efteråt säger
    // svaret vad som återstår — `Död` (fönstret dog), `Slut` (trycket tog slut
    // under handlern, eller målet lämnade trädet — inget mer för trycket),
    // `Otaget` (widgetens egen hantering följer) eller `Taget` (`handled`).
    enum class AppDown : std::uint8_t { Död, Slut, Otaget, Taget };
    AppDown   appMouseDown(gfx::MouseButton b, Widget* mål, float x, float y);
    // `Widget::appPress` på målet för ett TAGET tryck. Svaret: om
    // trycket lever efteråt — fönstrets liv frågar anroparen.
    bool      appPressStep(gfx::MouseButton b, float x, float y,
                           Widget::AppPressStep steg);
    // SLÄPPET: bokföringen nollas FÖRE handlern, och `onMouseUp` fyras på
    // målet. `ut` säger vem målet var (nullptr om det lämnade trädet under
    // handlern), om appen tog trycket och om handlern TOG SLÄPPET (`handled`,
    // utan verkan för ett taget tryck). false = fönstret dog.
    struct AppUp {
        Widget* target  = nullptr;
        bool    taken   = false;
        bool    handled = false;
    };
    bool      appMouseUp(gfx::MouseButton b, float x, float y, AppUp& ut);
    // AVBROTTET för en knapp: bokföringen nollas och målet får `onMouseCancel`
    // med pekarens senast kända läge. false = fönstret dog.
    bool      appMouseCancel(gfx::MouseButton b);
    // Målet för ett TAGET tryck äger pekaren (`updateCursor`) och får sina
    // rörelser genom `appPress` och aldrig genom hover-kroken.
    Widget*   takenPressOwner() const;
    bool      holdsTakenPress(const Widget* w) const;
    // `middleWidget_` för Middle, `rightWidget_` för Right.
    Widget*& auxWidget(gfx::MouseButton b);
    // Tryckets SEKVENSIDENTITET per knapp: stegas när ett tryck börjar
    // routas och när det tar slut (släppet,
    // avbrottet) — inte när mottagaren tas bort ur trädet, för då lever trycket.
    std::uint64_t& auxSeq(gfx::MouseButton b);

    // STYRPLATTANS GEST — nypning och vridning, `EventType::Gesture`.
    // Begin väljer MÅLET som hjulet väljer sitt: widgeten under pekaren och
    // sedan förfäderna (fönstrets rot frågas inte), inaktiverade hoppas över,
    // explicit capture frågar bara den fångande — och den första vars
    // `gesture` svarar true håller sekvensen (`gestureWidget_`). Update och
    // End/Cancel går sedan dit, var pekaren än står. Update utan hållen
    // sekvens väljer som en Begin (och levereras som en Begin); End/Cancel
    // utan en är ingenting.
    void    routeGesture(const gfx::Event& e);

    // HJULETS RADER FÖR EN WIDGET — EN plats för
    // rullningens riktning: en widget som följer den
    // (`Widget::followsWheelDirection`) får Shift+mushjul i sidled (bara när
    // eventet saknar sidled — macOS levererar enhetens axel) och
    // sina egna vändningar (`Widget::isWheelInverted`); en som inte gör det
    // får eventets råa rader. Räknas för VARJE widget hjulet prövas på.
    gfx::Pointf wheelFor(const Widget& w, const gfx::Event& e) const;

    // MUSPEKAREN: formen ur den widget som ÄGER pekaren just nu —
    // explicit capture, annars trycket, annars mittentrycket,
    // annars högertrycket, annars målet för ett tryck appen
    // tog, annars `over` (widgeten under pekaren, FÅR vara nullptr) — via
    // `Widget::cursorAt`, och vidare till `gfx::Window::setCursor`. Ingen
    // cache här: backenden vet vad den faktiskt visar och hoppar själv över
    // ett oförändrat val. Fönstrets VÄNTA-PEKARE slår allt, och en widgets
    // satta form (`Widget::setCursor`) slår dess `cursorAt` — bara för
    // widgeten själv, aldrig ärvd av barnen.
    void    updateCursor(Widget* over, float x, float y);
    // OMPRÖVNINGEN utan en musrörelse:
    // `updateCursor` med widgeten under den SENAST KÄNDA pekarpositionen.
    // Anropas av `setUseWaitCursor`, av `Widget::setCursor`/`resetCursor`
    // och när en widget göms, inaktiveras eller lossas — annars syntes
    // bytet först vid nästa rörelse. No-op när fönstret inte är öppet eller
    // pekaren står utanför det.
    void        refreshCursor();
    gfx::Pointf lastMouse_{};          // senaste pekarpositionen, fysiska px
    bool        mouseInside_   = false;  // pekaren står i klientytan
    bool        useWaitCursor_ = false;
    void    setFocus(Widget* w);      // klick/Tab — uppdaterar focused_-flaggor
    void    moveFocus(int dir);       // Tab-vandring: +1 framåt, -1 bakåt (DFS)

    // FOKUSHÄNDELSERNA — `onFocusLost`/`onFocus` för bytet `från` →
    // `till` (båda FÅR vara nullptr) läggs SIST i fönstrets kö, och en
    // leverans postas till pump-svansen. Semantiken (fokus INOM, innerst
    // respektive ytterst först, roten aldrig, ordningen) står vid fälten i
    // widget.h. Bara widgets med en satt handler vid bytet kommer med;
    // saknas alla läggs inget i kön.
    void    postFocusEvents(Widget* från, Widget* till);
    // KÖN och LEVERANSEN. Leveransen tar händelserna FRAMIFRÅN, en i taget,
    // tills kön är tom. En nästlad pump (en modal dialog i en handler) kör en
    // ANDRA leverans, som fortsätter där den här var — kön är den enda
    // sanningen om ordningen, så ingen leverans kan gå om en annan. Kön hålls
    // av en `shared_ptr` som varje postad leverans bär: den överlever
    // fönstret, så en leverans efter att fönstret dött (eller under tiden en
    // handler river det) läser aldrig fönstrets minne — händelsernas vakter
    // slopar dem, och `fönster` rörs bara när dess livstecken lever.
    struct FocusEvent {
        Widget*              w;
        Widget::HandlerGuard guard;   // tagen vid bytet
        bool                 gained;  // true = onFocus, false = onFocusLost
    };
    using FocusQueue = std::deque<FocusEvent>;   // tas framifrån
    static void deliverFocusEvents(AppWindow* fönster,
                                   const std::weak_ptr<char>& liv,
                                   FocusQueue& kö);
    std::shared_ptr<FocusQueue> focusQueue_ = std::make_shared<FocusQueue>();

    // FÖNSTRETS LIVSTECKEN — rotens: roten är en medlem och dör med
    // fönstret. `Widget::HandlerGuard` och fönstrets egna postade
    // leveranser frågar det.
    std::weak_ptr<char> lifeToken() { return root_.lifeToken(); }

    // `commitEdits()`s KÖ — EN FIFO per fönster, en medlem och ingen
    // lokal. Så länge `commitDepth_` > 0 (minst en `commitEdits()` på stacken)
    // lägger `Widget::postEvent` sin händelse SIST här i stället för i pumpens
    // kö, så en commit som en handler gjorde asynkron (den flyttade fokus)
    // ändå levereras innan `commitEdits()` återvänder. ORDNINGEN: kön töms
    // alltid FRAMIFRÅN, av den yttersta såväl som av en rekursiv
    // `commitEdits()` (som alltså också levererar det den yttre samlat) och av
    // pumpens svans (`App::drainCommitQueues` — en modal dialog i en insamlad
    // handler levererar resten där, i ordning). Ingen commit går om en
    // tidigare. Djupet räknas ner av en vakt, också genom ett undantag ur en
    // handler, och ingen pekare in i en stackram finns att lämna kvar.
    std::deque<std::function<void()>> commitQueue_;
    int                               commitDepth_ = 0;
    // Tömmer kön framifrån. false = fönstret dog under leveransen.
    bool drainCommitQueue();
    // `Widget::fireEvent` räknar varje körd handler; `commitEdits()` ritar om
    // bara när räknaren rört sig.
    std::uint64_t handlerRuns_ = 0;

    // Capture & grab — anropas via Widget::setPointerCapture/
    // grabInput. capturedWidget_ får ALL mus (drag/down/wheel) tills
    // release; grab-stackens topp får tangenter/text före fokuserad widget
    // + mouseDownOutside-kroken. Båda släpps vid WindowFocusLost, capturen
    // också vid `MouseCaptureLost`.
    void setPointerCapture(Widget* w) { capturedWidget_ = w; }
    void releasePointerCapture(Widget* w)
    {
        if (capturedWidget_ == w)
            capturedWidget_ = nullptr;
    }
    // CAPTURE ÖVER FÖNSTERGRÄNSEN — se
    // `Widget::takePointerCapture`. Tar OS-fönstrets capture för det pågående
    // trycket (`gfx::Window::captureMouse`); lyckas det får `w` den explicita
    // capturen, knapparna som hölls där trycket började räknas här
    // (`buttonsDown_`), och varje ANNAT av appens fönster släpper sitt tryck
    // (`dropPointer`) — med en återvalidering efter VARJE krok (öppet,
    // `w` kvar här med capturen, `gfx::Window::hasMouseCapture`). Ett
    // avbrutet övertagande städas och svarar false.
    bool takePointerCapture(Widget* w);
    // TRYCKEN OCH CAPTUREN GICK FÖRLORADE utan ett släpp — fokusförlusten,
    // `MouseCaptureLost` och ett tryck
    // som ett annat fönster tog över. EN väg, och HELA BOKFÖRINGEN NOLLAS FÖRE
    // NÅGON KROK: `pressed()`, `middlePressed()` och `rightPressed()`
    // slocknar, den explicita capturen släpps och `buttonsDown_` blir 0. Sedan
    // avbryts drag-and-drop (användarkod), och sist får varje widget som hade
    // fått ett släpp `mouseCancel` — den tryckta `Left`, en annan fångande
    // `Left`/`None`, mitten- och högertryckets `Middle`/`Right` — med vakt och
    // livstecken efter varje, och bara den som inte har tagit sin roll igen
    // under avbrottet. Ingen `mouseUp`. Grab, fokus och tangenter rörs inte
    // (de är fokusförlustens egna). false = fönstret dog under en krok.
    bool dropPointer();
    void pushGrab(Widget* w);
    void removeGrab(Widget* w);
    bool inGrabbedSubtree(Widget* w) const;   // w inom grab-toppens delträd?

    // ------------------------------------------- DRAG-AND-DROP
    // Sessionen bor HÄR därför att räckvidden är FÖNSTRET; API:t i widget.h
    // (egenskaperna, eventen, args) är oberoende av var den bor.
    // Implementationen bor i src/drag_drop.cpp.
    //
    // FASERNA:
    //   Idle       inget pågår
    //   Pending    vänstertryck på en `allowDrag`-widget, tröskeln inte
    //              passerad — trycket är fortfarande ett vanligt tryck.
    //              `starting` är satt medan tröskeln är passerad och källans
    //              `dragStart`-krok och `onDragStart` körs —
    //              `Widget::isDragSource()` läser den, och varje
    //              återställning (`drag_ = DragSession{}`) släcker den
    //   Active     draget pågår: pekaren styr målvalet, tangenterna är
    //              modala (Escape avbryter), hover och mouseDrag står still
    //   Finishing  släpp/avbrott pågår: drop-/end-handlers körs. Ett nytt
    //              avbrott härifrån (en modal dialog i `onDrop` vars nästlade
    //              pump levererar WindowFocusLost) är en NO-OP
    //
    // PEKARNA ÄR LIVSFARLIGA och skyddas på ETT sätt: `detachTree` nollar
    // `source`/`target` när de lämnar trädet, och varje användaranrop följs
    // av en kontroll mot SESSIONENS fält — aldrig mot en lokal kopia, som
    // kan dingla om handlern förstörde widgeten.
    struct DragSession {
        enum class Phase { Idle, Pending, Active, Finishing };
        Phase       phase   = Phase::Idle;
        bool        starting = false;    // starten pågår (se Pending ovan)
        Widget*     source  = nullptr;   // den tryckta widgeten
        Widget*     target  = nullptr;   // aktuellt mål (allowDrop)
        float       startX  = 0.0f, startY = 0.0f;   // trycket, fysiska px
        float       x       = 0.0f, y = 0.0f;        // pekaren senast
        DragData    data;                            // lasten
        DragEffect  allowed = DragEffect::None;      // källans mask
        DragEffect  effect  = DragEffect::None;      // målets val, validerat
        std::string text;                            // dragbilden
        ImageSource img{};
        // AUTOSKROLLEN: `kDragScrollTimerId` går.
        // SIST, så `drag_ = DragSession{}` nollar den med resten — timern
        // stoppas av `dragEnd`/`dragCancel` och av ett sent tick.
        bool        scrolling = false;
    };
    using DragPhase = DragSession::Phase;
    DragSession drag_;

    void    dragArm(Widget* w, float x, float y);    // vänstertryck
    bool    dragThresholdPassed(float x, float y) const;
    bool    dragBegin(float x, float y);             // true = draget startade
    void    dragMotion(float x, float y);            // ny pekarposition
    // Autoskrollen: kedjan från widgeten under (x, y) uppåt (utan roten)
    // frågas med `Widget::dragScroll` — första true vinner. `apply` rullar.
    bool    dragScrollChain(float x, float y, bool apply);
    void    dragScrollTick();                        // timerns steg
    void    dragRelease(float x, float y);           // knappen släppt
    void    dragCancel();                            // Escape/fokusförlust
    void    dragOutside();                           // MouseLeave
    // Målet under en punkt: första förfader-eller-jag med `allowDrop` och
    // aktiverad; nullptr utanför klientytan.
    Widget* dropTargetAt(float x, float y);
    void    dragEnterTarget(Widget* t);
    void    dragOverTarget();
    void    dragLeaveTarget();
    void    setDropEffect(Widget* t, DragEffect e);
    void    dragEnd(DragEffect effect, Widget* target);   // källans slut
    DragArgs makeDragArgs(Widget& w, const Ctx& c) const;
    void    drawDragFeedback(Ctx c);   // overlay-lagret (src/drag_drop.cpp)

    // ------------------------------------------------ TOOLTIPEN
    // Texten bor på widgeten (`Widget::setToolTip`); VISNINGEN bor här,
    // per fönster, som drag-sessionen — implementationen i src/tooltip.cpp.
    //
    // TILLSTÅNDEN: VILA (inget på gång) → VÄNTAR (`pending` — timern
    // `kToolTipTimerId` löper för `source`) → VISAS (`shown` — popupen är
    // uppe). Varje rörelse över samma källa startar om timern: tooltipen
    // kommer när pekaren VILAR, inte när den passerar.
    //
    // PEKARNA ÄR WIDGETAR i trädet och nollas av `detachTree` när de lämnar
    // det — samma skydd som drag-sessionens.
    //
    // KÄLLAN ÄR EN WIDGET OCH EN TEXT: `Widget::toolTipAt`
    // svarar per DEL, så två delar i samma widget är två källor — `text` är
    // tokenet som väntar eller visas, och `blockedText` det som tystades.
    struct ToolTipState {
        Widget* source  = nullptr;   // vars tooltip som väntar eller visas
        Widget* shown   = nullptr;   // vars tooltip som VISAS (popupen uppe)
        Widget* blocked = nullptr;   // tystad efter klick/tangent tills
                                     // pekaren byter källa (Windows)
        bool    pending = false;     // timern löper
        bool    warm    = false;     // en tooltip syntes nyss — nästa väntar
                                     // bara `toolTipReshowMs`
        float   x = 0.0f, y = 0.0f;  // pekaren senast, fysiska px
        TrText  text;                // källans text (`source`/`shown`)
        TrText  blockedText;         // den tystade delens text (`blocked`)
    };
    ToolTipState                   tip_;
    std::unique_ptr<ToolTipWindow> tipWin_;   // skapas lat, återanvänds

    // Källan för en widget under pekaren: den själv eller närmaste förfader
    // vars `toolTipAt` vid (x, y) svarar med en icke-tom text (routed
    // lookup), och den texten. Tom `widget` = ingen.
    struct ToolTipSource {
        Widget* widget = nullptr;
        TrText  text;
    };
    ToolTipSource toolTipSourceAt(Widget* over, float x, float y);
    void    toolTipMotion(Widget* over, float x, float y);   // MouseMotion
    // Göm och sluta vänta. `blockera` tystar källan tills pekaren byter
    // (klick och tangent gör det, fokusförlust och pekaren-ut gör det inte).
    void    toolTipCancel(bool blockera);
    void    toolTipTimer();                    // fördröjningen löpte ut
    void    toolTipShow(Widget* src);
    void    toolTipChanged(Widget& w);         // Widget::setToolTip
    void    toolTipClosed();                   // popupens onClose

    // ------------------------------------------------------ träd-vandring
    // EN sanning om geometrin: ritning, hit-test, damage och
    // ctxFor gör SAMMA Ctx-vandring — containerns childArea byter area,
    // clipsChildren/clipRect klipper. Roten specialbehandlas: dess barn
    // resolvar direkt mot fönstrets innehållsyta (c.area ur makeCtx).

    // Hit-test: rekursiv, bakifrån per nivå (sist = överst); klippta barn
    // träffas inte utanför klippet.
    Widget* widgetAt(float mx, float my, const Ctx& c);
    Widget* widgetAtRec(Widget& w, const Ctx& c, float mx, float my);
    // `w.frontChild()` när det är ett av w:s barn, annars nullptr.
    static Widget* frontChildOf(const Widget& w);

    // Ctx:t en widget resolvar mot: förälderkedjans childArea-vandring
    // från roten ner till widgetens förälder. Behavior-krokar (mouseDown/
    // mouseDrag/caretBlink/mouseWheel) MÅSTE få det här — aldrig rå makeCtx.
    Ctx ctxFor(const Widget& w);

    // SYNS widgeten i trädet? Den själv och varje förfader synlig, och varje
    // förälder visar sina barn — en widget i en INAKTIV FLIK är synlig men
    // ligger under en förälder som svarar `showsChildren() == false`.
    // Tooltipens fråga (källan ska synas) och standardknappens (en
    // knapp i en dold flik tar inte Enter): EN definition.
    static bool shownInTree(const Widget& w);

    // FOKUSOMBUDETS KEDJA: `w` följt genom `focusProxy()` så länge
    // ombudet är en STRIKT ättling — dit fokus landar när det sätts på `w`.
    // `setFocus` och popupvägen (`openPopupKeyTarget`) delar den.
    static Widget* proxyTarget(Widget* w);

    // Rit-vandringarna (full resp. per damage-rect) + damage-insamlingen
    // (klipper mot förälderkedjans klipp — clipAcc, fysiska px).
    void stepTree(Widget& w);   // frameStep, före skadan
    void drawTree(Widget& w, const Ctx& c);
    void drawTreePartial(Widget& w, const Ctx& c, const gfx::Rectf& dmg);
    void collectDamage(Widget& w, const Ctx& c, const gfx::Rectf& clipAcc,
                       std::vector<gfx::Rectf>& rects);

    // Djupet-först över trädet (hover/MouseLeave/fokuslistan).
    template <class Fn>
    void forEachWidget(Widget& w, Fn&& fn)
    {
        for (const auto& ch : w.children()) {
            fn(*ch);
            forEachWidget(*ch, fn);
        }
    }
    void collectFocusable(Widget& w, std::vector<Widget*>& out);

    // FÖNSTERSTÄDNINGEN när ett delträd lämnar trädet.
    // Anropas av `Widget::unadopt`/`releaseOwned` — `Widget` är redan
    // `friend`, precis som för `Widget::focus()`.
    //
    // ORDNINGEN ÄR BINDANDE och anropet sker FÖRE trädmutationen:
    // `setFocus(nullptr)` går via `ctxFor(*old)`, alltså en vandring uppåt
    // genom förälderkedjan för att släcka careten, och kedjan måste vara
    // intakt då.
    //
    // Vad som städas och var det bor:
    //   fokus/press/mitten- och    FÖNSTRET  — nollas/raderas för delträdet
    //   högertryck/dubbelklick/                (trycken utan …Up och utan
    //   capture/grab/gesten                    mouseCancel, gesten utan End;
    //                                          tangenternas minne har vakter)
    //   lastRect_ (damage)         WIDGETEN  — de LÄMNADE pixlarna skördas
    //                                          till `extraDamage_` först
    //   hovered_/pressed_/focused_ WIDGETEN  — nollas, så ett åter-adopterat
    //                                          träd inte kommer tillbaka
    //                                          "varmt"
    // `menuKeyHook_` rörs INTE: ett delträd som är värd för en öppen
    // presentation är ett DENY-fall och kommer aldrig hit.
    void detachTree(Widget& w);

    // RIVNINGSVÄGEN FÖR INNEHÅLLET — `detachTree`s motsvarighet för ett
    // innehåll som DOG utan att
    // lossas: en lånad flytande dockningspanel — eller ett flytande
    // verktygsfält — som förstördes medan dess fönster
    // lever. Varje widgetpekare fönstret håller (fokus, tryck, dubbelklick,
    // mitten- och högertryck, gest, capture, grab, drag, tooltip,
    // tangenternas minne) släpps UTAN att en
    // widget rörs — ingen krok, ingen förälderkedja (en död widgets
    // `parent()` går inte att läsa) och ingen användarkod — och fönstret
    // ritas om helt. Bara det flytande dockningsfönstret (`friend class
    // DockFloatWindow`), vars innehåll är objektet (och fältets rubrik, som
    // fönstret äger och som överlever det).
    void forgetWidgets();

    // FÖNSTRETS INBYGGDA får aldrig lämna trädet: `topBar_`, `menuBar`,
    // `statusBar` och `appIcon` läses OVILLKORLIGT av `chromeInsetH()`,
    // `buildTopBar`s wiring och namnlistens motvikt, och de tre senare är
    // dessutom VÄRDE-medlemmar som fönstret äger — ett `unadopt` hade
    // lämnat en pekare kvar i chrome-matematiken och en `releaseOwned` hade
    // varit omöjlig ändå.
    //
    // Frågas i `Widget::validateRelease`s STRUKTUR-kontroll (samma kategori
    // som `PropertyGrid::editor_`), alltså FÖRE all mutation. Den kan inte
    // uttryckas som en `canReleaseChild`-override, för fönstrets rot är en
    // vanlig `Panel` och inte en egen typ.
    bool isBuiltinMember(const Widget& w) const;

    template <class T>
    static T* findIn(Widget& parent, const std::string& name)
    {
        for (Widget* ch : parent.children()) {
            if (ch->name == name)
                return dynamic_cast<T*>(ch);
            if (T* t = findIn<T>(*ch, name))
                return t;
        }
        return nullptr;
    }

    // Caret-timern (textwidgets): AppWindow äger den — startas när en
    // widget med wantsTextInput() får fokus, stoppas vid fokusförlust och
    // WindowFocusLost. restartCaretTimer börjar om perioden med synlig fas
    // (anropas via Widget::restartCaretBlink när careten flyttats).
    void    restartCaretTimer(Widget* w);

    // Render-callbackets innehållspass: samlar damage (per-widget dirty →
    // union gammal+ny rect) och ritar fullt eller per damage-rect med
    // klipp. Chromet ligger HELT utanför (ritas per present ovanpå).
    void renderContent(gfx::Window& w);
    void recordDamage(const std::vector<gfx::Rectf>& rects);  // debug-läget
    void drawDebugDamage(const Ctx& c);   // ramverkets overlay-lager

    // OVERLAY-VANDRINGEN: `drawOverlay` på varje synlig
    // widget som svarar `hasOverlay()`, i trädordning, med `draw()`s Ctx och
    // förfädernas klipp (rektanglar OCH radier, ritvandringens) inom ytan
    // trädet ritas i. Körs i overlay-callbacken efter appens `overlay()`.
    void drawWidgetOverlays(const Ctx& c);

    TrText      title_;
    int         initW_, initH_;   // LOGISKA px
    // Begärd INNEHÅLLSSTORLEK, logiska px; 0 = ingen begärd.
    // Lagras i stället för att räknas ut på plats — se setContentSize.
    int         contentW_ = 0, contentH_ = 0;
    // Storleksgränserna, logiska px; 0 = obegränsat.
    // EN lagrad sanning: `showOwned` kopierar den till `WindowDesc`,
    // settrarna efter show() skickar den till OS-fönstret, och
    // `pushHitRegions` läser den för att stänga av resize-greppen.
    // GRÄNSENS FORM: med `minInContent_`/`maxInContent_` är
    // `minH_`/`maxH_` INNEHÅLLSMÅTT (`set…ContentSize`), och `appMinH`/
    // `appMaxH` lägger på chromets höjd när gränsen används — EN väg, så
    // `effMin*`/`effMax*`, läsarna och `isFixedSize` aldrig räknar olika.
    int         minW_ = 0, minH_ = 0, maxW_ = 0, maxH_ = 0;
    bool        minInContent_ = false, maxInContent_ = false;
    // FÖNSTRET FÖLJER INNEHÅLLET. `fitWanted_` är
    // det ÖNSKADE (oavrundade) innehållsmåttet vid den senaste anpassningen —
    // ritningens fråga jämför mot det, så en halv pixel aldrig passar om i en
    // slinga (`DockFloatWindow::fitWanted`s form). `fitClientW_/H_` är den
    // anpassade klientstorleken på de anpassade axlarna (0 = fri) — de
    // effektiva gränserna; i `GrowOnly` är det INNEHÅLLETS
    // klientmått, alltså GOLVET, och 0 när innehållet saknar mått på axeln.
    // `sizeRequestNew_`: en `setContentSize`/`setClientSize` som ännu inte
    // anpassats — den ger den fria axeln, och i `GrowOnly` storleken på den
    // anpassade, sedan gäller fönstrets aktuella (användarens drag vinner).
    SizeToContent             sizeToContent_ = SizeToContent::Manual;
    SizeToContentMode         sizeToContentMode_ = SizeToContentMode::GrowAndShrink;
    std::optional<gfx::Sizef> fitWanted_;
    int                       fitClientW_ = 0, fitClientH_ = 0;
    bool                      sizeRequestNew_ = false;
    bool                      refitPosted_    = false;
    bool                      fitEmptyLogged_ = false;
    // onResize: kön med vakter (en ny widget på samma adress är ingen
    // dubblett), den ritade storleken per post (en storlek som ändrats sedan
    // ritningen väntar på nästa — ingen leverans av oritad geometri),
    // EN postad leverans åt gången och kedjespärren — omgångar i följd
    // med en körd handler och ingen inmatning emellan (`App::eventSeq_`).
    struct ResizeEntry {
        Widget*              w;
        Widget::HandlerGuard guard;
        gfx::Sizef           drawn;
    };
    std::vector<ResizeEntry> resizeQueue_;
    bool                     resizePosted_   = false;
    int                      resizeChain_    = 0;
    std::uint64_t            resizeChainSeq_ = 0;
    bool                     active_ = false;   // isActive()
    int         initX_ = gfx::kWindowPosDefault;   // skärmpos vid show —
    int         initY_ = gfx::kWindowPosDefault;   // default: OS:et väljer
    gfx::Rectf  anchorRect_{};      // popup-ankare (setPopupAnchor) —
    bool        hasAnchor_ = false; // ägarens klient-fysiska px
    gfx::Chrome chrome_;

    // RAMEN, PLATSEN OCH LÄGET. `initX_`/`initY_` ovan
    // är placeringsrymden och bara systemfältsmenyns värds (friend); det
    // appen begär står här, i LOGISKA px, och räknas om när fönstret öppnas
    // (`showOwned`, src/window_placement.h).
    StartPosition    startPosition_ = StartPosition::Default;
    int              locX_ = 0, locY_ = 0;   // `setLocation` före show
    bool             hasLocation_ = false;
    gfx::WindowState startState_  = gfx::WindowState::Normal;
    // `setChrome` på ett ÖPPET fönster — tillämpas när det öppnas nästa gång.
    std::optional<gfx::Chrome> nextChrome_;
    // KORTET: geometrin när fönstret stängdes — `geometry()` efter
    // stängningen. Tas i `App::executeClose` medan OS-fönstret finns och
    // nollas när fönstret öppnas igen.
    std::optional<WindowGeometry> closedGeometry_;
    // Geometrin ur OS-fönstret (`gfx::Window::placement`), positionen i
    // logiska px. Kräver att OS-fönstret finns — `geometry()` och kortet.
    WindowGeometry liveGeometry() const;
    // BEGÄRAN (före öppningen och före den första visningen) —
    // `geometry()` och kortet för ett fönster som stängdes utan att ha synts.
    WindowGeometry requestedGeometry() const;
    // STARTLÄGET som det tillämpas: Normal för popup och offscreen, och
    // en modal startar aldrig minimerad. `showOwned` och läsarna före den
    // första visningen delar regeln.
    gfx::WindowState effectiveStartState() const;
    // RAMEN: sätter `chrome_`, bygger namnlisten LATT vid Custom och
    // synkar — `setChrome` före show och `nextChrome_` vid nästa öppning.
    void applyChrome(gfx::Chrome c);
    // Namnlistens synlighet är EN regel: Custom och inte transparent.
    void syncTopBar();

    std::unique_ptr<gfx::Window> win_;    // skapas i konstruktorn (fabriken)
    std::unique_ptr<gfx::Font>   font_;   // skapas i show() (hör till renderern)
    // ROLLFONTEN (Label::Role::Heading). Skapas i samma reloadFont() och
    // MÅSTE rivas i samma block som font_ i App::executeClose — en
    // font-atlas är en TEXTUR och hör till renderern, så en kvarlevande
    // rollfont kraschar teardown (inte första körningen; därför står det
    // här).
    std::unique_ptr<gfx::Font>   headingFont_;
    std::unique_ptr<ImageCache>  images_; // skapas i show(), rivs FÖRE font_
    // Ritytans GPU-resurser — skapas efter images_, rivs
    // FÖRE den; `trim()` överst i renderContent (src/canvas/resources.h).
    std::unique_ptr<CanvasResources> canvas_;
    // TEXTFONTERNA — `Ctx::textFonts`, LATA per variant.
    // Skapas i show() efter canvas_, och MÅSTE rivas i samma block som font_
    // i App::executeClose, före window().shutdown(): fonterna hör till
    // renderern (en font-atlas är en TEXTUR, rollfontens regel).
    std::unique_ptr<FontSet>     fontSet_;
    gfx::DisplayMetrics          display_;   // skärmens mm (measureDisplay)
                                          // (texturer hör till renderern)
    // OBS: ingen stil/tema/läge HÄR — utseendet är app-globalt
    // (App::themeState) så alla fönster följer samma val.

    // Den osynliga ROT-panelen: äger fönstrets widget-träd.
    // Ritas/resolvar ALDRIG själv — barnen får innehållsytan direkt.
    // Täcker contentArea, inte klientytan — UTOM TopBar (första barnet),
    // som vandringarna placerar mot CHROME-ctx:t (hela klientytan).
    Panel       root_;

public:
    // TAB-ORDNINGEN som ett LÄSBART värde: de
    // fokuserbara widgetarna i exakt den ordning Tab/Shift+Tab besöker dem
    // — djupet-först, sedan STABILT sorterad på `tabIndex` där de
    // auto-satta bär sin egen listposition som nyckel.
    //
    // `moveFocus` konsumerar den, så det finns EN sanning om ordningen.
    // Den är publik för att kunna GRINDAS UTAN ett tangenttryck:
    // `collectFocusable`/`moveFocus` är privata, och komposit-stoppet
    // (`Widget::focusesChildren`) hade annars bara kunnat mätas genom att
    // mata Tab genom `handleEvent` (protected). Introspektion,
    // inte tillstånd — den har med flit ingen `.glazeui`-form.
    std::vector<Widget*> focusOrder();

    // STOPPET Tab (`dir` +1) eller Shift+Tab (−1) landar på härifrån — det
    // `moveFocus` sedan fokuserar (EN sanning, publik
    // av samma skäl som `focusOrder()`). Var vi STÅR är det fokuserade
    // ELLER dess närmaste förfader i listan: ett fokusombud
    // (`Widget::focusProxy`) står aldrig själv i listan, kompositen gör det.
    // nullptr när fönstret saknar stopp.
    Widget* nextFocus(int dir);

    // FÖNSTRET UTAN FOKUS: ingen widget har
    // tangentbordet — WinForms `ActiveControl = null`. Tangenterna går då
    // bara till fönstrets acceleratorer, text till ingen, och nästa Tab
    // landar på första stoppet. För den som döljer innehåll med fokus i och
    // inte har något annat stopp att lämna det till (dockningsvärdens
    // fokusvakt). Fokushändelserna fyras som vid varje annat byte.
    void clearFocus() { setFocus(nullptr); }

    // PÅGÅR ETT DRAG i det här fönstret? Introspektion och inte
    // tillstånd, `focusOrder`s sort: sant från att tröskeln passerades och
    // `onDragStart` gav en last till att `onDragEnd` har körts. En app kan
    // läsa den för att hålla tyst under ett drag (en tooltip, en egen
    // hover-effekt i `frame()`), och grinden läser den för att se att ett
    // drag faktiskt startade — eller inte.
    bool isDragging() const
    {
        return drag_.phase == DragSession::Phase::Active ||
               drag_.phase == DragSession::Phase::Finishing;
    }

    // VEMS TOOLTIP VISAS just nu i det här fönstret? nullptr när
    // ingen gör det. Introspektion och inte tillstånd, `isDragging`s sort —
    // det är den widget vars TEXT står i popupen, alltså källan efter den
    // routade uppslagningen (en komposits inredning visar kompositens).
    Widget* toolTipWidget() const { return tip_.shown; }

    // MODIFIERARNA i den senaste mus-, tangent-, hjul- eller gesthändelsen
    // fönstret tog emot — `gfx::kMod*`-masken (WinForms
    // `Control.ModifierKeys`). En widgets MUSkrokar har inga args att bära
    // dem i (`mouseDown(ctx, x, y)`), och en ny parameter hade rört varje
    // widget; listvyns Ctrl-klick och Shift-klick läser dem HÄR, under sin
    // `mouseDown`, och ritytan under sin `mouseWheel` (Ctrl/Cmd +
    // styrplattans drag kretsar). Värdet är det eventet
    // BAR (fasadens kontrakt vid `gfx::Event::mods`) — en modifierartangents
    // eget event bär läget EFTER tangenten, så en tryckt Ctrl ENSAM syns
    // direkt på alla plattformar. Fönsterhändelser och text rör den inte.
    // Introspektion och inte tillstånd — ingen `.glazeui`-form.
    std::uint32_t modifierKeys() const { return modifiers_; }

    // VAR STÅR PEKAREN? — den senast kända positionen i
    // KLIENTYTAN, i fysiska px (mus-krokarnas rymd), eller nullopt när
    // fönstret inte är öppet eller pekaren har lämnat det (`MouseLeave`).
    // Under ett tryck med capture kan punkten ligga UTANFÖR klientytan: den
    // är pekarens verkliga läge, inte en klampad. För den som måste veta om
    // pekaren står över en YTA utan att få en händelse när den lämnar den —
    // dockningsvärdens lämna-prövning frågar om pekaren står över
    // overlayn. `hovered()` räcker inte där: den säger vilken AKTIVERAD
    // widget som är överst, och över ett inaktiverat barn är ingen hovrad.
    // `modifierKeys()`s sort — introspektion och inte tillstånd, ingen
    // `.glazeui`-form.
    std::optional<gfx::Pointf> mousePosition() const
    {
        if (!open_ || !mouseInside_)
            return std::nullopt;
        return lastMouse_;
    }

    // HJULETS SORT i den senaste `MouseWheel` fönstret tog emot —
    // `modifierKeys()`s mönster och skäl: `mouseWheel`-kroken
    // har inga args att bära den i, och en ny parameter hade rört varje
    // override. `isPreciseWheel()` är STYRPLATTANS TVÅFINGERSDRAG (raderna
    // är då logiska px / `gfx::kPrecisePxPerRow`, kontraktet i gfx/events.h)
    // och `wheelPhase()` dess fas — Begin/Update/End när OS:et vet den,
    // `Momentum` för OS:ets egen tröghet, None för ett mushjul. Satt FÖRE
    // appens `event()`-krok, så också den läser rätt värde. Ritytan
    // panorerar på ett precist hjul och zoomar på ett mushjul; en lista
    // rullar på båda. Introspektion och inte tillstånd — ingen `.glazeui`-form.
    bool              isPreciseWheel() const { return wheelPrecise_; }
    gfx::GesturePhase wheelPhase() const { return wheelPhase_; }

    // HÅLLS TANGENTEN NERE? (`modifierKeys()`s mönster och
    // skäl) — för det som pågår SÅ LÄNGE en tangent hålls och frågas PER
    // BILDRUTA: förstapersonens WASD i ritytans `onTick`, och ritytans
    // tickning, som lever medan en tangent appens `onKeyDown` tog fortfarande
    // hålls. Ett läge som slås på och av av tangenten själv lyssnar i stället
    // på `onKeyDown`/`onKeyUp` — släppet kommer till samma
    // kedja som trycket, och fokusförlusten ger ett eget. Mängden är redan
    // uppdaterad när `onKeyUp` körs: inne i handlern svarar `isKeyHeld` nej.
    //
    // MÄNGDEN uppdateras FÖRE appens `event()`-krok — också för ett event
    // kroken sedan äter: `KeyDown` sätter, `KeyUp` tar bort, och den TÖMS
    // när fönstret tappar fokus, döljs eller stängs. Släppet av en tangent
    // som hölls när fokus gick kommer aldrig hit, och en kvarglömd tangent
    // hade hållit ritytan tickande (regel 1).
    //
    // DEN OVIKTA TANGENTEN lagras — den `event()`-kroken och acceleratorerna
    // ser (`gfx::Key`-kontraktet) — så `isKeyHeld(Key::KP_End)` svarar BARA
    // för numpadens End. Men en widget fick den VIKTA i sin `keyDown`
    // (`KeyArgs::key`), och frågan med en DEDIKERAD tangent svarar därför som
    // widgeten ser den: `isKeyHeld(Key::End)` är sann för End OCH för
    // numpadens End i navigationsläget (`gfx::navigationKey`). Numpadens två
    // lägen är EN fysisk tangent — ett släpp i det andra läget (Shift vänder
    // NumLock på Windows mitt i ett tryck) släpper båda.
    //
    // `Key::Unknown` hålls aldrig (det är många tangenter i en post). En post
    // per `Key` och inte per fysisk tangent: två tangenter med samma symbol
    // (Enter och numpadens Enter) släpps av den som släpps först. Byts
    // layouten mitt i ett tryck kan släppet komma som en annan symbol, och
    // då står tangenten kvar till nästa fokusförlust. Introspektion och inte
    // tillstånd — ingen `.glazeui`-form.
    bool isKeyHeld(gfx::Key k) const;

    // STATUSRADEN — publikt värde-fält. Deklarerad EFTER
    // `root_` med flit: medlemmar rivs i OMVÄND deklarationsordning, så
    // raden dör FÖRE roten — samma ordning som fönsterklassens egna
    // genererade värde-medlemmar har, och roten rör aldrig lånade barn i
    // rivning (`children_` är pekare, `ownedChildren_` äger).
    //
    // Adoptionen sker i konstruktorn, LÅNAD, direkt efter TopBar-bygget.
    StatusBar   statusBar;

    // MENYRADEN — publikt värde-fält, statusBar-
    // prejudikatet hela vägen: skapas med fönstret, adopteras LÅNAD i
    // konstruktorn och är DOLD tills något gör `menuBar.show();`.
    //
    // ADOPTIONSORDNINGEN är deterministisk och skriven: efter TopBar
    // (chromet byggs först) och FÖRE `statusBar`. De två kan aldrig
    // krocka geometriskt — menyraden dockar Top och statusraden Bottom,
    // så dock-carvingen skär i var sin kant — men ordningen fastställs
    // ändå, för barnordningen ÄR carve-ordningen och en oskriven ordning
    // är en ordning som kan ändras av misstag.
    //
    // Den ligger i KLIENTYTAN och inte i chromet: den får ingen
    // Caption-roll, och `pushHitRegions` (som bara särbehandlar `topBar_`)
    // behöver inte röras alls.
    //
    // Deklarerad EFTER `statusBar` så den rivs FÖRE den — och båda före
    // `root_`, av samma skäl som statusraden.
    MenuBar     menuBar;

    // PROGRAMIKONEN och PROGRAMMENYN — publikt värde-fält,
    // menuBar/statusBar-prejudikatet med en skillnad: den är
    // SYNLIG som förval, för varje fönster har en ikon (GlazeUI-ikonen när
    // appen inte valt någon, `appIcon.setNoIcon(true)` för ingen alls).
    // Adopteras LÅNAD SIST i konstruktorn och ligger i CHROME-trädet som
    // TopBar: rotbarn utanför dock-flödet, placerad mot hela klientytan
    // (`isChromeRoot`). Klickbar, inte dragyta (`pushHitRegions` ger den
    // `HitRole::Client`). `appIcon.hide()` döljer den i fönstret men lämnar
    // OS-ikonen kvar. Deklarerad sist så den rivs först, före `root_`.
    AppIcon     appIcon;

private:
    // Caption-barerna — TopBar är ägd av roten och
    // pekaren ett uppslag; StatusBar är fältet ovanför. TopBar byggs i
    // ctorn vid Chrome::Custom (buildTopBar).
    TopBar*    topBar_    = nullptr;
    bool controlBox_ = true, minimizeBox_ = true, maximizeBox_ = true;

    void buildTopBar();         // plattforms-defaults + wiring (OS-check)
    void applyCaptionBoxes();   // controlBox/min/max → show/hide

    // CHROME-TRÄDETS RÖTTER: `topBar_` och `appIcon`
    // resolvas mot chrome-ctx:t = HELA klientytan, inte innehållsytan. EN
    // fråga som de sex vandringarna ställer (skadan, full och partiell
    // ritning, widgetarnas lager, hit-testen och `ctxFor`) — ett glömt
    // ställe placerar ikonen en namnlist för lågt i just den vandringen.
    bool isChromeRoot(const Widget& w) const;

    // OS-fönstrets ikon ur `appIcon`: ingen
    // (`noIcon`), appens bild, eller förvalet med `programFirst` — ramarna
    // ur src/window_icon.h. Anropas efter `win_->init` i `showOwned` och av
    // AppIcons settrar medan fönstret är öppet; aldrig för en popup (ingen
    // aktivitetsfältspost att visa den i).
    void applyWindowIcon();

    // Hit-test-kartan: räknas ur trädet med samma
    // geometri som ritningen och trycks ner per repaint (+ vid show).
    void pushHitRegions(const Ctx& chromeCtx);

    Widget*     pressedWidget_ = nullptr;
    Widget*     focusedWidget_ = nullptr;

    // MITTENTRYCKET: widgeten vars `middleDown` tog det
    // — den får `middleDrag` och `middleUp` tills släppet. Nollas UTAN
    // `middleUp` vid avbrottet (`dropPointer` — med `mouseCancel(Middle)`), i
    // `detachTree` (tyst) och när fönstret visas på nytt (ett nytt liv börjar
    // utan drag, `drag_`s regel). `Widget::middlePressed()` läser den här —
    // ingen kopia på widgeten.
    Widget*     middleWidget_  = nullptr;
    // HÖGERTRYCKET — `middleWidget_`s tvilling i allt:
    // widgeten vars `rightDown` tog trycket; `Widget::rightPressed()` läser den.
    Widget*     rightWidget_   = nullptr;
    // `auxSeq(b)`: mitten- och högertryckets sekvensidentitet.
    std::uint64_t middleSeq_ = 0;
    std::uint64_t rightSeq_  = 0;
    // KNAPPARNA SOM HÅLLS i fönstret, en bit per `gfx::MouseButton` —
    // satta och släckta av tryck och släpp FÖRE appens krok, nollade av
    // avbrottet och när fönstret öppnas, och övertagna av
    // `takePointerCapture`. Avbrottet läser dem: en ANNAN fångande widget får
    // `mouseCancel(Left)` bara med vänsterknappen nere, annars `None` —
    // `pressedWidget_` duger inte, för dockningens fall A flyttar den tryckta
    // till ett annat fönster mitt i trycket (`detachTree` nollar den).
    std::uint8_t buttonsDown_  = 0;

    // `modifierKeys()` — masken ur senaste mus-/tangenthändelsen.
    std::uint32_t modifiers_ = 0;

    // `isKeyHeld()` — de hållna tangenterna, en bit per `gfx::Key` (den
    // OVIKTA), satta och släppta av `setKeyHeld` i `handleEvent` före appens
    // krok, och tömda vid fokusförlust, i `hide()` och i `App::executeClose`.
    // `gfx::kLastKey` är enumets sista post, så mängden växer med enumet.
    std::bitset<static_cast<std::size_t>(gfx::kLastKey) + 1> heldKeys_;
    void setKeyHeld(gfx::Key k, bool held);
    // Hålls `raw` eller dess numpadtvilling i mängden (den OVIKTA posten)?
    bool heldRaw(gfx::Key raw) const;

    // ------------------------------- TANGENTERNAS KEDJOR
    // En LÄNK är en widget i fokusenheten med sin vakt, tagen när kedjan
    // räknades — en widget som dör, lossas eller byter fönster före släppet
    // hoppas över. KEDJAN är fokusenheten INNERST FÖRST.
    struct KeyLink {
        Widget*              w;
        Widget::HandlerGuard guard;
    };
    using KeyChain = std::vector<KeyLink>;
    // Fokusenheten från `start`: `start` och de närmaste förfäderna i rad som
    // är kompositer i Tab-mening (`focusesChildren && focusable && isEnabled
    // && isVisible`) — fönstrets rot aldrig, utom som `start` (fönstret utan
    // fokus). Inte `const`: länkarnas vakter tar
    // muterbara widgets.
    KeyChain keyUnit(Widget& start);
    // Enheten en tangent går till NU: från den fokuserade widgeten om den
    // syns och är aktiverad, från roten om ingen har fokus, annars tom.
    KeyChain currentKeyUnit();
    // MINNET — per hållen tangent kedjan som tryckets `onKeyDown` gick
    // till, så att släppet når samma kedja var fokus än står då.
    //   raw        tangenten ur eventet, OVIKT (numpadens två lägen paras
    //              med `numpadTwin`)
    //   key        den VIKTA som `onKeyDown` fick — `onKeyUp` får samma
    //   chain      fokusenheten vid trycket, innerst först
    //   recipient  index i kedjan för den som TOG tangenten (vars handler
    //              satte `handled`) — den får `keyUp`; 0 = den innersta
    //   skipped    handlarna hoppades över (en öppen popup i enheten) — då
    //              också vid släppet, så paret håller
    //   id         postens identitet: en handler kan ha tömt minnet (en modal
    //              dialog ger fokusförlust), och posten letas upp igen efteråt
    // En post skapas av ett NYTT tryck och tas ut av släppet, av fokusförlusten
    // och av `hide()` — eller slängs tyst när appens `event()` äter släppet och
    // när fönstret stängs. OS:ets UPPREPNING går till postens kedja och skapar
    // ingen ny (den som får
    // upprepningens `onKeyDown` får släppet); en upprepning UTAN post (trycket
    // åts eller gjordes i ett annat fönster) skapar den — så får också den
    // kedjan sitt släpp.
    struct KeyTarget {
        gfx::Key      raw = gfx::Key::Unknown;
        gfx::Key      key = gfx::Key::Unknown;
        KeyChain      chain;
        std::size_t   recipient = 0;
        bool          skipped   = false;
        std::uint64_t id        = 0;
    };
    std::vector<KeyTarget> keyTargets_;
    std::uint64_t          keyTargetIds_ = 0;
    // Posten för `raw` eller dess numpadtvilling, uttagen ur minnet — eller
    // bara uppslagen (pekaren gäller tills minnet ändras: kopiera före en krok).
    std::optional<KeyTarget> takeKeyTarget(gfx::Key raw);
    const KeyTarget*         findKeyTarget(gfx::Key raw) const;
    bool                     hasKeyTarget(gfx::Key raw) const;
    // Släppet till en kedja: handlarna innerst först (stoppade av `handled`),
    // sedan `keyUp` på mottagaren om ingen satte `handled`, sist omritning om
    // något kördes. false = fönstret dog under en handler.
    bool deliverKeyUp(const KeyTarget& t, KeyArgs a);
    // FOKUSFÖRLUSTENS OCH DÖLJANDETS SLÄPP: varje post får ett eget
    // släpp med `cancelled` och mods 0, äldst först. Anroparen tar posterna ur
    // minnet INNAN någon handler körs (`std::exchange`). false = fönstret dog.
    bool releaseKeyTargets(std::vector<KeyTarget> poster);
    // POPUPENS FÖRMEDLING: ett släpp en popup
    // inte såg trycket för — Wayland ger popupytan tangentbordet när den
    // grabbar, medan tangenten hölls i ägaren, och ägaren får ingen
    // fokusförlust (fokusgruppen) — går till närmaste ägare som minns
    // tangenten, annars hade en rityta där tickat för evigt. true = en ägare
    // tog det; efteråt kan fönstret vara rivet (anroparen frågar livstecknet).
    bool forwardKeyUp(const gfx::Event& e);
    // POPUPEN SOM TAR TANGENTBORDET (`setPopupTakesKeys`) — åt
    // andra hållet: ägaren skickar tangenterna NED till sin öppna popup med
    // attributet (`openKeyPopup`), FÖRST i `handleEvent`, med popupens
    // `windowId` genom dess egen ingång. `keysToPopup_` minns vilka tryck som
    // gick BARA dit (en bit per OVIKTAD `gfx::Key`), så släppet följer trycket
    // och sväljs när popupen redan stängts; en tangent ÄGAREN höll när popupen
    // öppnades upprepas i popupen men släpps hos båda — tryckets post finns
    // här. true = ägaren rör inget mer (eventet var popupens, eller en
    // handler i popupen rev ägaren).
    AppWindow* openKeyPopup() const;
    bool       forwardToKeyPopup(const gfx::Event& e);
    std::bitset<static_cast<std::size_t>(gfx::kLastKey) + 1> keysToPopup_;

    // `isPreciseWheel()`/`wheelPhase()` — ur senaste hjulet.
    bool              wheelPrecise_ = false;
    gfx::GesturePhase wheelPhase_   = gfx::GesturePhase::None;

    // STYRPLATTANS GEST: widgeten vars `gesture` tog sekvensens
    // Begin — den får sekvensens Update/End/Cancel var pekaren än står, tills
    // End/Cancel. Nollas utan något anrop i `detachTree` (ingen användarkod
    // under en trädmutation) och när fönstret visas på nytt; en ny Begin
    // väljer om (en End som aldrig kom).
    Widget*       gestureWidget_ = nullptr;

    // DUBBELKLICKEN (alla tre knapparna):
    // det FÖRRA trycket per knapp — [0] vänster, [1] mitten, [2] höger —
    // widgeten, tiden och punkten i SKÄRMENS rymd. Ett andra tryck på SAMMA
    // widget inom `gfx::doubleClickMs()` och högst `Style::dragThreshold` bort
    // är ett dubbelklick (`countClick`); trycket EFTER ett dubbelklick börjar
    // en ny följd, som på Windows. Mitten och höger räknas på den widget som
    // TOG trycket. Pekarna nollas i `detachTree` — en widget som lämnar
    // trädet får aldrig ett halvt dubbelklick av en ny widget på samma adress.
    struct ClickTrack {
        Widget*       widget = nullptr;
        std::uint64_t ms     = 0;
        float         x = 0.0f, y = 0.0f;
    };
    ClickTrack clicks_[3];
    // Är trycket på `w` vid (x, y) — fysiska fönster-px — det andra i följden
    // `t`? Följden uppdateras.
    bool countClick(ClickTrack& t, Widget* w, float x, float y);

    // Capture & grab. En widget som lämnar trädet städas av `detachTree`;
    // pekarna kan annars bara bli hängande via fönsterrivning, då hela
    // bokföringen dör tillsammans.
    Widget*              capturedWidget_ = nullptr;
    std::vector<Widget*> grabStack_;

    bool        popup_       = false;   // noActivate + stäng vid klick utanför
    bool        popupTakesKeys_ = false;   // ägaren skickar tangenterna hit
    // ALLTID ÖVERST — BEGÄRAN. `showOwned` kopierar den till
    // `WindowDesc`, sättaren efter show går direkt till OS-fönstret, och
    // gettern läser fönstret när det är öppet (det är där degraderingen
    // syns) och det här fältet dessförinnan.
    bool        topmost_     = false;
    bool        transparent_ = false;   // per-pixel-alfa (tvingar chrome av)
    // RUNDAT FÖNSTER — skilt från `transparent_` fast båda
    // begär per-pixel-alfa: `transparent_` är appens val och släcker
    // chromet, det här är ramverkets och BEHÅLLER det. Sätts i showOwned
    // ur `Style::windowRadius` och styr tre saker: att ytan begärs
    // genomskinlig, att hit-regionerna ändå pushas, och att omritningen
    // alltid går den fulla vägen (alfa kan inte återställas per rect).
    bool        roundedChrome_ = false;
    bool        offscreen_   = false;   // skapas dolt (WindowDesc::hidden)
    bool        isModal_     = false;   // run() pumpar med modal-filtret
    // AKTIVITETSFÄLTET — attributet; `showOwned` kopierar
    // det till `WindowDesc::showInTaskbar`.
    bool        showInTaskbar_ = true;
    // VERKTYGSFÖNSTER utan popupens beteende (`WindowDesc::toolWindow`):
    // ingen knapp och inte i Alt+Tab, men fönstret TAR fokus. Bara
    // systemfältsmenyns värd sätter det (friend) — det är värdens
    // fokusförlust som stänger menyn vid ett klick utanför. De flytande
    // dockningsfönstren sätter det också.
    bool        toolWindow_    = false;
    // AKTIVERINGEN — INTERNA attribut, satta av vännen
    // `DockFloatWindow` före `show()`; `showOwned` kopierar dem till
    // `WindowDesc`. Ingen publik yta och ingen grammatik (`toolWindow_`s
    // prejudikat). `noActivate_` gäller för alltid (det flytande
    // verktygsfältet: ett klick på "Fet" lämnar tangentbordet i textfältet,
    // som VS); `showInactive_` bara den första visningen (varje flytande
    // panel: lossningen mitt i ett drag stjäl inte fokus från fönstret som
    // håller trycket, och vid en dold start behåller huvudfönstret
    // aktiveringen).
    bool        noActivate_    = false;
    bool        showInactive_  = false;

    // Fönstrets liv (ägs av App): open_ = OS-fönstret finns; pendingClose_
    // = stängning begärd, verkställs i App:s pump-svans (efter onClose-
    // vetot); hidden_ = dolt via hide() (öppet men osynligt, ritas inte);
    // loadFired_ = onLoad har fyrats (EN gång per objekt). owner_/owned_
    // är ÄGARSKAPS-relationen (livstid/Z-ordning — aldrig koordinater);
    // kaskaden stänger barnen före ägaren.
    bool        open_         = false;
    bool        pendingClose_ = false;
    // Den väntande stängningens SKÄL — satt av `requestClose`, läst
    // av App:s vetorunda och nollad efter den (veto eller verkställd).
    CloseReason closeReason_  = CloseReason::UserClosing;
    bool        hidden_       = false;
    bool        loadFired_    = false;
    // `onLoad` FÖRE SYNLIGHETEN: OS-fönstret är
    // skapat men har ALDRIG visats — fram till den första visningen gäller
    // startvärdena (`setLocation`, `setStartPosition`, `setWindowState` och
    // fönsterknapparna byter begäran, som `revealHidden` och backendens
    // startläge läser). `loading_` = `onLoad` körs just nu; `showAfterLoad_` =
    // fönstret ska visas när den är klar (`hide()`/`show()` i handlern).
    bool        firstShowPending_ = false;
    bool        loading_          = false;
    bool        showAfterLoad_    = false;
    // DET ÄGDA FÖNSTER SOM AKTIVERADES MEDAN `onLoad` KÖRDE: `show(*this)` i
    // handlern visar och aktiverar det, och den första visningen efter `onLoad`
    // aktiverar sedan ÄGAREN och tar aktiveringen ifrån det. Det
    // senast aktiverade får den tillbaka direkt efter ägarens första visning
    // (`revealHidden`); en dold start släpper posten (`hide()`), liksom ett nytt
    // liv (`showOwned`).
    struct LoadActivated {
        AppWindow*          w = nullptr;
        std::weak_ptr<char> life;   // fönstrets livstecken, taget vid visningen
    };
    LoadActivated loadActivated_;
    // Reentransvakt för showOwned. `open_` duger inte: den sätts FÖRST i
    // slutet av showOwned, så ett `show()` ur en handler som körs mitt i
    // showOwned (popup-stängningens onClose) hade passerat open_-vakten och
    // kört en ANDRA `win_->init()` på samma fönster.
    bool        showing_      = false;
    AppWindow*  owner_        = nullptr;
    std::vector<AppWindow*> owned_;
    DialogResult result_ = DialogResult::None;   // sätts av closeWithResult

    // STANDARD- OCH AVBRYTKNAPPEN — pekaren och knappens
    // livstecken, satta av `setAcceptButton`/`setCancelButton`. Pekaren läses
    // bara medan tecknet lever, och `~AppWindow` rör den aldrig: knapparna är
    // värdemedlemmar som kan dö före fönstrets bas. (`…Btn_` och inte
    // `accept_`/`cancel_`: dialoger som ärver fönstret bär ofta en egen
    // `cancel_`, och två medlemmar med samma namn i kedjan läser fel.)
    Button*             acceptBtn_ = nullptr;
    std::weak_ptr<char> acceptLife_;
    Button*             cancelBtn_ = nullptr;
    std::weak_ptr<char> cancelLife_;
    // Knappen om den lever OCH sitter i det här fönstret, annars nullptr.
    Button* liveAccept() const;
    Button* liveCancel() const;
    // Tar knappen en tangent här: den sitter i det här fönstret, syns i
    // trädet (`shownInTree`) och är aktiverad. Livet prövar anroparen.
    bool    usable(const Button& b) const;
    // Standardknappen — se `isDefaultButton`. nullptr = ingen.
    Button* defaultButton() const;
    // Standardknappen KAN ha bytts: rita om den förra (`förut`) och den nya
    // när de skiljer sig. Sättarna och `setFocus` går hit.
    void    defaultButtonChanged(Button* förut);
    // `ändrad` bytte synlighet, aktivering eller anknytning — är den (förfader
    // till) acceptknappen eller den fokuserade widgeten ritas acceptknappen
    // och en fokuserad knapp om (`Widget::refreshDefaultButton`, `adopt`,
    // `detachTree`). Widget är friend.
    void    defaultButtonMayChange(const Widget& ändrad);
    // EN ÖPPEN POPUP: den första kontrollen i
    // trädet vars popup är öppen (`Widget::hasOpenPopup`), följd till sitt
    // fokusombud — den som hade tagit tangenten om kontrollen haft fokus.
    // nullptr när ingen popup är öppen. Return och Escape som ingen tog
    // erbjuds den FÖRE standard- och avbrytknappen.
    Widget* openPopupKeyTarget();

    bool        needsRedraw_ = true;
    bool        animating_   = false;
    bool        fullDamage_  = true;      // invalidate() = rita om allt
    bool        caretPhase_  = false;     // blinkfasen (speglas i widgeten)

    // FULLA invalideringar sedan fönstret skapades — `invalidate()` stegar
    // och ingenting nollar. Halva `Widget::damageStamp()`:
    // `fullDamage_` ovan är förbrukningsflaggan, det här är ett räkneverk.
    std::uint64_t fullInvalidations_ = 0;

    // Finkornig damage utöver per-widget-dirty (Widget::invalidateRect) —
    // fysiska px, konsumeras och töms av renderContent varje pass.
    std::vector<gfx::Rectf> extraDamage_;

    // Damage-debug (setDebugDamage): markörer som tonar ut i overlayen.
    // overlayAnimating_ driver presents medan markörer lever — SKILT från
    // animating_ (som betyder att INNEHÅLLET ändras per frame = full
    // damage); uttoningen rör aldrig retained-innehållet, annars skulle
    // den mata sig själv med ny damage och aldrig konvergera.
    struct DamageMark {
        gfx::Rectf    rect;
        std::uint8_t  color;    // index i färgcykeln
        std::uint64_t bornMs;   // gfx::ticksMs() vid omritningen
    };
    bool                    debugDamage_      = false;
    bool                    overlayAnimating_ = false;
    std::uint32_t           damageGen_        = 0;   // färgcykling/omritning
    std::vector<DamageMark> damageMarks_;

    friend class MenuPresentation;   // setMenuKeyHook (öppen menys tangenter)
    friend class AppIcon;   // platsen läser topBar_, settrarna applyWindowIcon
    friend class ToolTipWindow;      // toolTipClosed (tooltipens popup)
    // Systemfältsmenyns värd: `toolWindow_`, och
    // `initX_`/`initY_` så att den skapas direkt vid ikonen — på rätt skärm
    // och i rätt DPI från början.
    friend class TrayHostWindow;
    // Det flytande dockningsfönstret (en panels eller ett verktygsfälts) läser
    // ÄGARKEDJANS `pendingClose_` (ett stängningsveto får inte stoppa en
    // kaskad) — och sätter sina egna
    // attribut före `show()` (ägarens genvägar är publika, `tryShortcut`):
    // `toolWindow_`, `noActivate_`, `showInactive_` och, för ett BURET
    // objekt, `initX_`/`initY_` med `StartPosition::Default`.
    friend class DockFloatWindow;
    friend class Widget;   // Widget::invalidate väcker fönstret (needsRedraw_)
    friend class App;      // pumpen: handleEvent/wantsFrame/executeClose
};

} // namespace glaze
