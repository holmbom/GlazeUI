// glaze::App — loop-ägaren. Pumpen är EN implementation (pumpUntilClosed)
// med två ingångar: App::run(w) och AppWindow::showModal (nästlad pump) —
// det är skyddet mot reentrans-buggarna: uppskjutna stängningar delas och
// verkställs bara i loop-svansen, aldrig mitt i en dispatch.

#include "glaze/app.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <gfx/platform.h>
#include <gfx/renderer.h>
#include <gfx/timer.h>

#include "glaze/app_window.h"
#include "glaze/language.h"
#include "glaze/strings.h"
#include "canvas/image_data.h"  // releaseQueuedPixels — pumpens släpp
#include "canvas/input_map.h"   // ritytornas bindningstabell
#include "canvas/resources.h"   // komplett typ för canvas_.reset()
#include "external_frames.h"    // externa bildkällor — pumpens och fönstrets sida
#include "font_set.h"           // komplett typ för fontSet_.reset()
#include "image_cache.h"        // komplett typ för images_.reset()
#include "profiler_hooks.h"     // profileraren — pumpens och bildens krokar
#include "timer_registry.h"     // appens timer — anteckningen och svansen
#include "tray_registry.h"      // systemfältets händelser och språkbyte

namespace glaze {
namespace {

// Modal-filtrets omfång: ENDAST input filtreras —
// fönsterhändelser och timers släpps igenom, annars fryser omritningen i
// bakgrundsfönster. MouseLeave släpps också igenom: den SLÄCKER bara
// tillstånd (hover) och kan aldrig ge ett blockerat fönster ny input.
bool isInputEvent(gfx::EventType t)
{
    switch (t) {
    case gfx::EventType::MouseMotion:
    case gfx::EventType::MouseButtonDown:
    case gfx::EventType::MouseButtonUp:
    case gfx::EventType::MouseWheel:
    case gfx::EventType::Gesture:   // styrplattans nypning/vridning
    case gfx::EventType::KeyDown:
    case gfx::EventType::KeyUp:
    case gfx::EventType::TextInput:
        return true;
    default:
        return false;
    }
}

// macOS APPMENY: fasaden bär ingen egen text, så det översatta
// "Avsluta "-prefixet trycks ner härifrån — efter `gfx::init()` och vid
// varje språkbyte. På Windows och Linux är fasadanropet en no-op.
void pushQuitMenuPrefix()
{
    const TrText prefix = tr(str::QuitAppPrefix);
    gfx::setQuitMenuPrefix(prefix.str());
}

} // namespace

App* App::instance_ = nullptr;

// Den nåbara singletonen: skapas lat vid första anropet —
// normalt första show()/run() — och lever till processlut (gfx::shutdown
// i dtorn, efter main). main behöver aldrig deklarera någon App.
App& app()
{
    static App instance;
    return instance;
}

// Fasaden lagrar och validerar (gfx/platform.h) — fönstren läser den när de
// skapas.
void setAppId(const std::string& id)
{
    gfx::setAppId(id);
}

// Dockan likaså: fasaden lagrar, och macOS tillämpar (gfx/platform.h) — i
// `init()` när anropet kom före det första fönstret, annars direkt.
void setShowInDock(bool show)
{
    gfx::setShowInDock(show);
}

bool showInDock()
{
    return gfx::showInDock();
}

// Det app-globala utseendet — Meyers-singleton så default-paletten hämtas
// säkert oavsett statisk initieringsordning, och state:t finns även
// innan/efter App-instansens livstid.
App::ThemeState& App::themeState()
{
    static ThemeState s = [] {
        ThemeState t;
        t.theme = defaultTheme(t.ui);
        return t;
    }();
    return s;
}

// Utseendebyte = rita om ALLA öppna fönster (nästa repaint resolvar om —
// händelsestyrt, ingen polling).
void App::invalidateAll()
{
    if (!instance_)
        return;
    for (AppWindow* w : instance_->windows_)
        w->invalidate();
}

// Byte av UI/Style kan ändra UI-fontens storlek — då måste varje ÖPPET
// fönster rastrera om sin font innan nästa repaint, annars mäts de nya
// måtten mot gamla radhöjden. Ordningen är säker: font_ byts ut medan ingen
// frame pågår (settrarna körs ur en handler, ritningen sker efteråt) och
// atlas-texturen hör till samma renderer som den nya fonten.
void App::reloadFontsAll()
{
    if (!instance_)
        return;
    for (AppWindow* w : instance_->windows_)
        if (w->open_ && !w->pendingClose_)
            w->reloadFont();   // no-op när storleken är oförändrad
}

// SPRÅKBYTET. Kontraktet och skälen står vid
// deklarationen i app.h — ordningen här är den, steg för steg.
void App::languageChangedAll()
{
    if (!instance_)
        return;

    // 1. SYNKRON popup-stängning. En KÖAD stängning verkställs i
    //    pump-svansen, alltså EFTER kroken och omritningen nedan — och
    //    en öppen meny bär en SNAPSHOT (`SnapRow`) med den gamla texten.
    //    Korrektheten får inte vila på implicit pumpordning.
    instance_->closePopupsNow(nullptr);

    // 1b. macOS appmeny — ingen widget, inget fönster, så den har ingen
    //     egen krok att lyssna i.
    if (instance_->ok_)
        pushQuitMenuPrefix();

    // 1c. Systemfältets tooltips — av samma skäl: de är
    //     OS:ets egna och bärs av ingen widget, så varje synlig ikon trycker
    //     om sin text till backenden. Efter steg 1, så att en öppen
    //     systemfältsmeny redan är stängd.
    detail::trayLanguageChanged();

    // 2. SNAPSHOT. Kroken i steg 4 är en generell virtuell yta och får i
    //    princip öppna eller stänga fönster, alltså mutera `windows_`.
    std::vector<AppWindow*> snapshot;
    for (AppWindow* w : instance_->windows_)
        if (w->open_ && !w->pendingClose_)
            snapshot.push_back(w);

    // 3. Titeln trycks om av App SEPARAT — och FÖRE den virtuella kroken,
    //    så en override som glömmer basanropet ändå inte tappar titeln.
    for (AppWindow* w : snapshot)
        if (w->open_ && !w->pendingClose_)
            w->repushTitle();

    // 4. Krokarna, med OMPRÖVAT fönsterfilter före varje anrop: först
    //    WIDGETARNAS (`Widget::languageChanged` — det en widget härleder ur
    //    språket och lagrar, talfältets decimaltecken), sedan FÖNSTRETS, så
    //    att en omlayout i `onLanguageChanged` mäter de nya texterna.
    for (AppWindow* w : snapshot)
        if (w->open_ && !w->pendingClose_) {
            w->languageChangedTree();
            if (w->open_ && !w->pendingClose_)
                w->onLanguageChanged();
        }

    // 5. Omritning. INTE `reloadFontsAll` (fonten är oförändrad) och INTE
    //    `applyContentSizeAll` (chromets höjd är stilmått, inte text).
    invalidateAll();
}

// Chromets höjd är STILMÅTT (`barHeight`, `statusBarH`), så ett byte av
// UI/Style flyttar innehållsytan. Varje fönster som bett om en
// innehållsstorlek räknas om — annars gällde löftet bara fram till första
// stilbytet. Skild funktion från reloadFontsAll: fonten måste laddas om
// FÖRE omräkningen (måtten mäts mot radhöjden), och två steg i rätt
// ordning är tydligare än ett steg med en dold ordning i sig.
void App::applyContentSizeAll()
{
    if (!instance_)
        return;
    for (AppWindow* w : instance_->windows_)
        if (w->open_ && !w->pendingClose_)
            w->applyContentSize();   // no-op utan begärd innehållsstorlek
}

App::App()
    : canvasInput_(std::make_unique<detail::InputMap>(detail::InputMap::defaults()))
{
    if (instance_) {
        gfx::log("glaze::App: en instans finns redan — EN App per process");
        return;
    }
    instance_ = this;
    // GLAZE_PROFILE gäller också en app som aldrig frågar
    // `profiler()`.
    detail::profileEnvironment();
    ok_ = gfx::init();   // loggar själv vid fel
    if (ok_)
        pushQuitMenuPrefix();
}

App::~App()
{
    shutDown();
    if (instance_ == this)
        instance_ = nullptr;
}

void App::shutDown()
{
    // De externa bildkällorna först: ingen källa tas emot och inget väntande
    // jobb körs mer (jobben bär handtag som ska släppas medan trådarna lever).
    detail::externalClose();

    // Säkerhetsnät: fönster som fortfarande är öppna när App dör (appfel —
    // run() ska ha stängt dem) rivs så OS-fönster inte läcker. Objekten
    // ägs av användaren och raderas aldrig här.
    while (!windows_.empty())
        executeClose(windows_.back());
    pendingClose_.clear();

    // POSTKÖERNA stängs och töms FÖRE plattformen. Det
    // som ligger kvar är funktioner och deras fångster — en `std::jthread`
    // som joinar i sin destruktor, ett handtag som släpper en resurs. Låg de
    // kvar i `posted_` eller `frame_` hade de förstörts med App, och i delat
    // läge är det i libglaze:s DLL_PROCESS_DETACH, efter att processens
    // trådar dödats. Här lever trådarna. Flytten sker under låset och
    // förstörelsen utanför det; en destruktor som postar möter en STÄNGD kö
    // (`enqueue` släpper den), så ingenting nytt blir kvar till App:s
    // destruktor.
    PostQueue kvar, kvarRam;
    {
        std::lock_guard<std::mutex> lås(postMutex_);
        postClosed_ = true;
        kvar.swap(posted_);
        kvarRam.swap(frame_);
    }
    kvar.clear();
    kvarRam.clear();

    if (ok_) {
        gfx::shutdown();
        ok_ = false;
    }
}

namespace detail {

// DELAT LÄGE. App bor i libglaze, och
// Windows river en DLL:s statiska objekt i DLL_PROCESS_DETACH — efter att
// `ExitProcess` dödat processens övriga trådar, och under laddarlåset. Där
// får varken fönstren stängas eller `gfx::shutdown` köras (D3D11-enheterna,
// systemfältets `Shell_NotifyIcon`). Programfilens statiska objekt rivs i
// stället i `exit()`, med trådarna vid liv: ankaret i src/app_anchor.cpp
// anropar oss därifrån, och ett ankare i en plugin-DLL — som rivs i DLL:ens
// DETACH — får ingenting gjort. Läser `instance_` och aldrig `app()`: på
// Linux och macOS har App redan rivits här (en gemensam omvänd
// atexit-ordning, och App skapades efter ankaret), och då är `instance_`
// nullptr.
void shutdownApp(const void* anchor)
{
    if (App::instance_ && gfx::isInProgramFile(anchor))
        App::instance_->shutDown();
}

const Theme::Colors& osIconColors()
{
    return App::themeState().theme.resolve(ThemeMode::System).colors;
}

// LÄSFRÅGORNA läser `instance_` och aldrig `app()` (`shutdownApp`s mönster):
// en widget eller en rityta som frågar headless — en grind, previewn före
// första fönstret — ska inte skapa App och initiera plattformen.
const InputMap& canvasInput()
{
    if (App::instance_)
        return *App::instance_->canvasInput_;
    static const InputMap förval = InputMap::defaults();   // oföränderlig, i cpp:n
    return förval;
}

bool appWheelInverted(InputDevice d, bool horizontal)
{
    return App::instance_ && App::instance_->wheelInverted(d, horizontal);
}

} // namespace detail

AppWindow* App::find(const std::string& title) const
{
    for (AppWindow* w : windows_)
        if (w->title() == title)   // den UPPLÖSTA texten — tokenoperatorn
                                   // hålls utanför
            return w;
    return nullptr;
}

// AKTIVT FÖNSTER — `AppWindow::active_` följer
// OS:ets fokushändelser; ett stängningsväntande fönster räknas inte.
AppWindow* App::activeWindow() const
{
    for (AppWindow* w : windows_)
        if (w->open_ && !w->pendingClose_ && w->active_)
            return w;
    return nullptr;
}

// KEDJESPÄRRENS RÄKNEVERK: bara riktig inmatning och en
// storleksändring av fönstret — användarens drag på Linux kommer som
// `WindowResized` utan pekarhändelser och ska leverera per bildruta. En
// Wakeup, en timer och en fokusändring stegar det inte.
void App::noteEvent(gfx::EventType t)
{
    if (isInputEvent(t) || t == gfx::EventType::WindowResized)
        ++eventSeq_;
}

// Användarskalan — themeState-mönstret fast värdet bor i gfx
// (backendernas uiScale() LÄSER det; widgets → gfx är den tillåtna
// riktningen). Kedjan är händelsestyrd: sättare → resize per fönster →
// WindowDisplayScaleChanged → reloadFont + invalidate (hård regel 1).

float App::userScale() const
{
    return gfx::userScale();
}

void App::setUserScale(float s)
{
    const float old = gfx::userScale();
    gfx::setUserScale(s);   // klampar 0.5–2.0 + loggar; ogiltigt ignoreras
    if (gfx::userScale() == old)
        return;   // oförändrat (klampat till samma/ogiltigt) — rör inget
    // Öppna popups har fel ankare efter ett skalbyte — stäng dem
    // (uppskjutet, verkställs i pump-svansen).
    closePopups(nullptr);
    // Varje öppet fönster räknar om fysisk storlek ur sin lagrade
    // logiska och pushar skal-eventet EFTER renderer-resizen (ordningen
    // ägs av backenden — fonten laddas om av event-vägen).
    for (AppWindow* w : windows_)
        if (w->open_ && !w->pendingClose_)
            w->window().userScaleChanged();
}

void App::setDisplayCalibration(const std::string& id, float pxPerMm)
{
    gfx::setDisplayCalibration(id, pxPerMm);   // validerar + loggar
    for (AppWindow* w : windows_)
        if (w->open_ && !w->pendingClose_)
            w->measureDisplay();
}

// Budgeten LAGRAS bara: varje fönster håller den i början av sin nästa frame
// (`ImageCache::trim` överst i renderContent). Ett fönster som står still
// behåller sina texturer tills det ritar nästa gång — en omritning bara för
// att släppa minne vore en frame utan något att visa (regel 1).
void App::setCanvasMemoryBudget(std::size_t bytes)
{
    canvasBudget_ = bytes;
}

// Uppladdningsbudgeten LAGRAS bara, av minnesbudgetens skäl: varje fönster
// läser den i början av sin nästa frame (`ImageCache::trim`). 0 byte är
// ingen budget (bara framens första uppladdning, som alltid går igenom,
// hade kommit upp), och ett negativt heltal blir efter omvandlingen till
// `std::size_t` större än det största objekt en process kan ha — båda är
// fel i anroparen.
void App::setCanvasUploadBudget(std::size_t bytes)
{
    constexpr auto kStörstaObjekt =
        static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (bytes == 0 || bytes > kStörstaObjekt) {
        gfx::log("App::setCanvasUploadBudget: %zu byte är ingen budget (0 eller "
                 "negativ) — %zu byte gäller fortfarande",
                 bytes, uploadBudget_);
        return;
    }
    uploadBudget_ = bytes;
}

// RULLNINGENS RIKTNING OCH RITYTORNAS BINDNINGAR — LAGRAS
// bara: fönstrets hjuldispatch och ritytorna läser dem vid nästa händelse,
// och ingenting ritas om av att en riktning ändras.
void App::setWheelInvert(InputDevice d, bool vertical, bool horizontal)
{
    if (d == InputDevice::Keyboard) {
        gfx::log("App::setWheelInvert: tangentbordet har inget hjul (ignoreras)");
        return;
    }
    const int i  = d == InputDevice::Touchpad ? 1 : 0;
    wheel_[i][0] = vertical;
    wheel_[i][1] = horizontal;
}

bool App::wheelInverted(InputDevice d, bool horizontal) const
{
    if (d == InputDevice::Keyboard)
        return false;
    return wheel_[d == InputDevice::Touchpad ? 1 : 0][horizontal ? 1 : 0];
}

void App::setCanvasBinding(const std::string& mode, InputSource s,
                           std::uint32_t mods, InputAction a, InputDirection dir,
                           float speed)
{
    const InputBinding b{mode, s, mods, a, dir, speed};
    const char*        varför = nullptr;
    if (!detail::validBinding(b, varför)) {
        gfx::log("App::setCanvasBinding(\"%s\"): %s (ignoreras)", mode.c_str(),
                 varför);
        return;
    }
    canvasInput_->set(b);
}

void App::removeCanvasBinding(const std::string& mode, InputSource s,
                              std::uint32_t mods)
{
    canvasInput_->remove(mode, s, mods);
}

void App::resetCanvasBindings()
{
    *canvasInput_ = detail::InputMap::defaults();
}

std::vector<InputBinding> App::canvasBindings() const
{
    return canvasInput_->rows();
}

void App::setCanvasSourceInvert(InputSource s, bool on)
{
    canvasInput_->setSourceInvert(s, on);
}

void App::setCanvasActionInvert(InputDevice d, InputAction a, bool on)
{
    canvasInput_->setActionInvert(d, a, on);
}

// Hjärtat — hård regel 1 över N fönster: waitEvent (blockerar, ~0 % CPU)
// BARA när inget fönster vill ha frame; annars poll-drän + renderFrame för
// exakt de fönster som vill. Stängningar verkställs i svansen. Anropas av
// AppWindow::run (även nästlat — modala dialoger pumpar samma väg).
// PROFILERAREN mäter varvets delar och varje bild med
// krokarna nedan — avstängd är varje krok ett anrop och en atomisk läsning.
void App::pumpUntilClosed(AppWindow& w)
{
    ++pumpDepth_;
    while (w.open_) {
        detail::profilePumpBegin(pumpDepth_);
        std::uint32_t händelser = 0;
        gfx::Event e;
        // Somna ALDRIG med postat arbete i kön. `postWakeup()` ska väcka
        // oss, men en väckning kan gå förlorad (Windows: PostThreadMessageW
        // misslyckas om måltråden ännu inte har en meddelandekö) och en post
        // kan dessutom ha kommit FÖRE pumpen ens började. Den här raden gör
        // väckningen till en OPTIMERING i stället för en förutsättning:
        // värsta fallet blir en sen körning, aldrig en evig blockering.
        // Det postkönens budget lämnade kvar håller pumpen
        // vaken samma väg — nästa varv tar nästa del, utan väckning.
        // Samma regel för en ANTECKNAD timertick: den
        // levereras i svansen och får aldrig bli liggande medan pumpen sover.
        // Och för en KÖAD STÄNGNING: `close()` i `onLoad` köar den
        // medan fönstret ännu är dolt, och ett dolt fönster ger på Linux inga
        // händelser alls — pumpen hade sovit för evigt med stängningen i kön.
        //
        // En extern bildkälla (app.h) stryper skrivbordsritningen: ett fönster
        // som vill ha en frame före sin period väntar, och pumpen sover till
        // periodens slut på appens timer — aldrig en snurrande loop. Ett
        // väntande källjobb håller pumpen vaken som en post.
        std::uint32_t väntanMs = 0;
        const bool    vill     = anyWantsFrame();
        const bool    stryps =
            vill && !detail::externalDesktopAllowed(detail::ExternalClock::now(),
                                                    väntanMs);
        bool släppFri = false;   // timern gick inte att starta: rita hellre än somna
        if ((!vill || stryps) && !hasPosted() && !detail::timerHasDue() &&
            pendingClose_.empty() && !detail::externalJobPending()) {
            const bool vaknaPåTid =
                stryps &&
                gfx::startAppTimer(detail::kExternalWakeTimer, väntanMs);
            släppFri = stryps && !vaknaPåTid;
            const bool fick =
                !släppFri && gfx::waitEvent(e);   // sover — ~0 % CPU i vila
            if (vaknaPåTid)
                gfx::stopAppTimer(detail::kExternalWakeTimer);
            detail::profilePumpLap(detail::PumpPart::Wait);
            if (fick) {
                dispatch(e);
                ++händelser;
            }
        }
        while (gfx::pollEvent(e)) {  // töm resten av kön
            dispatch(e);
            ++händelser;
        }
        detail::profilePumpLap(detail::PumpPart::Events, händelser);

        // Loop-svansen: kör postade funktioner FÖRST och riv OS-fönster
        // sedan — båda HÄR, aldrig mitt i dispatch. Ordningen är bärande:
        // en postad `close()` som hann köras i varvet ska tas i SAMMA varv
        // ramkön hel, sedan appens kö inom budgeten
        // (`drainPosted`),och det som inte hann tas nästa varv. APPENS TIMRAR
        // levereras efter de postade: dispatchen
        // antecknade dem bara, så en handler som tar längre tid än
        // intervallet aldrig svälter ritningen, och en postad `stop()` hinner
        // gälla samma varv.
        drainPosted();
        detail::profilePumpLap(detail::PumpPart::Posted);
        detail::timerDrain();
        detail::profilePumpLap(detail::PumpPart::Timers);
        drainCommitQueues();
        detail::profilePumpLap(detail::PumpPart::Commits);
        flushPendingCloses();
        detail::profilePumpLap(detail::PumpPart::Closes);
        if (!w.open_) {
            detail::profilePumpEnd(pumpDepth_);
            break;
        }

        // Skrivbordets period (en extern bildkälla): ett varv ritar alla
        // fönster som vill, eller inga.
        const detail::ExternalClock::time_point varv = detail::ExternalClock::now();
        std::uint32_t                           kvarMs = 0;
        const bool tillåten =
            släppFri || detail::externalDesktopAllowed(varv, kvarMs);
        bool ritade = false;
        for (AppWindow* win : windows_) {
            if (tillåten && win->wantsFrame()) {
                ritade            = true;
                win->needsRedraw_ = false;
                detail::profileFrameBegin(*win);
                win->window().renderFrame();
                // FÖLJDEN — begärd nästa bild redan nu? Frågas bara när
                // profileraren är på: `hasPosted` tar postköns lås.
                detail::profileFrameEnd(
                    *win, win->wantsFrame() || (detail::profiling() && hasPosted()));
            }
        }
        if (ritade)
            detail::externalDesktopDrawn(varv);
        // BILDERNA SOM SLÄPPER SINA PIXLAR: efter varvets ritning,
        // så varje fönster som ritade en bild i varvet hann få den.
        detail::releaseQueuedPixels();
        detail::profilePumpEnd(pumpDepth_);
    }
    --pumpDepth_;
}

// Event-routing: känt fönster-id → det fönstret; id 0 /
// okänt: Quit ⇒ avsluta appen (stäng alla), SystemThemeChanged ⇒ broadcast
// (temabytet rör alla fönster), Tray ⇒ systemfältets register (ikonen är
// appens), Timer med id 0 ⇒ appens timerregister, som bara
// ANTECKNAR — leveransen sker i svansen, övrigt ⇒
// släpp + logga. Events till fönster under uppskjuten stängning släpps tyst.
void App::dispatch(const gfx::Event& e)
{
    if (AppWindow* win = findWindow(e.windowId)) {
        // Popup-fönster stängs när ett tryck landar någon annanstans
        // eller när fokus lämnar (klick-utanför för menyer-som-fönster).
        if (e.type == gfx::EventType::MouseButtonDown ||
            e.type == gfx::EventType::WindowFocusLost)
            closePopups(win);

        // Modal-filtret: bara stackens topp får input. Klick på ett
        // blockerat fönster ⇒ modalen pockar på (raise + fokus).
        // UNDANTAG: ett fönster som (transitivt) ÄGS av stackens topp är
        // modalens eget (Dropdown-popupen i en dialog) — det ska ha input
        // trots filtret, annars är dialogens dropdown död för musen.
        if (!modalStack_.empty() && win != modalStack_.back() &&
            isInputEvent(e.type)) {
            bool ownedByTop = false;
            for (AppWindow* o = win->owner_; o; o = o->owner_)
                if (o == modalStack_.back()) {
                    ownedByTop = true;
                    break;
                }
            if (!ownedByTop) {
                if (e.type == gfx::EventType::MouseButtonDown)
                    modalStack_.back()->window().raise();
                return;
            }
        }
        if (win->open_ && !win->pendingClose_) {
            // Inmatningens tid till fönstrets nästa bild.
            if (isInputEvent(e.type))
                detail::profileInput(*win);
            win->handleEvent(e);
        }
        return;
    }

    switch (e.type) {
    case gfx::EventType::Quit:
        // Bara ägarkedjornas RÖTTER köas:
        // ett ägt fönster stängs i sin rots kaskad, och ett veto
        // där stoppar den också för de ägda. Köades varje fönster för sig
        // hade ett ägt fönsters egen runda kommit EFTER rotens veto — som en
        // egen stängning (Alt+F4): en flytande dockningspanel hade stängts
        // fast appen aldrig avslutades. Ett öppet fönsters ägare är alltid
        // öppen (`showOwned`), så varje fönster nås från en rot.
        // SKÄLET är appavslutet — rötterna får `AppQuit`, de ägda
        // `OwnerClosing` i kaskaden.
        for (AppWindow* win : windows_)
            if (!win->owner_)
                win->requestClose(CloseReason::AppQuit);
        break;
    case gfx::EventType::SystemThemeChanged: {
        // En KOPIA av registret: appens `event()`-krok får riva fönster
        // (kontraktet vid `AppWindow::close()`), och registret frågas innan
        // varje fönster rörs.
        const std::vector<AppWindow*> kopia = windows_;
        for (AppWindow* win : kopia)
            if (std::find(windows_.begin(), windows_.end(), win) !=
                    windows_.end() &&
                win->open_ && !win->pendingClose_)
                win->handleEvent(e);
        break;
    }
    case gfx::EventType::None:
        break;   // "inget hände" — aldrig värt en loggrad
    case gfx::EventType::Wakeup:
        // TRÅDSÄKER VÄCKNING: eventet finns BARA för att få
        // `waitEvent` att returnera så pumpen når sin svans och dränerar
        // postkön. Ingen logg (windowId är 0 med flit, så utan grenen
        // hade den hamnat i "event till okänt fönster") och INGEN
        // invalidering — hård regel 1: det som ska synas invaliderar den
        // postade funktionen själv, via settrarna.
        break;
    case gfx::EventType::Tray:
        // SYSTEMFÄLTETS IKON: appens, inte ett fönsters —
        // windowId är 0 och registret routar på trayId. Modalfiltret ovan
        // gäller fönster; systemfältets motsvarighet (klicken lyfter modalen
        // i stället för att fyras) bor i dispatchen, som får toppen.
        detail::trayDispatch(e, modalStack_.empty() ? nullptr
                                                    : modalStack_.back());
        break;
    case gfx::EventType::Timer:
        // APPENS TIMER: windowId 0 är appens — ingen
        // handler här, bara anteckningen; svansen levererar. Ett annat id är
        // ett stängt fönsters timer och släpps som övrigt.
        if (e.windowId == 0) {
            detail::timerNoteDue(e);
            break;
        }
        [[fallthrough]];
    default:
        gfx::log("App: event %d till okänt fönster %u — släpps",
                 static_cast<int>(e.type), e.windowId);
        break;
    }
}

// Stäng öppna popup-fönster utom target och de fönster target (transitivt)
// ägs av — klick i en undermeny ska inte stänga menyn den hänger i.
void App::closePopups(AppWindow* target)
{
    for (AppWindow* w : windows_) {
        if (!w->popup_ || w == target || !w->open_ || w->pendingClose_)
            continue;
        bool keep = false;
        for (AppWindow* o = target; o; o = o->owner_)
            if (o == w) {
                keep = true;
                break;
            }
        if (!keep)
            w->close();   // uppskjutet — verkställs i pump-svansen
    }
}

// onClose i hela kedjan, barnen djupast först — men UTAN veto. Formen
// finns bara för popuparna nedan: `confirmClose` kortsluter vid första
// `e.cancel` och skulle då hoppa över resten av handlarna, och en popup
// måste få veta att den stängdes (Dropdown nollar sitt `open_` där).
void App::firePopupClose(AppWindow* w, CloseReason skäl)
{
    for (AppWindow* child : w->owned_)
        firePopupClose(child, CloseReason::OwnerClosing);
    w->fireCloseCancelled(skäl);   // returvärdet ignoreras med flit
}

// Som closePopups, men RIVER popuparna direkt. Skälet är xdg-shell: bara
// den ÖVERSTA popupen får förstöras, alltså får en ny grabbande popup inte
// skapas medan den gamla lever (se AppWindow::showOwned).
//
// TVÅ AVSTEG, båda medvetna och båda BEGRÄNSADE TILL POPUPS:
//  1. Stängningen är INTE vetobar. En popup-dismissal är det inte hos
//     kompositören heller (`xdg_popup.popup_done` går inte att neka), och
//     ett veto här hade lämnat den gamla popupen vid liv medan den nya
//     ändå skapades — alltså exakt det protokollbrott formen ska stänga.
//  2. Användarens `onClose` körs därmed här, tidigare i varvet än den
//     vanliga pump-svansen. Det är den enda plats i ramverket där en
//     stängning verkställs synkront, och den gäller bara ett popup-fönster
//     som är på väg att ersättas.
void App::closePopupsNow(AppWindow* target)
{
    closePopups(target);
    // Bara POPUPS plockas ur kön; allt annat uppskjutet hör hemma i
    // svansen. Indexloopen tål att listan VÄXER under tiden — en
    // onClose-handler får kalla close() på fler fönster.
    for (std::size_t i = 0; i < pendingClose_.size();) {
        AppWindow* w = pendingClose_[i];
        if (!w->popup_) {
            ++i;
            continue;
        }
        pendingClose_.erase(pendingClose_.begin() +
                            static_cast<std::ptrdiff_t>(i));
        if (!w->open_)
            continue;   // redan stängd via en kaskad
        firePopupClose(w, w->closeReason_);
        executeClose(w);
    }
}

// TRÅDSÄKER POST — APPENS kö, inom budgeten. Vägen in är
// `enqueue`, som ramverkets egna poster delar (`detail::postFrameWork`).
void App::post(std::function<void()> fn)
{
    enqueue(posted_, std::move(fn));
}

// Budgeten LAGRAS bara: nästa dränering läser den (`drainPosted`). Ett
// negativt tal är fel i anroparen — 0 är obegränsat, inte "ingen tid".
void App::setPostBudget(int ms)
{
    if (ms < 0) {
        gfx::log("App::setPostBudget: %d ms är ingen budget (0 = obegränsat) — "
                 "%d ms gäller fortfarande",
                 ms, postBudgetMs_);
        return;
    }
    postBudgetMs_ = ms;
}

namespace detail {

// RAMVERKETS EGNA POSTER — kontraktet står vid deklarationen
// i app.h. Samma väg in som `App::post`, i den andra kön.
void postFrameWork(std::function<void()> fn)
{
    App& a = app();
    a.enqueue(a.frame_, std::move(fn));
}

} // namespace detail

// ---------------------------------------------------------- EXTERNA BILDKÄLLOR
//
// Kontraktet står vid deklarationerna i app.h. Registret är ett eget lås och
// en egen väg in: en källa har högst ETT väntande jobb och det körs i svansen
// före ramkön (`drainPosted`). Ett jobb tillhör en KÄLLA, och källornas id
// delas ut i stigande ordning och återanvänds aldrig — så "finns källan
// fortfarande" är hela generationskontrollen, och ett jobb som snapshotats
// men inte körts än faller bort om dess källa avregistrerats i ett jobb före.
// Registret lever till processlut utan destruktor (jobben bär handtag som
// ska släppas medan trådarna lever: `externalClose`), som `defaultPool()`.

namespace detail {

namespace {

struct ExternalSource {
    unsigned              hz = 1;
    std::function<void()> job;   // tomt = inget väntar
};

struct ExternalRegistry {
    std::mutex                                      mutex;
    std::map<ExternalFrameSourceId, ExternalSource> sources;
    ExternalFrameSourceId                           next = 0;
    bool                                            closed = false;
    // Skrivbordets period i mikrosekunder: den längsta av källornas, 0 utan
    // källa. Under låset.
    std::int64_t periodUs = 0;
};

ExternalRegistry& externals()
{
    static ExternalRegistry* const r = new ExternalRegistry;
    return *r;
}

// UI-trådens: tidigast nästa skrivbordsritning medan en källa finns.
ExternalClock::time_point g_nextDesktopDraw{};

// Källornas takt → skrivbordets period. Under låset.
void updatePeriod(ExternalRegistry& r)
{
    std::int64_t period = 0;
    for (const auto& [id, s] : r.sources)
        period = std::max<std::int64_t>(period, 1'000'000 / s.hz);
    r.periodUs = period;
}

// Presentationstakten för alla öppna fönster.
void applyPacing(bool on)
{
    for (AppWindow* w : app().windows())
        if (w->isOpen())
            w->window().renderer().setExternalFramePacing(on);
}

} // namespace

ExternalFrameSourceId registerExternalFrameSource(unsigned desktopHz)
{
    app();   // App lat, som `Timer::start`: pacing och väckning kräver plattformen
    ExternalRegistry& r = externals();
    bool              första = false;
    ExternalFrameSourceId id = 0;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        if (r.closed)
            return 0;
        id = ++r.next;
        // 0 Hz är ingen period: skrivbordet ritas då en gång per sekund.
        r.sources[id].hz = std::max(desktopHz, 1u);
        första           = r.sources.size() == 1;
        updatePeriod(r);
    }
    if (första) {
        g_nextDesktopDraw = ExternalClock::now();   // en väntande ritning får gå först
        applyPacing(true);
    }
    return id;
}

bool postExternalFrame(ExternalFrameSourceId source, std::function<void()> job)
{
    if (!job)
        return false;
    ExternalRegistry& r = externals();
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        if (r.closed)
            return false;
        const auto it = r.sources.find(source);
        if (it == r.sources.end() || it->second.job)
            return false;   // okänd källa, eller ett jobb väntar redan
        it->second.job = std::move(job);
    }
    gfx::postWakeup();   // utanför låset, som `App::enqueue`
    return true;
}

void unregisterExternalFrameSource(ExternalFrameSourceId source)
{
    ExternalRegistry& r = externals();
    ExternalSource    borta;   // släpps utanför låset, med sitt väntande jobb
    bool              sista = false;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        const auto it = r.sources.find(source);
        if (it == r.sources.end())
            return;
        borta = std::move(it->second);
        r.sources.erase(it);
        updatePeriod(r);
        sista = r.sources.empty() && !r.closed;
    }
    if (sista)
        applyPacing(false);   // full takt: nästa varv ritar det som väntar
}

bool externalJobPending()
{
    ExternalRegistry&           r = externals();
    std::lock_guard<std::mutex> lås(r.mutex);
    for (const auto& [id, s] : r.sources)
        if (s.job)
            return true;
    return false;
}

void externalRunJobs()
{
    ExternalRegistry& r = externals();
    std::vector<std::pair<ExternalFrameSourceId, std::function<void()>>> jobb;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        if (r.closed)
            return;
        for (auto& [id, s] : r.sources)
            if (s.job) {
                jobb.emplace_back(id, std::move(s.job));
                s.job = nullptr;   // platsen är fri: nästa jobb får köas medan det här kör
            }
    }
    for (auto& [id, fn] : jobb) {
        {
            std::lock_guard<std::mutex> lås(r.mutex);
            if (r.closed || r.sources.find(id) == r.sources.end())
                continue;   // avregistrerad (eller nedstängd) sedan snapshoten
        }
        const PostedCall mät;   // antalet och den längsta (profileraren)
        fn();
    }
}

bool externalPacingActive()
{
    ExternalRegistry&           r = externals();
    std::lock_guard<std::mutex> lås(r.mutex);
    return !r.sources.empty();
}

bool externalDesktopAllowed(ExternalClock::time_point now, std::uint32_t& waitMs)
{
    ExternalRegistry& r = externals();
    std::int64_t      period = 0;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        period = r.periodUs;
    }
    if (period == 0 || now >= g_nextDesktopDraw)
        return true;
    const auto kvar = std::chrono::duration_cast<std::chrono::microseconds>(
        g_nextDesktopDraw - now);
    waitMs = static_cast<std::uint32_t>(
        std::max<std::int64_t>((kvar.count() + 999) / 1000, 1));
    return false;
}

void externalDesktopDrawn(ExternalClock::time_point at)
{
    ExternalRegistry& r = externals();
    std::int64_t      period = 0;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        period = r.periodUs;
    }
    if (period > 0)
        g_nextDesktopDraw = at + std::chrono::microseconds(period);
}

void externalClose()
{
    ExternalRegistry& r = externals();
    std::map<ExternalFrameSourceId, ExternalSource> kvar;
    {
        std::lock_guard<std::mutex> lås(r.mutex);
        r.closed = true;
        kvar.swap(r.sources);
        r.periodUs = 0;
    }
}

} // namespace detail

// VÄGEN IN, för båda köerna. Sättaren gör två saker: lägger funktionen i
// kön under lås, och VÄCKER pumpen. Väckningen måste ligga UTANFÖR låset —
// `postWakeup` går ner i backenden, och att hålla ett lås genom ett
// systemanrop är onödigt (och på Wayland dessutom en väg in i display-lås).
// Också ramkön väcker, som när dess poster gick genom `post`: de kommer ur
// ritningen, och en ritning kan ske UTANFÖR pumpens varv (D3D11:s
// `WM_SIZE`/`WM_PAINT` ritar direkt, inifrån `waitEvent`) — då är det
// väckningens event som får `waitEvent` att återvända till svansen.
// Efter `shutDown()` är kön STÄNGD: funktionen köas inte och ingenting
// väcks. Den förstörs när `fn` går ur scope — efter låset, så en destruktor
// som själv postar inte låser sig.
void App::enqueue(PostQueue& kö, std::function<void()> fn)
{
    if (!fn)
        return;
    {
        std::lock_guard<std::mutex> lås(postMutex_);
        if (postClosed_)
            return;
        kö.push_back(std::move(fn));
    }
    gfx::postWakeup();
}

// Båda köerna — det budgeten lämnade kvar ligger i appens.
bool App::hasPosted() const
{
    std::lock_guard<std::mutex> lås(postMutex_);
    return !frame_.empty() || !posted_.empty();
}

// SVANSENS POSTADE ARBETE: RAMKÖN först och hel — ticken och
// kamerans händelse ur förra bildens ritning, `onResize` — sedan APPENS kö
// inom budgeten. Med appens först hade ticken väntat bakom en skur av
// trådarnas svar, och nästa bild hade ritats utan rörelse.
void App::drainPosted()
{
    // En extern bildkälla (app.h) går före båda köerna: dess bildruta har en
    // tidsfrist som varken ramkön eller appens budget ska ligga ivägen för.
    detail::externalRunJobs();
    runQueue(frame_, 0);
    runQueue(posted_, postBudgetMs_);
}

// EN KÖ i pump-SVANSEN, på UI-tråden. SNAPSHOT via swap: funktionerna körs
// UTANFÖR låset, så en postad lambda får posta nya (de tas nästa varv —
// ingen svältloop) och får öppna en modal dialog utan att hålla låset
// genom en hel nästlad pump. En nästlad pump kör det som postats efter
// snapshoten; snapshotens rest körs här när dialogen stängts.
// BUDGETEN (> 0 ms, väggtid från köns början — också en nästlad pumps)
// frågas EFTER varje funktion, så den första körs alltid och kön går
// framåt. Det som inte hann läggs FÖRST i kön igen, under låset — före allt
// som postats under tiden, också från andra trådar och från en nästlad
// pumps egen rest — så FIFO består. En kö som stängts under tiden
// (`shutDown`) tar inte emot resten: den förstörs med snapshoten, efter
// låset.
void App::runQueue(PostQueue& kö, int budgetMs)
{
    PostQueue snapshot;
    {
        std::lock_guard<std::mutex> lås(postMutex_);
        if (kö.empty())
            return;
        snapshot.swap(kö);
    }
    using Klocka = std::chrono::steady_clock;
    const Klocka::time_point slut =
        Klocka::now() + std::chrono::milliseconds(budgetMs);
    std::size_t körda = 0;
    while (körda < snapshot.size()) {
        {
            const detail::PostedCall mät;   // antalet och den längsta (profileraren)
            snapshot[körda]();
        }
        ++körda;
        if (budgetMs > 0 && Klocka::now() >= slut)
            break;
    }
    if (körda == snapshot.size())
        return;
    std::lock_guard<std::mutex> lås(postMutex_);
    if (!postClosed_)
        kö.insert(kö.begin(),
                  std::make_move_iterator(snapshot.begin() +
                                          static_cast<std::ptrdiff_t>(körda)),
                  std::make_move_iterator(snapshot.end()));
}

// En leverans får riva fönster — också andra än sitt eget — så vi går en
// KOPIA av registret och frågar det innan varje fönster rörs.
void App::drainCommitQueues()
{
    const std::vector<AppWindow*> kopia = windows_;
    for (AppWindow* w : kopia)
        if (std::find(windows_.begin(), windows_.end(), w) != windows_.end() &&
            !w->commitQueue_.empty())
            w->drainCommitQueue();
}

void App::queueClose(AppWindow* w)
{
    if (std::find(pendingClose_.begin(), pendingClose_.end(), w) ==
        pendingClose_.end())
        pendingClose_.push_back(w);
}

void App::flushPendingCloses()
{
    // executeClose kan kaskad-stänga fönster som själva står i listan —
    // de är redan stängda (open_ falskt) när turen kommer och hoppas över.
    while (!pendingClose_.empty()) {
        AppWindow* w = pendingClose_.front();
        pendingClose_.erase(pendingClose_.begin());
        if (!w->open_)
            continue;

        // onClose-vetot (fråga 15): fråga delträdet innan något rivs.
        // Avblåst stängning = fönstret lever vidare som om inget hänt — också
        // RESULTATET: `closeWithResult(OK)` + veto + ett
        // senare kryss hade annars svarat OK ur `run(ägare)` — och SKÄLET:
        // nästa stängning börjar om som fönstrets egen.
        if (!confirmClose(w, w->closeReason_)) {
            w->pendingClose_ = false;
            w->result_       = DialogResult::None;
            w->closeReason_  = CloseReason::UserClosing;
            // EN VETAD STÄNGNING FÖRE DEN FÖRSTA VISNINGEN:
            // `close()` i `onLoad` hoppar över visningen efter handlern, och
            // fönstret lever vidare — det fortsätter dit det var på väg, annars
            // stod det öppet och osynligt, och en modal `run()` blockerade bakom
            // en dialog ingen ser. En begärd
            // dold start (`hide()` i `onLoad` eller i vetots handler) står kvar.
            if (w->open_ && w->firstShowPending_ && w->showAfterLoad_)
                w->revealHidden();
            continue;
        }
        executeClose(w);
    }
}

// Vetorundan: barnen DJUPAST FÖRST, sedan fönstret självt. Första cancel
// avbryter hela kaskaden — stänger man ett ägarfönster och ett ägt barn
// säger nej, får varken barnet eller ägaren stängas. Barnen frågas med
// `OwnerClosing`, vad ägaren än stängs för.
bool App::confirmClose(AppWindow* w, CloseReason skäl)
{
    for (AppWindow* child : w->owned_)
        if (!confirmClose(child, CloseReason::OwnerClosing))
            return false;
    return !w->fireCloseCancelled(skäl);
}

// River OS-fönstret och avregistrerar. Kaskaden: BARNEN FÖRE
// ÄGAREN, och ett stängt barn plockar bort sig ur ägarens lista. Raderar
// aldrig C++-objektet — det ägs av användaren.
void App::executeClose(AppWindow* w)
{
    if (!w->open_)
        return;
    while (!w->owned_.empty())
        executeClose(w->owned_.back());

    // FÖNSTRETS GEOMETRI — KORTET tas HÄR, medan
    // OS-fönstret finns: `geometry()` efter stängningen läser det, så
    // `run(); spara(win.geometry());` fungerar. Stängdes fönstret maximerat
    // eller minimerat får klientstorleken dessutom tillbaka NORMALrutans —
    // `trackClientSize` följer den aktuella ytan, och utan raderna hade
    // fönstret öppnats nästa gång i normalläge men med den maximerade
    // storleken.
    // Ett fönster som stängs innan det SYNTS (`close()` i `onLoad`, en dold
    // start) får begäran som kort: OS-fönstret står kvar på init:s
    // plats och läge, som aldrig gällde.
    w->closedGeometry_ = w->firstShowPending_ ? w->requestedGeometry()
                                              : w->liveGeometry();
    if (w->closedGeometry_->state != gfx::WindowState::Normal &&
        w->closedGeometry_->width > 0 && w->closedGeometry_->height > 0) {
        w->initW_ = w->closedGeometry_->width;
        w->initH_ = w->closedGeometry_->height;
    }

    // Ägaren sparas FÖRE bokföringen nedan nollar w->owner_ — behövs för
    // att kunna återaktivera ägarfönstret efter rivningen (se slutet).
    AppWindow* owner = w->owner_;

    w->open_         = false;
    w->pendingClose_ = false;
    w->closeReason_  = CloseReason::UserClosing;   // nästa liv börjar om
    w->hidden_       = false;
    // Inget aktivt fönster och inga väntande `onResize` — ett stängt fönster levererar inget.
    w->active_       = false;
    w->resizeQueue_.clear();
    // …också fasen före den första visningen: ett stängt fönster
    // visas inte efter sin `onLoad` (`showOwned` läser `open_` först).
    w->firstShowPending_ = false;
    w->showAfterLoad_    = false;
    // Inga hållna tangenter: släppet kommer aldrig till ett
    // stängt fönster, och `isKeyHeld` ska svara nej på ett objekt som lever
    // vidare efter sin stängning. Tangentminnet töms TYST
    // —ingen användarkod medan ett fönster stängs, som för trycken.
    w->heldKeys_.reset();
    w->keyTargets_.clear();
    modalStack_.erase(std::remove(modalStack_.begin(), modalStack_.end(), w),
                      modalStack_.end());
    // Ur stängningskön också: en stängning som verkställs HÄR (kaskaden,
    // eller `~AppWindow` för ett objekt som dör med sin `close()` köad)
    // lämnade annars pekaren kvar, och `flushPendingCloses` läste ett lik —
    // eller stängde fel fönster, när ett nytt hamnat på samma adress. Båda
    // köloopparna tål att poster försvinner under tiden (se dem).
    pendingClose_.erase(
        std::remove(pendingClose_.begin(), pendingClose_.end(), w),
        pendingClose_.end());
    if (w->owner_) {
        auto& siblings = w->owner_->owned_;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), w),
                       siblings.end());
        w->owner_ = nullptr;
    }
    windows_.erase(std::remove(windows_.begin(), windows_.end(), w),
                   windows_.end());

    // Koppla loss callbacken FÖRE rivningen: backenden kan rita mitt i
    // DestroyWindow (WM_PAINT/WM_SIZE) och vid en senare åter-show skickar
    // CreateWindowExW WM_SIZE INNAN show() hunnit koppla om — en kvarsittande
    // callback skulle då rita med död font.
    w->window().setRenderCallback(nullptr);
    w->window().setOverlayCallback(nullptr);
    // Profilerarens ring för fönstret släpps — popuperna
    // kommer och går.
    detail::profileWindowClosed(*w);

    // Städordningen per fönster: fontens OCH bildcachens texturer hör
    // till renderern — släpp FÖRE win_->shutdown().
    // Ritytans resurser först: de skapades sist (efter
    // bildcachen), och sedan images_.
    w->canvas_.reset();
    w->images_.reset();
    w->font_.reset();
    // ROLLFONTERNA hör till SAMMA renderer — en font-atlas är
    // en textur, så de måste släppas i det HÄR blocket och inte av
    // fönstrets dtor. Missas det kraschar teardown, inte första körningen.
    w->headingFont_.reset();
    // …och TEXTFONTERNA av exakt samma skäl: de lata
    // varianterna är fonter i samma renderer.
    w->fontSet_.reset();
    w->window().shutdown();

    // OS:et återaktiverar inte alltid ägaren tillförlitligt när ett ägt/modalt
    // fönster rivs (Win32-kvirk i nästlade ägarkedjor: nästa foreground-HWND
    // kan falla igenom till fel fönster i den GLOBALA z-ordningen). Lyft fram
    // ägaren explicit — men BARA om den överlever den här stängningen: vid
    // kaskad-stängning (ägaren stängs medan barnet rivs först) är ägarens
    // pendingClose_ fortfarande satt, och då ska vi inte blinka fram den en
    // rad innan den själv rivs.
    //
    // ALDRIG FÖR EN POPUP. En popup tar aldrig
    // aktiveringen (`WindowDesc::noActivate` i alla backends — fokus-
    // kontraktet i gfx/window.h), så det finns ingenting att ge tillbaka. Och
    // för tooltipen vore lyftet SKADLIGT: den visas över ett fönster pekaren
    // bara VILAR på, som inte behöver vara aktivt — och när den stängdes
    // skulle ägaren aktiveras och ta fokus från appen användaren faktiskt
    // arbetade i.
    // En DOLD ägare lyfts inte — det är fasadens kontrakt (`gfx::Window::raise`).
    if (owner && owner->open_ && !owner->pendingClose_ && !w->popup_)
        owner->window().raise();
}

bool App::anyWantsFrame() const
{
    for (AppWindow* w : windows_)
        if (w->wantsFrame())
            return true;
    return false;
}

AppWindow* App::findWindow(gfx::WindowId id) const
{
    if (id == 0)
        return nullptr;
    for (AppWindow* w : windows_)
        if (w->window().id() == id)
            return w;
    return nullptr;
}

} // namespace glaze
