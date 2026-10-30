#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gfx/events.h>

#include "glaze/input.h"
#include "glaze/style.h"
#include "glaze/theme.h"

// glaze::App — loop-ÄGAREN: äger plattformens init/shutdown, fönsterregistret
// och den händelsestyrda event-pumpen över ALLA fönster. EN per process, som
// NÅBAR Meyers-singleton (glaze::app()) — skapas lat vid första fönstret, så
// main behöver aldrig deklarera den (WinForms-känslan):
//
//     int main() {
//         StartWindow win;
//         return win.run();   // visar + pumpar tills win stängts
//     }
//
// main driver sekvensen: run() återvänder när fönstret stängts, så splash →
// main är två run-anrop i rad. Fönstren ägs av ANVÄNDAREN (stack/medlemmar) —
// App registrerar/avregistrerar och river OS-fönstret uppskjutet, men
// raderar ALDRIG C++-objektet. gfx::shutdown körs i App-singletonens dtor
// vid processlut — i DELAT läge av programfilens ankare i stället
// (detail::shutdownApp nedan).

namespace glaze {

class App;
class AppWindow;
// Stängningens skäl — definierad i widget.h bredvid `CloseArgs`;
// här bara den OPAKA deklarationen, för app.h inkluderar inte widget.h.
enum class CloseReason : std::uint8_t;

// Den enda App-instansen — skapas vid första anropet (normalt första
// show()/run()). Aktivitetshanterar-ytan (windows/count/find) bor på den.
App& app();

namespace detail {

// PROGRAMFILENS NEDSTÄNGNING i DELAT läge — anropas av
// ankaret src/app_anchor.cpp, som kompileras in i varje mål som länkar
// `glaze`; aldrig av en app. Gör det App:s destruktor gör (stänger fönstren,
// kör gfx::shutdown) om `anchor` ligger i PROGRAMFILEN, annars ingenting:
// en plugin-DLL:s ankare rivs i dess DLL_PROCESS_DETACH, där ingenting får
// rivas. Idempotent — App:s destruktor blir sedan en no-op.
void shutdownApp(const void* anchor);

// STANDARDIKONENS FÄRGER SOM OS-IKON (window_icon.cpp): appens tema
// i OS:ETS läge — skalets namnlist och systemfält följer OS:et, inte appens
// `setThemeMode` — med konturen `text` och accenterna `stock*`. Aldrig en
// apps fråga.
const Theme::Colors& osIconColors();

// RAMVERKETS EGNA POSTER — det som hör till
// NÄSTA bild: ritytans tick och kamerans händelse ur ritningen, och fönstrets
// `onResize`-leverans. De går i en EGEN kö som pump-svansen kör FÖRST, hel
// och utan `App::setPostBudget`s budget — en tick som väntat bakom en skur
// av trådarnas svar hade gett en bild utan rörelse. Samma lås, samma
// väckning och samma stängda kö efter nedstängningen som `App::post`, men
// bara från UI-tråden och aldrig en apps väg: appen postar med `app().post`.
void postFrameWork(std::function<void()> fn);

// EN EXTERN BILDKÄLLA — en annan klocka än skärmens som
// driver bildrutor i UI-tråden (ett VR-headsets runtime). Medan en källa är
// registrerad presenterar alla fönster osynkat (`Renderer::
// setExternalFramePacing`) och ritas högst `desktopHz` gånger per sekund;
// en ändring som väntar ritas vid nästa tillåtna tillfälle. Med flera källor
// gäller den lägsta takten; 0 räknas som 1. Källans jobb
// körs i pump-svansen FÖRE ramverkets och appens poster, hela och utan
// budget. Ingenting väcks när ingen källa finns: källan driver bildrutorna.
//
// `register…` och `unregister…` i UI-tråden; id 0 är aldrig giltigt.
// `postExternalFrame` från VILKEN tråd som helst: false om källan inte finns
// (avregistrerad, appen nedstängd) eller redan har ett jobb som väntar — högst
// ett åt gången. Ett väntande jobb körs aldrig efter `unregister…`.
using ExternalFrameSourceId = std::uint64_t;
ExternalFrameSourceId registerExternalFrameSource(unsigned desktopHz);
bool postExternalFrame(ExternalFrameSourceId source, std::function<void()> job);
void unregisterExternalFrameSource(ExternalFrameSourceId source);

// APPENS BINDNINGSTABELL FÖR RITYTORNA — det ritytan
// ärver det den inte satt själv från (src/canvas/input_map.h). En LÄSFRÅGA:
// den skapar ALDRIG App (och kör alltså aldrig `gfx::init`) — finns ingen App
// än svarar den med ramverkets förval. Aldrig en apps väg: appen ändrar
// tabellen med `App::setCanvasBinding` m.fl.
class InputMap;
const InputMap& canvasInput();
// Appens rullningsriktning (`App::wheelInverted`) som läsfråga på samma sätt —
// false utan App. `Widget::isWheelInverted` frågar hit.
bool appWheelInverted(InputDevice d, bool horizontal);

} // namespace detail

// APPENS ID — namnet skrivbordet känner igen appen
// på och hittar dess `.desktop`-fil (och därmed ikonen) genom: Wayland
// `xdg_toplevel_set_app_id` och X11 `WM_CLASS`. Bara Linux frågar; Windows
// och macOS identifierar appen genom programfilen. Anropas i main FÖRE det
// första fönstret. TOM sträng = förvalet, PROGRAMFILENS NAMN (`demo`).
// Tillåtet är `[A-Za-z0-9._-]`, högst 255 tecken — ett ogiltigt id loggas och
// ignoreras. Samma sak som `gfx::setAppId`, nåbar utan gfx-headern.
void setAppId(const std::string& id);

// DOCKAN — om APPEN har en ikon i macOS-dockan.
// false = ingen (`NSApplicationActivationPolicyAccessory`): en app som bor i
// menyradens systemfält (`glaze::TrayIcon`). App-global som `setAppId` och
// anropas likadant i main FÖRE det första fönstret, så att dockikonen aldrig
// blinkar till; ett senare byte gäller direkt men kan lägga fönstren bakåt.
// Windows och Linux har ingen docka — begäran lagras och false loggas en
// gång. På Windows är det fönstrets knapp i aktivitetsfältet som styrs, och
// den styrs PER FÖNSTER (`AppWindow::setShowInTaskbar`) — två begrepp, två
// funktioner. `showInDock()` svarar med begäran, förval true. Samma sak som
// `gfx::setShowInDock`, nåbar utan gfx-headern.
void setShowInDock(bool show);
bool showInDock();

class App {
public:
    ~App();
    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Fönsterregistret utåt (aktivitetshanteraren): alla ÖPPNA fönster i
    // visningsordning. Icke-ägande pekare — objekten är användarens.
    // Per-fönster CPU/GPU-mätning är profilerarens (glaze/profiler.h).
    const std::vector<AppWindow*>& windows() const { return windows_; }
    std::size_t windowCount() const { return windows_.size(); }

    // Första öppna fönstret med den titeln (nullptr om inget).
    AppWindow* find(const std::string& title) const;

    // APPENS AKTIVA FÖNSTER (WinForms
    // `Form.ActiveForm`) — det första öppna, inte stängningsväntande fönstret
    // med `isActive()`; nullptr när appen inte har tangentbordsfokus. Svarar
    // på "vilket fönster kom tangenten från": en popup blir aldrig aktiv, så
    // det är ägaren vars genvägar gäller.
    AppWindow* activeWindow() const;

    // Användarskalan — appens multiplikator OVANPÅ OS-skalan:
    // uiScale() = osScale × userScale, default 1.0, APP-GLOBAL som temat.
    // ALLT växer — den logiska storleken är invarianten. setUserScale
    // klampar (0.5–2.0, via gfx), stänger öppna popups (en öppen dropdown
    // har fel ankare efter ett skalbyte) och låter varje öppet fönster
    // räkna om sin fysiska storlek (Window::userScaleChanged). Fonten
    // laddas om av EVENT-vägen (WindowDisplayScaleChanged → reloadFont),
    // inte av sättaren — ordningen ägs av backenden. Ramverket exponerar
    // API:t; APPEN äger lagringen (ingen inställningsfil här).
    float userScale() const;
    void  setUserScale(float s);

    // SKÄRMKALIBRERINGEN — appens egen mätning av en
    // skärm, i FYSISKA px per mm, för skärmens id (`AppWindow::display().id`):
    // demons linjalprov drar en linje tills den mäter 100 mm och sätter
    // talet här. App-global som användarskalan, och APPEN äger lagringen —
    // den sätter sitt sparade värde vid start. 0 tar bort kalibreringen.
    // Varje öppet fönster mäts om och ritas om, så det som ritas i mm byter
    // storlek direkt; ett id eller värde gfx avvisar (tomt, utanför 50–800
    // dpi) loggas och ändrar ingenting.
    void setDisplayCalibration(const std::string& id, float pxPerMm);

    // RITYTANS MINNESBUDGET — taket i BYTE för de bilder
    // ur pixlar (`glaze::Bitmap`) ett fönster håller som texturer; förval
    // 256 MB. Talet är FÖRVALET PER RITYTA (en rityta med
    // `Canvas::setMemoryBudget` har sitt eget) och taket för fönstrets bilder
    // UTANFÖR ritytorna (widgetarna); varje fönster har sina egna texturer,
    // och flera ritytor med förval kan tillsammans överstiga talet. Över
    // taket släpps de bilder som legat oritade längst (LRU) i början av
    // fönstrets nästa frame — aldrig mitt i en — och en släppt bild laddas
    // upp igen när den ritas nästa gång. Det som ritades i förra framen
    // släpps aldrig, så ett tak som det SYNLIGA överskrider hålls inte
    // (loggas en gång). App-global som användarskalan; sättaren lagrar.
    void        setCanvasMemoryBudget(std::size_t bytes);
    std::size_t canvasMemoryBudget() const { return canvasBudget_; }

    // RITYTANS UPPLADDNINGSBUDGET — hur många BYTE ett
    // fönster högst laddar upp till GPU:n per frame åt ritytan; förval
    // 16 MiB. Det som inte ryms väntar på NÄSTA frame, som fönstret begär
    // själv, så en vy full av nya plattor fylls på över några frames i
    // stället för att frysa en. Framens FÖRSTA uppladdning går alltid
    // igenom, så det som är större än budgeten också kommer upp. Bilderna ur
    // pixlar (`glaze::Bitmap`) räknas mot den, och ritytans meshar,
    // instanser och punkter strömmar i bitar inom
    // samma räknare. PER FÖNSTER och frame, app-global som minnesbudgeten, och
    // läst i början av varje fönsters nästa frame. 0 och ett negativt tal
    // (som i `std::size_t` blir större än `PTRDIFF_MAX`) loggas och ändrar
    // ingenting.
    void        setCanvasUploadBudget(std::size_t bytes);
    std::size_t canvasUploadBudget() const { return uploadBudget_; }

    // RULLNINGENS RIKTNING I HELA APPEN — förvalet
    // varje widget ärver när varken den eller en förfader satt något
    // (`Widget::setWheelInvert`): lodrätt och i sidled, för musens hjul och
    // för styrplattan var för sig. RELATIV MOT OS:ET — det OS:et redan vänt
    // (naturlig rullning) vänds härifrån. Ritytan följer sina bindningar i
    // stället (nedan). `Keyboard` har inget hjul — loggas och ingenting
    // ändras. App-global DRIFT och inget fönsters tillstånd, så den har
    // INGEN `.glazeui`-form (`setUserScale`s prejudikat) — appen sätter sitt
    // sparade val i `main` eller `onLoad`. Sättaren lagrar; nästa hjulsteg
    // läser den.
    void setWheelInvert(InputDevice d, bool vertical, bool horizontal);
    bool wheelInverted(InputDevice d, bool horizontal) const;

    // RITYTANS BINDNINGAR FÖR HELA APPEN — tabellen
    // varje rityta ärver från, rad för rad: en rityta som satt en rad med
    // samma nyckel (läge, källa, modifierare) har sin egen
    // (`Canvas::setBinding`), och vändningarna likaså. Formen och reglerna
    // står vid `Canvas::setBinding` och i glaze/input.h. Förvalet är
    // ramverkets gester (MANUAL `input`), och `resetCanvasBindings()` tar
    // tillbaka det — rader OCH vändningar. En ogiltig rad (tomt läge, fart
    // ≤ 0 eller inte ändlig) loggas och ändrar ingenting. Ingen `.glazeui`-form,
    // av rullningens skäl ovan. Sättarna lagrar; nästa händelse läser dem.
    void setCanvasBinding(const std::string& mode, InputSource s,
                          std::uint32_t mods, InputAction a,
                          InputDirection dir = InputDirection::Normal,
                          float speed = 1.0f);
    void removeCanvasBinding(const std::string& mode, InputSource s,
                             std::uint32_t mods);
    void resetCanvasBindings();
    std::vector<InputBinding> canvasBindings() const;
    void setCanvasSourceInvert(InputSource s, bool on);
    void setCanvasActionInvert(InputDevice d, InputAction a, bool on);

    // TRÅDSÄKER POST TILL UI-TRÅDEN. Den ENDA
    // glaze-funktion som får kallas från en annan tråd:
    //
    //     std::thread([this] {
    //         auto rapport = tungBeräkning();
    //         glaze::app().post([this, rapport] { status.setText(rapport); });
    //     }).detach();
    //
    // Funktionen körs i UI-TRÅDEN, i pump-SVANSEN — samma plats som
    // uppskjutna stängningar, aldrig mitt i dispatch. Därför är det säkert
    // att röra widgets, stänga fönster och öppna nya i den; en postad
    // funktion som HANN köras i varvet stänger i samma varv, eftersom kön
    // dräneras FÖRE `flushPendingCloses()`.
    //
    // KÖN ÄR FIFO MED EN TIDSBUDGET (`setPostBudget` nedan):
    // svansen kör funktionerna i postordning tills budgeten är slut, och det
    // som inte hann körs i nästa varv — efter en ritning och före allt som
    // postats sedan. En funktion som postas inifrån en postad körs tidigast
    // i nästa varv, efter det som redan låg i kön, så en funktion som postar
    // sig själv kan aldrig svälta ritningen.
    //
    // Väckningen går via `gfx::postWakeup()`: pumpen sover i `waitEvent`
    // (hård regel 1) och måste få returnera för att nå svansen. Posten
    // INVALIDERAR ingenting själv — det gör settrarna som lambdan
    // anropar.
    //
    // Körs kön UTAN pump (ingen `run()` igång) ligger funktionerna kvar
    // tills någon pumpar. Det är avsiktligt: en kö som körs av sig själv
    // hade behövt en egen loop, och det finns ingen sådan (regel 1).
    // Pumpen kollar kön FÖRE den blockerande väntan (`hasPosted()`), så en
    // post som kommer innan pumpen började — eller vars väckning går
    // förlorad — leder aldrig till att appen somnar med arbete kvar.
    //
    // **GRÄNSEN, och den är skriven för att den inte hålls av koden**:
    // trådsäkerheten gäller en LEVANDE app. Alla
    // postande trådar måste vara stoppade eller joinade INNAN appen rivs.
    // Postar en tråd samtidigt som `gfx::shutdown()` kör är utfallet
    // odefinierat — backendernas väckningstillstånd är atomiskt, men en fd
    // som stängs mellan läsningen och skrivningen kan hinna återanvändas av
    // OS:et, och ett redan köat macOS-block kan köras efter rivningen. Att
    // täcka det hade krävt generationsräknare och en shutdown-barriär i tre
    // backends; priset är inte värt det för ett fall appen själv äger.
    //
    // **EFTER NEDSTÄNGNINGEN SLÄPPS EN POST TYST**:
    // App stänger kön innan `gfx::shutdown()` körs — vid processlut, i
    // delat läge från programfilens ankare — och förstör det som låg kvar
    // där, utanför låset och medan processens trådar lever. En post efter
    // det köas inte och väcker ingenting; funktionen förstörs direkt, och
    // körs aldrig. Det är ingen ursäkt för att posta sent: gränsen ovan
    // gäller fortfarande.
    void post(std::function<void()> fn);

    // POSTKÖNS BUDGET — hur länge
    // pump-svansen högst kör APPENS postade funktioner per varv, i ms; förval
    // 4. En skur av trådarnas svar (plattor, moln och träd i hundratal)
    // fördelas då över bildrutorna i stället för att frysa en. Varvets FÖRSTA
    // funktion körs alltid, så kön går framåt också när en enda funktion är
    // längre än budgeten — den kan inte delas, och syns som profilerarens
    // `postedMax`. Klockan räknar väggtid från köns början, också en nästlad
    // pumps (en modal dialog i en postad funktion). Ramverkets egna poster
    // (ritytans tick, kamerans händelse ur ritningen, `onResize`) körs FÖRE
    // appens och räknas inte (`detail::postFrameWork`). 0 = obegränsat: allt
    // som låg i kön när varvet började körs i det. Ett
    // negativt tal loggas och ändrar ingenting. App-global; från UI-tråden
    // som allt utom `post`. Sättaren lagrar, och nästa dränering läser den.
    void setPostBudget(int ms);
    int  postBudget() const { return postBudgetMs_; }

    // SPRÅKBYTET — hela invalideringsrundan efter att
    // `glaze::setLanguage`/`setFallbackLanguage`/`loadLanguage` har
    // stegat språkgenerationen. PUBLIK av en enda anledning: språkbutiken
    // är en modul av FRIA FUNKTIONER (`language.cpp`) och inte en
    // `AppWindow`, så den kan inte nå de privata statiska rundorna via
    // `friend class AppWindow` som utseende-axlarna gör. Appar har inget
    // ärende hit — anropa `glaze::setLanguage`, som driver den själv.
    //
    // ORDNINGEN ÄR BÄRANDE och därför skriven:
    //
    //   1. `closePopupsNow(nullptr)` — den SYNKRONA vägen. En KÖAD
    //      stängning verkställs i pump-svansen, alltså EFTER kroken och
    //      omritningen nedan, och en öppen meny bär en SNAPSHOT
    //      (`SnapRow`) med den GAMLA texten. Korrektheten får inte vila
    //      på implicit pumpordning.
    //   2. En SNAPSHOT av `open_ && !pendingClose_`-fönstren tas — samma
    //      filter som övriga globala loopar.
    //   3. Titeln trycks om av App SEPARAT, före den virtuella kroken, så
    //      en override som glömmer basanropet ändå inte tappar titeln.
    //   4. `AppWindow::onLanguageChanged()` över snapshoten, med
    //      OMPRÖVAT `open_ && !pendingClose_` före VARJE anrop: kroken är
    //      en generell virtuell yta och en override får i princip öppna
    //      eller stänga fönster, alltså mutera `windows_` mitt i loopen.
    //   5. `invalidateAll()`. INTE `reloadFontsAll` (fonten är
    //      oförändrad) och INTE `applyContentSizeAll` (chromets höjd är
    //      stilmått, inte text).
    static void languageChangedAll();

private:
    App();               // bara via glaze::app() — EN per process
    friend App& app();
    friend class AppWindow;
    friend void detail::shutdownApp(const void* anchor);
    friend const Theme::Colors& detail::osIconColors();
    friend void detail::postFrameWork(std::function<void()> fn);
    friend const detail::InputMap& detail::canvasInput();
    friend bool detail::appWheelInverted(InputDevice d, bool horizontal);

    // Fönstren och plattformen — destruktorns arbete, och i DELAT läge
    // programfilens ankares (`detail::shutdownApp`). Idempotent: andra
    // gången finns inga fönster och ingen plattform kvar att riva.
    void shutDown();

    // Pumpen — DELAS av alla run()-anrop (en pump, nästlade ingångar:
    // modala dialoger pumpar samma implementation). Hård regel 1 bor här:
    // sover i gfx::waitEvent när INGET fönster vill ha frame (animating/
    // overlayAnimating/needsRedraw), dispatchar på Event::windowId, ritar
    // exakt de frame-hungriga fönstren, och verkställer uppskjutna
    // stängningar i LOOP-SVANSEN — aldrig mitt i dispatch. Nästlade pumpar
    // delar stäng-listan.
    void pumpUntilClosed(AppWindow& w);

    // Utseendet (ritning + tema + mått + läge) är APP-GLOBALT: ETT state som ALLA
    // fönster ritar ur, så ett byte (temaBtn, setThemeMode, OS-byte i
    // System-läget) slår igenom i varje öppet fönster — precis som main.
    // Statiskt (inte per App-instans) så fönsterobjekt kan konstrueras och
    // sättas upp innan App finns; AppWindows utseende-API läser/skriver
    // hit och invalidaterar alla öppna fönster via invalidateAll().
    // TRE OBEROENDE AXLAR + läget. Att välja `ui` sätter de två andra till
    // familjens defaults (AppWindow::setUI); inom familjen kan man sedan
    // byta fritt — en UI kan ha flera teman och flera storlekar.
    //
    // **Theme och Style HÖR TILL sitt UI**. Det uttrycks
    // i UPPSLAGET, inte i en vakt: `themeAt(ui, i)` / `styleAt(ui, i)` är
    // enda vägen till dem, och det finns ingen platt global lista där
    // "Regular" kan betyda vad som helst. Att korsa familjer kräver alltså
    // att man uttryckligen hämtar ur en annan — och gör man det har man
    // gjort det med avsikt, precis som en raderad handler ger ett
    // kompileringsfel i stället för runtime-magi (rot-CLAUDE.md). Ingen
    // idiotsäkring: måtten och paletterna är designade för sin form (Glazes
    // Regular är höjd 27/font 13 för en laminerad platta, Minimalistics
    // höjd 18/font 10 för en outline-kant), och kontrastgrinden prövar bara
    // par inom en familj — men den som blandar har själv valt det.
    //
    // `style` lagras som VÄRDE, inte som pekare. En pekare hade sparat en
    // kopia av en POD på ~280 byte, men till priset av att
    // `setStyle(Style{…})` med en temporär — eller en lokal som går ur
    // scope — lämnat state:t dinglande. Det är normal felanvändning av ett
    // API som tar `const Style&`, och en kommentar om "statisk livstid"
    // hindrar den inte. Samma resonemang som `theme`, som är ett
    // värde. Då finns ingen nullptr-gren, och state:t är komplett per
    // konstruktion i stället för per konvention.
    struct ThemeState {
        UI        ui    = UI::Glaze;
        Theme     theme;                     // default = Theme::glazeDefault()
        Style     style = defaultStyle(UI::Glaze);
        ThemeMode mode  = ThemeMode::System; // System följer OS:et i drift
    };
    static ThemeState& themeState();
    static void        invalidateAll();

    // Måtten (Style) äger UI-fontens storlek, så ett byte av UI/Style måste
    // ladda om fonten i VARJE öppet fönster — annars ritas de nya måtten med
    // gamla fontens radhöjd och alla autostorlekar blir fel. No-op per
    // fönster när storleken är oförändrad (AppWindow::reloadFont jämför), så
    // ett rent färgbyte kostar ingenting.
    static void        reloadFontsAll();

    // …och samma byte kan ändra CHROMETS höjd (`barHeight`/`statusBarH` är
    // stilmått), så varje fönster som bett om en INNEHÅLLSSTORLEK
    // (`AppWindow::setContentSize`) måste räknas om. Utan det
    // hade löftet "appen slipper känna ramverkets tal" bara gällt fram till
    // första stilbytet: talen hade frusits vid `show()`. No-op per fönster
    // utan begärd innehållsstorlek.
    static void        applyContentSizeAll();

    void dispatch(const gfx::Event& e);

    // Popup-regeln: tryck i ett annat fönster eller fokusförlust stänger
    // öppna popup-fönster (klick-utanför för menyer-som-fönster).
    // target och fönster som target ägs av (transitivt) lämnas ifred.
    void closePopups(AppWindow* target);

    // Samma urval som `closePopups`, men stängningen VERKSTÄLLS direkt i
    // stället för i pump-svansen — och bara för popups. Anropas av
    // `AppWindow::showOwned` innan ett popup-fönster skapas: xdg-shell
    // tillåter bara att den ÖVERSTA popupen förstörs, så en ny grabbande
    // popup får inte existera samtidigt med den den ersätter. Övriga
    // uppskjutna stängningar rörs inte — de hör hemma i svansen.
    // **Popup-stängningen är INTE vetobar** (skälen i app.cpp) — ett veto
    // hade lämnat den gamla popupen levande medan den nya ändå skapades.
    void closePopupsNow(AppWindow* target);

    // onClose i hela kedjan utan veto — bara för closePopupsNow ovan. `w`
    // får `skäl` (stängningens), dess ägda `OwnerClosing`.
    void firePopupClose(AppWindow* w, CloseReason skäl);

    void queueClose(AppWindow* w);     // AppWindow::close — uppskjuten
    void flushPendingCloses();         // loop-svansen
    // Loop-svansen, FÖRE stängningarna: ramkön hel, sedan appens kö inom
    // budgeten.
    void drainPosted();
    // Loop-svansen, efter de postade och FÖRE stängningarna: varje
    // fönsters commit-kö töms framifrån — en modal dialog i en insamlad
    // handler levererar så resten i ordning (kontraktet i app_window.h).
    void drainCommitQueues();

    // Finns något postat just nu, i NÅGON av köerna? Läses av pumpen FÖRE
    // den blockerande väntan: en post som kom innan pumpen började (eller
    // vars väckning gick förlorad) får aldrig leda till att vi somnar med
    // arbete i kön. Det gör klassen "tappad väckning" OFARLIG i stället för
    // dödlig — väckningen blir en optimering, inte en förutsättning. Det
    // budgeten lämnade kvar ligger i appens kö och håller pumpen vaken på
    // samma sätt, utan någon väckning.
    bool hasPosted() const;

    // onClose-vetot: frågar w:s ägda barn DJUPAST FÖRST, sedan
    // w. Första cancel avbryter HELA kaskaden — ingen i kedjan stängs. `w`
    // frågas med `skäl` (stängningens, `AppWindow::closeReason_`), barnen
    // med `OwnerClosing` — de stängs för att ägaren gör det.
    bool confirmClose(AppWindow* w, CloseReason skäl);

    void executeClose(AppWindow* w);   // river OS-fönstret; barnen FÖRST
    bool anyWantsFrame() const;
    AppWindow* findWindow(gfx::WindowId id) const;

    std::vector<AppWindow*> windows_;      // ICKE-ägande, i visningsordning
    std::vector<AppWindow*> pendingClose_; // verkställs i loop-svansen

    // Postkön — TVÅ köer under ETT lås och EN mekanism:
    // `enqueue` lägger i och väcker, `runQueue` kör. `posted_` är APPENS
    // (`post`, inom budgeten), `frame_` RAMVERKETS (`detail::postFrameWork`,
    // först och hel). Låset skyddar BARA listorna — funktionerna körs
    // UTANFÖR det, annars hade en postad lambda som själv postar låst sig
    // (och en handler som öppnar en modal dialog hållit låset genom en
    // hel nästlad pump). Dräneringen tar en SNAPSHOT via swap, så en
    // lambda som postar en ny lambda inte kan svälta varvet; det budgeten
    // inte hann läggs tillbaka FÖRST i kön, under låset — före det som
    // postats under tiden, också från andra trådar.
    // `postClosed_` sätts av `shutDown()` under samma lås: därefter släpper
    // `enqueue` funktionen i stället för att köa den (kontraktet vid `post`).
    using PostQueue = std::vector<std::function<void()>>;
    void enqueue(PostQueue& kö, std::function<void()> fn);
    // Kör snapshoten av `kö`; `budgetMs` > 0 slutar efter den funktion som
    // tog budgeten, 0 kör allt.
    void runQueue(PostQueue& kö, int budgetMs);

    mutable std::mutex postMutex_;   // mutable: hasPosted() är const
    PostQueue          posted_;
    PostQueue          frame_;
    bool               postClosed_   = false;
    int                postBudgetMs_ = 4;   // setPostBudget — 0 = obegränsat

    // Modalstacken (run() på ett isModal-fönster pushar, executeClose
    // plockar bort): filtret i dispatch släpper input BARA till toppen —
    // nästlade modaler = den senast öppnade vinner. Fönsterhändelser/
    // timers filtreras aldrig.
    std::vector<AppWindow*> modalStack_;

    int  pumpDepth_ = 0;   // nästlingsdjup — kvarlever-varningen bara ytterst
    bool ok_        = false;   // gfx::init() lyckades

    // INMATNINGENS RÄKNEVERK — stegas för varje
    // RIKTIG händelse (`noteEvent`: musen, tangenterna, texten, hjulet,
    // gesten och en storleksändring av fönstret) överst i
    // `AppWindow::handleEvent`, så också grindarnas syntetiska når det.
    // `onResize`s kedjespärr läser det: en Wakeup — leveransens egen
    // väckning — stegar det inte.
    std::uint64_t eventSeq_ = 0;
    void          noteEvent(gfx::EventType t);

    std::size_t canvasBudget_ = std::size_t{256} << 20;   // per fönster
    std::size_t uploadBudget_ = std::size_t{16} << 20;    // per fönster och frame

    // RULLNINGENS RIKTNING [mus/styrplatta][lodrätt/sidled] och RITYTORNAS
    // BINDNINGSTABELL — den senare en intern typ bakom
    // en pekare, skapad med förvalen i konstruktorn (dll_gate: inget
    // tillstånd i en header).
    bool                              wheel_[2][2] = {};
    std::unique_ptr<detail::InputMap> canvasInput_;

    static App* instance_;   // = singletonen — AppWindow når den utan att skapa
};

} // namespace glaze
