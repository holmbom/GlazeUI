// SYSTEMFÄLTETS IKON — `glaze::TrayIcon`, registret,
// synkningen mot gfx/tray.h, animationens tidskälla, menyns värdfönster och
// dispatchen från App. Kontraktet utåt står i glaze/tray_icon.h, den rena
// logiken (animationen, routningen, ankaret) i src/tray_model.h och
// kopplingen till App i src/tray_registry.h.
//
// Fyra saker att inte råka bryta:
//
//   1. INGET GFX-ANROP FÖRE `show()`. Settrarna, kollektionsvokabulären och
//      menyns platta form lagrar bara tills ikonen syns — grinden prövar en
//      riktig `TrayIcon` på Windows utan att någonsin lägga den i
//      utvecklarens systemfält, och Windows minns varje programfil
//      som gjort det.
//   2. EN HANDLER FÅR RIVA IKONEN. Dispatchen slår upp id OCH generation
//      igen efter varje handler och slutar där om registreringen är en
//      annan — en ny ikon kan ha fått samma id och samma adress.
//   3. MENYN ÖPPNAS I PUMP-SVANSEN, efter att värdfönstret tagit fokus.
//      Fokusbytet ger det fönster som hade fokus ett `WindowFocusLost`, och
//      `App::dispatch` stänger då VARJE popup som inte ägs av det fönstret —
//      också den nya menyn. Pumpen tömmer kön innan den kör postade
//      funktioner, så en postad öppning kommer efter fokusbytet.
//   4. VÄRDARNA ÄGS AV REGISTRET och rivs aldrig medan en ikon pekar på dem
//      eller medan de är öppna: menykedjan, App:s stängningskö och en postad
//      öppning håller råa pekare dit.

#include "glaze/tray_icon.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gfx/platform.h>
#include <gfx/tray.h>

#include "glaze/app.h"
#include "glaze/app_window.h"

#include "menu_path.h"
#include "menu_query.h"
#include "prop_parse.h"
#include "tray_model.h"
#include "tray_registry.h"
#include "window_icon.h"

namespace glaze {

// KOMPONENTEN — INERT för previewn per konstruktion:
// konstruktorn rör bara registret och settrarna lagrar tills `show()`, som
// previewn aldrig utför (punkt 1 nedan).
GLAZE_REGISTER_COMPONENT("glaze::TrayIcon", TrayIcon,
                         ::glaze::PreviewForm::Inert)

namespace {
// Loggens avsändare i menystigens hjälpare (src/menu_path.h).
constexpr const char* kVem = "TrayIcon"; // lang_gate:rad loggens avsändare, ingen användartext

// Systemfältets största ram: fältet visar en LITEN bild — SM_CXSMICON
// är 48 px vid 300 % och macOS menyrad 18 pt × 2 = 36 px — så de stora
// storlekarna i `kIconSizes` avkodas och skalas aldrig för det.
constexpr int kTrayFrameMax = 64;

// Sträckan i värdens klient-px som kvoten placeringsrymd per fysisk px mäts
// över — lång, så att en rymd som avrundar till hela enheter ändå ger kvoten
// exakt nog.
constexpr float kKvotSträcka = 1000.0f;
} // namespace

// ============================================================ VÄRDFÖNSTRET
//
// SYSTEMFÄLTSMENYNS VÄRD. Våra menyer stängs av VÄRDENS fokusförlust
// (`App::dispatch` → `closePopups`) och tar sina tangenter genom värdens
// menykrok — men ikonen sitter i OS:ets fält, och appens fönster kan vara
// dolda eller sakna fokus. Värden är därför ett eget fönster: genomskinligt,
// 1 × 1 och utan chrome, ett VERKTYGSFÖNSTER (ingen knapp, inte i Alt+Tab)
// som TAR fokus — `TrackPopupMenu`-fällans lösning ("the current window must
// be the foreground window") i vår form. Den skapas på ankarets hörn, så den
// får rätt skärm och DPI från början, och den stängs när menykedjan stängts.
//
// I namnrymden `glaze` och inte i en anonym: `AppWindow` är vän med just
// `glaze::TrayHostWindow` (`toolWindow_` och `initX_`/`initY_`).
class TrayHostWindow final : public AppWindow {
public:
    TrayHostWindow() : AppWindow("", 1, 1, gfx::Chrome::None)
    {
        setTransparent(true);      // ingenting syns
        toolWindow_ = true;        // ingen knapp och inte i Alt+Tab — men fokus
        appIcon.setNoIcon(true);   // ingen OS-ikon att avkoda
    }

    // Platsen för NÄSTA öppning, i placeringsrymden (`WindowDesc::posX`).
    void placeAt(int x, int y)
    {
        initX_ = x;
        initY_ = y;
    }
};

namespace detail {

// Bilden ikonen visar NU — en animationsruta eller den stilla ikonen — som
// de bildvärden `osIcon` tar (src/window_icon.h): en ogiltig stor bild är
// FÖRVALET, och programfilens egen ikon går då före. Två lika bilder ger
// samma ikon i fältet — och likheten är bildvärdets (image_source.h): samma
// Bitmap i en NY version är en annan bild, så ändrade pixlar läggs om.
struct TrayBild {
    ImageSource stor{};
    ImageSource liten{};

    bool operator==(const TrayBild&) const = default;
};

// HELA INNEHÅLLET i en `gfx::setTrayIcon` — `TrayIconDesc`s tre delar, med
// bilden som handtag. Det som skickades sist jämförs med det som gäller nu,
// så en ändring som inte ändrar innehållet skickar ingenting.
struct TrayInnehåll {
    TrayBild    bild;
    std::string tips;
    bool        mall = false;

    bool operator==(const TrayInnehåll&) const = default;
};

// En registrering: id:t och generationen. Generationen är unik per
// registrering, så paret pekar ut ETT objekts liv även när ett nytt fått
// samma id — och samma adress.
struct TrayLiv {
    gfx::TrayId   id  = 0;
    std::uint64_t gen = 0;
};

namespace {

// Ett levande objekts post i registret.
struct Post {
    TrayIcon*     ikon = nullptr;
    std::uint64_t gen  = 0;
};

// REGISTRET — id → objekt, generationsräknaren och menyernas värdfönster.
// Värdarna bor här av LIVSTIDSSKÄL: registret skapas av den första
// `TrayIcon`-konstruktorn och överlever därmed varje ikon, också en global,
// så en ikon som rivs vid processlut pekar aldrig på en värd som redan
// rivits. En värd som rivs efter `App` är redan stängd (App river varje
// öppet fönster i sin destruktor).
struct Registret {
    std::map<gfx::TrayId, Post>                  poster;
    std::uint64_t                                nästaGen = 0;
    std::vector<std::unique_ptr<TrayHostWindow>> värdar;
};

Registret& registret()
{
    static Registret r;
    return r;
}

} // namespace

struct TrayState {
    // REGISTRET — det lägsta lediga id:t och registreringens
    // generation. id 0 = utan plats (alla 65535 tagna).
    gfx::TrayId   id  = 0;
    std::uint64_t gen = 0;

    // ANIMATIONEN och HOVERN — den rena modellen (src/tray_model.h).
    // `anim.shown` är backendens svar: står ikonen i OS:ets fält?
    TrayAnimation anim;
    bool          hovered = false;

    // DET SOM GÅTT TILL BACKENDEN. `skickat` är innehållet i den senaste
    // `setTrayIcon` som backenden TOG EMOT — tomt före första `show()`, efter
    // `hide()` och efter en vägran (som då prövas igen vid nästa synk). Tomt och inte ett förvalt innehåll: ett
    // `TrayInnehåll{}` är ett riktigt innehåll (förvalet utan tooltip), som
    // första `show()` måste skicka. `timerMs` är tidskällans period (0 = av).
    std::optional<TrayInnehåll> skickat;
    std::uint32_t               timerMs = 0;

    // MENYN. `host` är värden för den kedja som visas eller väntar på
    // pump-svansen (`menyVäntar`); `menyBegäran` räknar öppningarna, så en
    // postad öppning som ersatts eller avbrutits känner igen sig. `ankare` är
    // menyns ankare i placeringsrymden — `ankareKlämt` när det är ikonens
    // rect inklämd i arbetsytan, annars ett klicks punkt som klämms mot
    // värdens arbetsyta. `klick` är det senaste klickets ankare (reserven
    // när OS:et inte kan säga var ikonen står).
    TrayHostWindow* host        = nullptr;
    std::uint64_t   menyBegäran = 0;
    bool            menyVäntar  = false;
    gfx::Rectf      ankare{};
    bool            ankareKlämt = false;
    gfx::Pointf     klick{};
    bool            harKlick    = false;

    TrayLiv  liv() const { return TrayLiv{id, gen}; }
    TrayBild bild(const TrayIcon& t) const;

    // Synkningen mot backenden (gfx/tray.h).
    void sync(TrayIcon& t);
    void syncTimer(const TrayIcon& t);

    // Menyn och värden.
    void openMenu(TrayIcon& t);
    void chainClosed(TrayIcon& t);
    void cancelPending();
    void releaseHost();

    // Registret och App:s ingångar (src/tray_registry.h).
    static TrayIcon*       levande(const TrayLiv& l);
    static void            enroll(TrayIcon& t);
    static void            withdraw(TrayIcon& t);
    static TrayHostWindow* takeHost();
    static void            openPosted(TrayLiv l, std::uint64_t begäran);
    static void            dispatch(const gfx::Event& e, AppWindow* modalTop);
    static void            languageChanged();
    static std::uint32_t   idOf(const TrayIcon& t) { return t.state_->id; }
};

// ------------------------------------------------------------- registret

TrayIcon* TrayState::levande(const TrayLiv& l)
{
    const auto& p  = registret().poster;
    const auto  it = p.find(l.id);
    return it != p.end() && it->second.gen == l.gen ? it->second.ikon
                                                    : nullptr;
}

void TrayState::enroll(TrayIcon& t)
{
    Registret& r = registret();
    // Det LÄGSTA lediga id:t — kartan är sorterad, så den första
    // luckan. Samma id mellan körningar låter Windows 11 minnas var
    // användaren placerade ikonen.
    gfx::TrayId ledigt = 1;
    for (const auto& kv : r.poster) {
        if (kv.first != ledigt)
            break;
        ++ledigt;
    }
    if (ledigt > gfx::kTrayIdMax) {
        gfx::log("TrayIcon: alla %u id i systemfältet är tagna — ikonen kan "
                 "aldrig visas",
                 static_cast<unsigned>(gfx::kTrayIdMax));
        return;
    }
    t.state_->id  = ledigt;
    t.state_->gen = ++r.nästaGen;
    r.poster.emplace(ledigt, Post{&t, t.state_->gen});
}

void TrayState::withdraw(TrayIcon& t)
{
    auto&      p  = registret().poster;
    const auto it = p.find(t.state_->id);
    if (it != p.end() && it->second.ikon == &t)
        p.erase(it);
    t.state_->id = 0;
}

// En STÄNGD värd återanvänds (Dropdown-listans mönster: objektet lever, och
// OS-fönstret skapas om vid varje öppning), och de stängda utöver den rivs.
// En värd som någon ikon fortfarande pekar på rivs aldrig — en sådan som är
// stängd är en vars stängning ikonen ännu inte sett.
TrayHostWindow* TrayState::takeHost()
{
    Registret& r         = registret();
    const auto refererad = [&r](const TrayHostWindow* v) {
        for (const auto& kv : r.poster)
            if (kv.second.ikon->state_->host == v)
                return true;
        return false;
    };
    TrayHostWindow* ledig = nullptr;
    for (auto it = r.värdar.begin(); it != r.värdar.end();) {
        TrayHostWindow* v = it->get();
        if (v->isOpen() || refererad(v)) {
            ++it;
        } else if (!ledig) {
            ledig = v;
            ++it;
        } else {
            it = r.värdar.erase(it);
        }
    }
    if (!ledig) {
        r.värdar.push_back(std::make_unique<TrayHostWindow>());
        ledig = r.värdar.back().get();
    }
    return ledig;
}

// ---------------------------------------------------------- synkningen

TrayBild TrayState::bild(const TrayIcon& t) const
{
    // En startad animation med minst en bild visar sin ruta; ett ogiltigt
    // handtag i listan är förvalet i den rutan (kontraktet i headern).
    const int ruta = anim.shownFrame();
    if (ruta >= 0 && static_cast<std::size_t>(ruta) < t.frames_.size())
        return TrayBild{t.frames_[static_cast<std::size_t>(ruta)], {}};
    // Den stilla ikonen. Utan egen bild är förvalet HELA paret, så en liten
    // bild utan stor ändrar inte vad fältet visar.
    return t.icon_.valid() ? TrayBild{t.icon_, t.small_} : TrayBild{};
}

// SYNKNINGEN — den enda vägen som lägger ikonen i OS:ets fält eller
// uppdaterar den (att ta bort den är `hide()`s), och varje ändring av det
// ikonen visar slutar här: bilderna, animationen, tooltipen, mallflaggan,
// synligheten och språket. Ikonen läggs om bara när den är synlig,
// plattformen har ett fält och innehållet skiljer sig från det som skickades
// sist — hela innehållet på en gång, och `anim.shown` blir backendens svar.
// Sist följer tidskällan modellen.
void TrayState::sync(TrayIcon& t)
{
    if (t.visible_ && id != 0 && gfx::trayAvailable()) {
        TrayInnehåll nu{bild(t), t.toolTip_.str(), t.template_};
        if (!skickat || *skickat != nu) {
            gfx::TrayIconDesc d;
            d.image = osIcon(nu.bild.stor, nu.bild.liten, kTrayFrameMax);
            if (d.image.images.empty() && !d.image.programFirst) {
                // Appens bild gick inte att avkoda (`gfx::decodeImage` — eller
                // en RenderTexture, som inte kan bli en OS-ikon — har sagt
                // varför). En ikon som inte syns är en app som inte går att
                // nå — förvalet i stället.
                d.image = osIcon(ImageSource{}, ImageSource{}, kTrayFrameMax);
            }
            d.toolTip       = nu.tips;
            d.templateImage = nu.mall;
            anim.shown      = gfx::setTrayIcon(id, d);
            // Bara det backenden TOG EMOT är skickat — en vägran (loggad där)
            // prövas igen vid nästa synk i stället för att glömmas.
            if (anim.shown)
                skickat = std::move(nu);
            else
                skickat.reset();
        }
    }
    syncTimer(t);
}

// Tidskällan går bara medan modellen vill (synlig, startad, minst två
// bilder) — annars sover appen (regel 1). Ett nytt intervall startar om
// perioden, så det gäller från nästa bild.
void TrayState::syncTimer(const TrayIcon& t)
{
    const std::uint32_t vill =
        anim.wantsTimer() ? static_cast<std::uint32_t>(t.intervalMs_) : 0u;
    if (vill == timerMs)
        return;
    timerMs = vill;
    gfx::setTrayTimer(id, vill);
}

// -------------------------------------------------------------- menyn

void TrayState::openMenu(TrayIcon& t)
{
    const TrayLiv mitt = liv();

    // En kedja som ligger kvar — oftast en som redan stängs, för klicket på
    // ikonen tog fokus från dess värd — stängs uttryckligen, och dess värd
    // med den. Den nya menyn får en egen värd. Stängningen är uppskjuten,
    // men en kedja vars rotfönster redan är borta avslutas här och kör då
    // appens `onClose`, som får riva ikonen.
    t.menu_.close();
    if (!levande(mitt))
        return;
    releaseHost();

    // ANKARET: ikonens rect inklämd i arbetsytan, så att menyn hamnar
    // kant i kant med aktivitetsfältet respektive under menyraden. Kan OS:et
    // inte svara: det senaste klickets punkt.
    gfx::Rectf ikon, yta;
    if (gfx::trayIconRect(id, ikon, yta)) {
        ankare      = trayMenuAnchor(ikon, yta);
        ankareKlämt = true;
    } else if (harKlick) {
        ankare      = gfx::Rectf{klick.x, klick.y, 1.0f, 1.0f};
        ankareKlämt = false;
    } else {
        gfx::log("TrayIcon(%s): openMenu — varken ikonens plats eller ett "
                 "klick att lägga menyn vid",
                 t.name.c_str());
        return;
    }

    // VÄRDEN skapas på ankarets hörn och tar fokus. Ingen användarkod körs
    // här: värden har inga handlers och är ingen popup.
    TrayHostWindow* värd = takeHost();
    värd->placeAt(static_cast<int>(std::lround(ankare.x)),
                  static_cast<int>(std::lround(ankare.y)));
    värd->show();
    if (!värd->isOpen()) {
        gfx::log("TrayIcon(%s): menyns värdfönster kom aldrig upp",
                 t.name.c_str());
        return;
    }
    värd->window().raise();
    host       = värd;
    menyVäntar = true;

    // ÖPPNINGEN I PUMP-SVANSEN (punkt 3 överst): när den körs har fokusbytet
    // levererats, och ett annat av våra fönsters fokusförlust har redan
    // stängt det den skulle stänga — inte den här menyn.
    const std::uint64_t nr = ++menyBegäran;
    glaze::app().post([mitt, nr] { openPosted(mitt, nr); });
}

void TrayState::openPosted(TrayLiv l, std::uint64_t begäran)
{
    TrayIcon* ikon = levande(l);
    if (!ikon)
        return;   // ikonen dog under tiden — dess destruktor stängde värden
    TrayIcon&  t = *ikon;
    TrayState& s = *t.state_;
    if (!s.menyVäntar || s.menyBegäran != begäran)
        return;   // avbruten (closeMenu, hide) eller ersatt av en ny öppning
    s.menyVäntar         = false;
    TrayHostWindow* värd = s.host;
    if (!värd || !värd->isOpen()) {
        s.host = nullptr;   // värden stängdes under tiden (Alt+F4)
        return;
    }
    if (!t.visible_ || t.menu_.items.empty()) {
        s.releaseHost();    // posterna tömdes under tiden
        return;
    }

    // Menyns ankare i värdens KLIENT-FYSISKA px (popup-kontraktets rymd):
    // ankaret relativt värdens klientorigo, delat med kvoten placeringsrymd
    // per fysisk px — 1 på Windows, 1 / backing på macOS. Relativt origo och
    // inte {0, 0}: OS:et får ha flyttat värden. Ett klicks punkt klämms här
    // mot arbetsytan på värdens skärm.
    gfx::Window& w = värd->window();
    gfx::Rectf   a = s.ankare;
    if (!s.ankareKlämt)
        a = trayMenuAnchor(a, w.screenArea());
    const gfx::Pointf o    = w.clientToScreen(0.0f, 0.0f);
    float             kvot = (w.clientToScreen(kKvotSträcka, 0.0f).x - o.x) /
                 kKvotSträcka;
    if (!(kvot > 0.0f))
        kvot = 1.0f;   // en rymd utan skärmkoordinater (finns inte där fältet finns)
    t.menu_.popupAtRect(värd->root(),
                        gfx::Rectf{(a.x - o.x) / kvot, (a.y - o.y) / kvot,
                                   a.w / kvot, a.h / kvot});

    // Öppningen stänger andra popups synkront (`App::closePopupsNow`), och
    // deras `onClose` är användarkod som får riva ikonen.
    if (!levande(l))
        return;
    if (!menuPresentationHostedIn(värd->root())) {
        gfx::log("TrayIcon(%s): menyn kom aldrig upp — värden stängs",
                 t.name.c_str());
        if (s.host == värd)
            s.host = nullptr;
        värd->close();
    }
}

// `Menu::onChainClosed` — kedjan är stängd (klick utanför, Escape, en
// aktivering): värden stängs, uppskjutet, för vi står mitt i App:s
// stängningsrunda. Inte medan en öppning väntar — då är `host` den nya
// värden, och kedjan som stängdes hörde till den gamla.
void TrayState::chainClosed(TrayIcon& t)
{
    if (!t.menu_.isOpen() && host && !menyVäntar) {
        host->close();
        host = nullptr;
    }
}

// En väntande öppning avbryts: värden har tagit fokus men har ingen meny.
void TrayState::cancelPending()
{
    if (menyVäntar)
        releaseHost();
}

void TrayState::releaseHost()
{
    menyVäntar = false;
    if (host) {
        host->close();
        host = nullptr;
    }
}

// ------------------------------------------------------------ dispatchen

void TrayState::dispatch(const gfx::Event& e, AppWindow* modalTop)
{
    TrayIcon* ikon = nullptr;
    TrayLiv   l;
    {
        const auto& p  = registret().poster;
        const auto  it = p.find(e.trayId);
        if (it == p.end())
            return;   // okänt id — en ikon som dog med eventet i kön
        ikon = it->second.ikon;
        l    = TrayLiv{e.trayId, it->second.gen};
    }
    TrayIcon&  t = *ikon;
    TrayState& s = *t.state_;

    // VAR det hände — menyns reserv när OS:et inte kan säga var ikonen står.
    if (e.trayAction == gfx::TrayAction::Click ||
        e.trayAction == gfx::TrayAction::DoubleClick ||
        e.trayAction == gfx::TrayAction::ContextMenu) {
        s.klick    = gfx::Pointf{e.mouseX, e.mouseY};
        s.harKlick = true;
    }

    const TrayRoute väg =
        routeTray(e.trayAction, t.visible_, modalTop != nullptr, s.hovered);

    // INPUT UNDER EN MODAL: ingen handler — dialogen lyfts, som vid ett
    // klick på ett blockerat fönster. `raise()` på en dialog vars stängning
    // redan är köad är ofarligt: den är öppen tills pump-svansen river den.
    if (väg.raiseModal && modalTop) {
        modalTop->window().raise();
        return;
    }
    if (!väg.fire)
        return;

    switch (e.trayAction) {
    case gfx::TrayAction::Tick:
        s.anim.tick();
        s.sync(t);
        return;
    case gfx::TrayAction::Click:
        if (t.onClick) {
            ClickArgs a;
            t.onClick(&t, a);
        }
        break;
    case gfx::TrayAction::DoubleClick:
        if (t.onDoubleClick) {
            ClickArgs a;
            t.onDoubleClick(&t, a);
        }
        break;
    case gfx::TrayAction::ContextMenu:
        if (t.onContextMenu) {
            TrayMenuArgs a;
            t.onContextMenu(&t, a);
        }
        break;
    case gfx::TrayAction::Enter:
    case gfx::TrayAction::Leave:
        // Routningen släppte bara fram det som ÄNDRADE hovern.
        if (t.onHover) {
            HoverArgs a;
            a.entered = e.trayAction == gfx::TrayAction::Enter;
            t.onHover(&t, a);
        }
        break;
    }

    // Handlern fick riva ikonen — eller riva den och skapa en ny med samma
    // id: registreringen slås upp igen innan något mer rörs.
    if (!levande(l))
        return;
    // Menyn EFTER handlern, som fick bygga och tända posterna — och bara om
    // den har poster (`openMenu` prövar).
    if (väg.openMenu)
        t.openMenu();
}

// Tooltipen är OS:ets egen och bärs av ingen widget: varje ikon synkas,
// och en synlig ikon vars upplösta text ändrats trycker om den. Bara fasaden
// anropas här — ingen användarkod — så kartan går rakt av. Fasaden är säker
// också när `gfx::init` misslyckades (den vägrar då, och ikonen räknas som
// inte i fältet).
void TrayState::languageChanged()
{
    for (const auto& kv : registret().poster)
        kv.second.ikon->state_->sync(*kv.second.ikon);
}

} // namespace detail

// ================================================================ TrayIcon

TrayIcon::TrayIcon() : state_(std::make_unique<detail::TrayState>())
{
    detail::TrayState::enroll(*this);
    menu_.onChainClosed = [this] { state_->chainClosed(*this); };
}

// `Menu`s destruktor kopplar loss en öppen kedja UTAN att köra
// `onChainClosed` — värden stängs därför här. Den lever kvar i registret tills
// App:s pump-svans rivit den, efter menyn den bär.
TrayIcon::~TrayIcon()
{
    hide();
    state_->releaseHost();
    detail::TrayState::withdraw(*this);
}

bool TrayIcon::isSupported()
{
    return gfx::trayAvailable();
}

// ------------------------------------------------- previewns väg
//
// Ett ogiltigt värde för en KÄND nyckel är true, loggat och ignorerat
// (kontraktet vid `Widget::setProp`); en okänd nyckel är false.
bool TrayIcon::setProp(const std::string& key, const std::string& value)
{
    if (key == "interval") {
        int v = 0;
        if (!propparse::parseInt(value, v)) {
            gfx::log("TrayIcon(%s): '%s' är inget heltal för interval "
                     "(ignoreras)",
                     name.c_str(), value.c_str());
            return true;
        }
        setInterval(v);
        return true;
    }
    if (key == "templateImage") {
        if (value == "true")
            setTemplateImage(true);
        else if (value == "false")
            setTemplateImage(false);
        else
            gfx::log("TrayIcon(%s): '%s' är inte true/false för "
                     "templateImage (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "toolTip") {
        setToolTip(value);   // en literal — ett tr-id går via setTextToken
        return true;
    }
    return false;
}

bool TrayIcon::setTextToken(const std::string& key, const TrText& value)
{
    if (key == "toolTip") {
        setToolTip(value);
        return true;
    }
    return false;
}

// ---------------------------------------------------------- synligheten

void TrayIcon::show()
{
    glaze::app();   // App lat, som ett fönsters show(): fältet kräver gfx::init
    visible_ = true;
    if (!isSupported()) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            gfx::log("TrayIcon(%s): plattformen har inget systemfält — "
                     "ikonen visas inte (isSupported())",
                     name.c_str());
        }
        return;
    }
    state_->sync(*this);
}

void TrayIcon::hide()
{
    const bool varSynlig = visible_;
    visible_             = false;
    state_->hovered      = false;
    state_->anim.shown   = false;
    state_->skickat.reset();
    state_->syncTimer(*this);   // timern av
    // Ur OS:ets fält. Idempotent — och en no-op där ingen ikon kan stå
    // (Linux, id 0) eller där backenden redan tagit bort en vägrad.
    if (varSynlig)
        gfx::removeTrayIcon(state_->id);
    // SIST: en kedja vars rotfönster redan är borta avslutas synkront och
    // kör appens `onClose`.
    closeMenu();
}

// ---------------------------------------------------------------- bilden

// Vakten är bildvärdets likhet: samma Bitmap i en ny version är en ändring,
// och det är så ändrade pixlar når fältet.
void TrayIcon::setIcon(ImageSource img)
{
    if (icon_ == img)
        return;
    icon_ = std::move(img);
    state_->sync(*this);
}

void TrayIcon::setSmallIcon(ImageSource img)
{
    if (small_ == img)
        return;
    small_ = std::move(img);
    state_->sync(*this);
}

void TrayIcon::setTemplateImage(bool on)
{
    if (template_ == on)
        return;
    template_ = on;
    state_->sync(*this);
}

void TrayIcon::setToolTip(TrText t)
{
    if (toolTip_ == t)
        return;   // samma TOKEN — samma text i varje språk
    toolTip_ = std::move(t);
    state_->sync(*this);
}

// ------------------------------------------------------------ animationen

void TrayIcon::addFrame(ImageSource img)
{
    frames_.push_back(std::move(img));
    state_->anim.setFrameCount(frameCount());
    state_->sync(*this);
}

void TrayIcon::setFrame(int index, ImageSource img)
{
    if (index < 0 || std::size_t(index) >= frames_.size()) {
        gfx::log("TrayIcon(%s): setFrame(%d) utanför intervallet (%zu bilder) "
                 "— ignoreras", name.c_str(), index, frames_.size());
        return;
    }
    frames_[std::size_t(index)] = std::move(img);
    state_->sync(*this);
}

void TrayIcon::setFrames(std::vector<ImageSource> frames)
{
    frames_ = std::move(frames);
    state_->anim.setFrameCount(frameCount());
    state_->sync(*this);
}

ImageSource TrayIcon::frame(int index) const
{
    if (index < 0 || std::size_t(index) >= frames_.size()) {
        gfx::log("TrayIcon(%s): frame(%d) utanför intervallet (%zu bilder)",
                 name.c_str(), index, frames_.size());
        return ImageSource{};
    }
    return frames_[std::size_t(index)];
}

void TrayIcon::setInterval(int ms)
{
    if (ms < kMinIntervalMs) {
        gfx::log("TrayIcon(%s): setInterval(%d) under %d ms — klampas",
                 name.c_str(), ms, kMinIntervalMs);
        ms = kMinIntervalMs;
    }
    intervalMs_ = ms;
    state_->sync(*this);   // en gående timer får perioden från nästa bild
}

void TrayIcon::startAnimation()
{
    state_->anim.started = true;
    state_->sync(*this);
}

void TrayIcon::stopAnimation()
{
    state_->anim.stop();
    state_->sync(*this);
}

bool TrayIcon::isAnimating() const
{
    return state_->anim.started;   // BEGÄRAN — se headern
}

// ------------------------------------------------------ DEN PLATTA FORMEN
// `AppIcon`s vokabulär (hård regel 8) med den TOMMA stigen som roten och
// utan genväg — hela stigen stiger ned från menyns egna poster.

void TrayIcon::addItem(const MenuPath& path, TrText text)
{
    detail::addMenuItem(detail::descendMenu(menu_.items, path, 0, kVem), kVem,
                        path, std::move(text), std::string());
}

void TrayIcon::addCheck(const MenuPath& path, TrText text, bool checked)
{
    detail::addMenuCheck(detail::descendMenu(menu_.items, path, 0, kVem), kVem,
                         path, std::move(text), checked);
}

void TrayIcon::addRadio(const MenuPath& path, TrText text, bool checked)
{
    detail::addMenuRadio(detail::descendMenu(menu_.items, path, 0, kVem), kVem,
                         path, std::move(text), checked);
}

void TrayIcon::addSeparator(const MenuPath& path)
{
    detail::addMenuSeparator(detail::descendMenu(menu_.items, path, 0, kVem),
                             kVem, path);
}

void TrayIcon::addSubmenu(const MenuPath& path, TrText text)
{
    detail::addMenuSubmenu(detail::descendMenu(menu_.items, path, 0, kVem),
                           kVem, path, std::move(text));
}

MenuItem* TrayIcon::itemAtPath(const MenuPath& path)
{
    return detail::itemAtRootPath(menu_.items, path, kVem);
}

// MENYPOSTENS TILLSTÅND — den delade implementationen i src/menu_path.h,
// med den tomma stigen som roten (`itemAtPath`).
void TrayIcon::setItemIcon(const MenuPath& path, ImageSource icon)
{
    detail::setMenuItemIcon(itemAtPath(path), kVem, path, std::move(icon));
}

void TrayIcon::setItemEnabled(const MenuPath& path, bool on)
{
    detail::setMenuItemEnabled(itemAtPath(path), kVem, path, on);
}

void TrayIcon::setItemChecked(const MenuPath& path, bool on)
{
    detail::setMenuItemChecked(itemAtPath(path), kVem, path, on);
}

void TrayIcon::logMissingPath(const char* fn, const MenuPath& path)
{
    detail::logMissingMenuPath(kVem, fn, path);
}

// ------------------------------------------------------------------ menyn

// Vid ikonen, i ett eget värdfönster — och i pump-svansen, efter att värden
// tagit fokus (punkt 3 överst). Tills dess svarar `isMenuOpen()` true, och
// `closeMenu()` avbryter.
void TrayIcon::openMenu()
{
    if (!visible_ || menu_.items.empty() || !isSupported())
        return;
    state_->openMenu(*this);
}

void TrayIcon::closeMenu()
{
    state_->cancelPending();
    menu_.close();   // uppskjutet; en meny som inte visas är en no-op
}

bool TrayIcon::isMenuOpen() const
{
    return menu_.isOpen() || state_->menyVäntar;
}

// ------------------------------------------------- App:s ingångar (registret)

namespace detail {

void trayDispatch(const gfx::Event& e, AppWindow* modalTop)
{
    TrayState::dispatch(e, modalTop);
}

void trayLanguageChanged()
{
    TrayState::languageChanged();
}

std::uint32_t trayIdOf(const TrayIcon& t)
{
    return TrayState::idOf(t);
}

} // namespace detail

} // namespace glaze
