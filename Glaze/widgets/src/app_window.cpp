#include "glaze/glaze.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "glaze/app.h"
#include "canvas/resources.h"   // CanvasResources komplett för canvas_
#include "drag_scroll.h"   // autoskrollens timer-id
#include "font_set.h"      // FontSet komplett för fontSet_
#include "image_cache.h"
#include "profiler_hooks.h"   // profileraren — innehållet, overlayen, skadan
#include "tooltip.h"   // ToolTipWindow komplett för tipWin_
#include "window_icon.h"   // programikonens ramar
#include "window_placement.h"   // startplatsen och logisk ↔ placering

namespace glaze {
namespace {

// ---- damage-parametrar -------------------------------------------------
// Trösklarna där partiell omritning kollapsar till full: många små rects
// kostar mer administration än de sparar, och stora rects närmar sig ändå
// hela ytan. Värsta fallet = att rita allt.
constexpr std::size_t kMaxDamageRects = 8;
constexpr float       kMaxDamageArea  = 0.6f;   // andel av innehållsytan

// ---- onResize-kedjespärren --------------------
// Så många leveransomgångar i följd med en körd handler och ingen riktig
// inmatning emellan levereras innan spärren slår till. 64 och inte färre:
// dockningens glidning (`dockAnimMs` 160) ändrar storlekar utan händelser i
// ~10 bildrutor vid 60 Hz och ~38 vid 240 Hz.
constexpr int kResizeChainMax = 64;

// ---- caret-timern ------------------------------------------------------
// AppWindow äger caret-blinket: timer i
// fasaden i stället för setAnimating — appen SOVER mellan blinkningarna
// (2 Hz-blink ska inte kosta 60 Hz omritning). Höga timer-id:n reserveras
// för ramverket; appars egna Window::startTimer-id ska ligga under.
constexpr std::uint32_t kCaretTimerId = 0x80000001;
constexpr std::uint32_t kCaretBlinkMs = 500;

// Debug-markörernas livslängd (uttoningen) och färgcykel. Färgerna är
// avsiktligt HÅRDKODADE (undantag från temaregeln): diagnostiklagret ska
// synas oavsett tema — samma resonemang som Chromes "paint flashing".
constexpr std::uint64_t kDamageFadeMs = 800;
constexpr gfx::Color    kDamageColors[] = {
    {255, 205, 0, 255},    // gul
    {255, 120, 0, 255},    // orange
    {255, 0, 200, 255},    // magenta
};
constexpr int kDamageColorCount =
    static_cast<int>(sizeof(kDamageColors) / sizeof(kDamageColors[0]));

// Runda UTÅT till hela pixlar. Damage-rects hålls pixel-exakta så
// bakgrundsfyllningen täcker exakt det klippet släpper igenom — inga
// AA-sömmar i skarven mellan omritat och behållet innehåll.
gfx::Rectf snapOut(const gfx::Rectf& r)
{
    const float x1 = std::floor(r.x);
    const float y1 = std::floor(r.y);
    const float x2 = std::ceil(r.x + r.w);
    const float y2 = std::ceil(r.y + r.h);
    return gfx::Rectf{x1, y1, x2 - x1, y2 - y1};
}

// Omslutande union — flytt/resize skadar både gammal och ny yta.
gfx::Rectf unionRect(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x1 = std::min(a.x, b.x);
    const float y1 = std::min(a.y, b.y);
    const float x2 = std::max(a.x + a.w, b.x + b.w);
    const float y2 = std::max(a.y + a.h, b.y + b.h);
    return gfx::Rectf{x1, y1, x2 - x1, y2 - y1};
}

gfx::Rectf intersectRect(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    const float x2 = std::min(a.x + a.w, b.x + b.w);
    const float y2 = std::min(a.y + a.h, b.y + b.h);
    return gfx::Rectf{x1, y1, std::max(0.0f, x2 - x1),
                      std::max(0.0f, y2 - y1)};
}

bool overlaps(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

// DEN TOMMA REKTEN: en widget utan yta ritar inte SIG SJÄLV —
// varken `draw`, `drawAfterChildren` eller `drawOverlay` (kontraktet i
// widget.h). Annars centrerade en dold överflödsknapp sin ikon kring
// verktygsfältets hörn, och en infälld panel sina rubrikknappar kring
// värdens. Barnen vandras ändå, och bokföringen är orörd.
bool hasArea(const gfx::Rectf& r)
{
    return r.w > 0.0f && r.h > 0.0f;
}

// Temats chrome-roller → fasadens ChromeColors. Chromet ritas av
// gfx-backenden, som inte känner till Theme — färgerna trycks ner per
// repaint (samma resolve-rytm som widgetarna, så titelraden följer med).
gfx::ChromeColors chromeColorsFrom(const Theme::Colors& c)
{
    gfx::ChromeColors cc;
    cc.titlebar    = c.titlebar;
    cc.titleText   = c.titlebarText;
    cc.buttonHover = c.chromeHover;
    cc.buttonPress = c.chromePress;
    cc.closeHover  = c.chromeClose;
    cc.glyph       = c.chromeGlyph;
    cc.closeGlyph  = c.chromeCloseGlyph;
    cc.frame       = c.frame;
    return cc;
}

// Resize-greppens mått (logiska px), emitterade i hit-test-kartan. ZONERNA
// LIGGER INNANFÖR KLIENTYTAN och läses bara av Linux, och där bara när
// fönstret saknar en storleksmarginal utanför ytan (X11 utan kompositör eller
// utan WM-stöd för `_GTK_FRAME_EXTENTS`, ett tiled Wayland-fönster): D3D11
// och Metal har OS:ets ram UTANFÖR den synliga kanten och hoppar över dem,
// och Linux gör detsamma när marginalen finns —
// annars tog de de yttersta px av allt innehåll vid kanten.
constexpr float kResizeBorder = 6.0f;
constexpr float kCornerGrip   = 12.0f;

// Chrome-ctx:t: samma Ctx men arean = HELA klient-
// ytan (innehållsytan + titelremsan). TopBar placeras mot den — innehålls-
// barnen behåller contentArea (captionen är non-client à la WinForms, så
// ankrade/dockade app-widgets påverkas inte av att namnlisten är widget).
Ctx chromeCtx(Ctx c)
{
    c.area = gfx::Rectf{0.0f, 0.0f, c.area.x + c.area.w,
                        c.area.y + c.area.h};
    return c;
}

// NUMPADENS TVÅ LÄGEN ÄR EN FYSISK TANGENT (tabellen vid `enum class Key` i
// gfx/events.h): siffran med NumLock på, navigationsrollen med den av.
// Svarar med tangentens post i det ANDRA läget, eller `Unknown` för en
// tangent som bara har ett. `isKeyHeld` släpper båda vid ett släpp: Windows
// låter Shift vända NumLock, så ett tryck kan börja som `KP_Up` och släppas
// som `KP_N8`.
gfx::Key numpadTwin(gfx::Key k)
{
    using K = gfx::Key;
    switch (k) {
    case K::KP_N0:       return K::KP_Insert;
    case K::KP_N1:       return K::KP_End;
    case K::KP_N2:       return K::KP_Down;
    case K::KP_N3:       return K::KP_PageDown;
    case K::KP_N4:       return K::KP_Left;
    case K::KP_N5:       return K::KP_Begin;
    case K::KP_N6:       return K::KP_Right;
    case K::KP_N7:       return K::KP_Home;
    case K::KP_N8:       return K::KP_Up;
    case K::KP_N9:       return K::KP_PageUp;
    case K::KP_Decimal:  return K::KP_Delete;
    case K::KP_Insert:   return K::KP_N0;
    case K::KP_End:      return K::KP_N1;
    case K::KP_Down:     return K::KP_N2;
    case K::KP_PageDown: return K::KP_N3;
    case K::KP_Left:     return K::KP_N4;
    case K::KP_Begin:    return K::KP_N5;
    case K::KP_Right:    return K::KP_N6;
    case K::KP_Home:     return K::KP_N7;
    case K::KP_Up:       return K::KP_N8;
    case K::KP_PageUp:   return K::KP_N9;
    case K::KP_Delete:   return K::KP_Decimal;
    default:             return K::Unknown;
    }
}

// Knappens bit i `buttonsDown_` — None har ingen.
std::uint8_t mouseBit(gfx::MouseButton b)
{
    return b == gfx::MouseButton::None
               ? std::uint8_t{0}
               : static_cast<std::uint8_t>(1u << static_cast<unsigned>(b));
}

// NAMNLISTENS MOTVIKT — den fasta luckan i motsatt ände
// från fönsterknapparna, som håller titeln centrerad i HELA baren (se
// buildTopBar). Den är lika bred som klustret plus kantluften — eller som
// programikonens plats om den är bredare: ikonen sitter i just den änden,
// och en smalare motvikt hade lagt titeln över den (då förskjuts titeln i
// stället). Måtten läses ur den AKTIVA stilen vid varje mätning.
// Fortfarande en `Spacer`, så `pushHitRegions` ser en lucka = dragyta —
// ikonen är ett eget rotbarn med Client-roll före barens Caption.
class CaptionBalance final : public Spacer {
public:
    CaptionBalance() : Spacer(0.0f) {}

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        const float pad     = 0.5f * (c.style->barHeight - c.style->capsuleH);
        const float cluster = 3.0f * c.style->capsuleSeg;
        const AppWindow* w  = appWindow();
        const float ikon =
            w ? w->appIcon.reservedWidth(c, AppIcon::Row::Title) : 0.0f;
        return gfx::Sizef{std::max(pad + cluster, ikon), height};
    }
};

// DE ANPASSADE AXLARNA — en regel för mätningen,
// anpassningen och ritningens fråga.
bool fitsWidth(SizeToContent s)
{
    return s == SizeToContent::Width || s == SizeToContent::WidthAndHeight;
}

bool fitsHeight(SizeToContent s)
{
    return s == SizeToContent::Height || s == SizeToContent::WidthAndHeight;
}

} // namespace

AppWindow::AppWindow(TrText title, int width, int height,
                     gfx::Chrome chrome)
    : title_(std::move(title)), initW_(width), initH_(height), chrome_(chrome),
      win_(gfx::createWindow())   // tvåfas: OS-fönstret öppnas först i show()
{
    // Rot-panelen är trädets ankare: osynlig, oklippande, ritas aldrig —
    // dess barn resolvar direkt mot innehållsytan. Den bär fönster-
    // kopplingen som adopt() propagerar till allt som läggs till.
    root_.setClip(false);
    root_.setOwnerForTree(this);

    // Eget chrome = riktig namnlist som widget.
    if (chrome_ == gfx::Chrome::Custom)
        buildTopBar();

    // STATUSRADEN: DOLD och LÅNAD, adopterad direkt
    // efter TopBar-bygget. En dold widget skär ingen remsa ur
    // innehållsytan, och ordningen i trädet är DETERMINISTISK och beror inte
    // på när `statusBar()` först anropas.
    //
    // `hide()` FÖRE `adopt()`: settern invalidaterar vid ändring, och en
    // widget utan förälder har inget fönster att invalidatera — ordningen
    // gör anropet trivialt no-op-säkert i stället för beroende av att
    // invalidate() klarar en lös widget.
    // MENYRADEN följer statusradens mönster exakt, och
    // adopteras FÖRE den: barnordningen ÄR dock-carvingens ordning, och
    // menyraden hör till TOPPEN av innehållsytan. `hide()` FÖRE `adopt()`
    // av samma skäl som statusraden — settern invaliderar, och en widget
    // utan förälder har inget fönster att invalidatera.
    //
    // NAMNEN sätts HÄR och inte i
    // `.glazeui`: de två är INBYGGDA fönstermedlemmar utan medlemsrad, så
    // writern får inte emittera en `name`-rad för dem — den hade skuggat
    // basklassens fält och inte kompilerat. Utan tilldelningen här bär de
    // TOMT namn i drift, och då kan ett handkodat `anchorTop = {"menuBar",
    // 8}` ALDRIG träffa — trots att de är just de tidigare syskon man
    // oftast vill ankra mot. Namnen är samma strängar grammatiken använder
    // (`bld::builtinMember`), så designervägen och kodvägen ger identiskt
    // resultat (hård regel 8).
    menuBar.name = "menuBar";
    menuBar.hide();
    root_.adopt(menuBar);

    statusBar.name = "statusBar";
    statusBar.hide();
    root_.adopt(statusBar);

    // PROGRAMIKONEN — SIST, och SYNLIG: varje fönster har
    // en ikon. Den hör till CHROME-trädet som TopBar: placerad mot hela
    // klientytan (`isChromeRoot`) och UTANFÖR dock-flödet av TopBars skäl —
    // annars hade `menuBar.show()` skurit en remsa ur dess yta och tryckt
    // ner den en menyrad. Hörnet följer plattformen: vänster på Windows och
    // Linux, HÖGER på macOS, där fönsterknapparna har vänstra hörnet. Namnet
    // av menuBar-skälet ovan — en inbyggd medlem utan `name`-rad i
    // `.glazeui`.
    appIcon.name        = "appIcon";
    appIcon.inDockFlow_ = false;
    if (AppIcon::onRightSide()) {
        appIcon.anchors.left  = false;
        appIcon.anchors.right = true;
    }
    root_.adopt(appIcon);
}

// TopBar-defaulterna: plattformsvalet görs HÄR vid konstruktion — inte i
// flexmotorn. FORMEN är densamma på alla tre plattformarna — kapsel-
// klustret — och plattformen väljer SIDAN (Mac har klustret till vänster,
// Windows och Linux till höger) och ORDNINGEN (se längre ner).
//
// TITELN CENTRERAS I HELA BAREN, inte i det lediga utrymmet. Flexen kan
// bara dela luckan mellan spacers, så centreringen görs med en MOTVIKT:
// en fast spacer lika bred som klustret läggs i motsatt ände, och två
// växande spacers omger titeln. Lika tunga sidor ⇒ titeln hamnar mitt i
// baren, utan att flexmotorn behöver ett centreringsbegrepp.
//
// Kopplingen följer CaptionButton-mönstret (växling via isMaximized());
// restoreGlyph speglas per repaint i renderContent.
void AppWindow::buildTopBar()
{
    auto tb = std::make_unique<TopBar>();
    topBar_ = tb.get();
    // UTANFÖR DOCK-FLÖDET — och det gäller DEN HÄR INSTANSEN, inte typen
    // `TopBar`. Namnlisten resolvas mot chrome-Ctx:t =
    // HELA klientytan, medan menyrad och statusrad skär i INNEHÅLLSytan.
    // Utan haken tryckte `menuBar.show()` ner namnlisten med en
    // menyradshöjd — en widget placerad i en area som inte är dess egen.
    // En vanlig nästlad TopBar (galleriets grupp 8) är ett helt vanligt
    // barn och SKA skäras av sina dockade syskon.
    topBar_->inDockFlow_ = false;
    root_.adopt(std::move(tb));

    using CB = CaptionButton;

    // Kantluften är HÄRLEDD, inte vald: kapseln ska ha samma avstånd till
    // fönsterkanten som till barens över- och underkant, så att dess
    // rundade ände och fönstrets hörn blir KONCENTRISKA (se windowRadius i
    // style.cpp). Motviktens bredd (luft + kluster, eller ikonens plats om
    // den är större) räknas av `CaptionBalance` vid varje mätning.
    const Style& st  = App::themeState().style;
    const float  pad = 0.5f * (st.barHeight - st.capsuleH);

    auto gap = [this](float w) {
        auto* s  = topBar_->add<Spacer>(0.0f);
        s->width = w;
    };
    auto title = [this] {
        topBar_->addSpacer();
        topBar_->titleLabel_      = topBar_->add<Label>(title_);
        topBar_->titleLabel_->setRole(Label::Role::Titlebar);
        topBar_->addSpacer();
    };
    // ORDNINGEN följer PLATTFORMEN — till skillnad från FORMEN, som är vår
    // och lika överallt (kapseln). macOS läser stäng · minimera ·
    // maximera från vänster, och klustret sitter just till vänster där.
    // Windows/Linux behåller minimera · maximera · stäng, för deras kluster
    // sitter till HÖGER och stängknappen hör till ytterkanten på båda
    // sidor — det är den gemensamma nämnaren, inte ordningen i sig.
    //
    // Rundningen följer med gratis: `CaptionButton::clusterPos()` räknar
    // sin plats bland SYNLIGA syskon, så adopt-ordningen ÄR ordningen och
    // ändarna rundas rätt utan att någon säger till.
    auto buttons = [this] {
        auto add = [this](CB::Kind k) {
            return topBar_->add<CB>(k, CB::Shape::Capsule);
        };
#ifdef __APPLE__
        topBar_->closeButton_ = add(CB::Kind::Close);
        topBar_->minButton_   = add(CB::Kind::Minimize);
        topBar_->maxButton_   = add(CB::Kind::Maximize);
#else
        topBar_->minButton_   = add(CB::Kind::Minimize);
        topBar_->maxButton_   = add(CB::Kind::Maximize);
        topBar_->closeButton_ = add(CB::Kind::Close);
#endif
    };

    // MOTVIKTEN i motsatt ände från klustret — där programikonen sitter —
    // är en `CaptionBalance` (klustret + kantluften, eller ikonens plats
    // om den är bredare).
    auto balance = [this] { topBar_->add<CaptionBalance>(); };

#ifdef __APPLE__
    gap(pad);
    buttons();
    title();
    balance();
#else
    balance();
    title();
    buttons();
    gap(pad);
#endif

    topBar_->minButton_->onClick = [this](Widget*, ClickArgs&) { minimize(); };
    topBar_->maxButton_->onClick = [this](Widget*, ClickArgs&) {
        isMaximized() ? restore() : maximize();
    };
    topBar_->closeButton_->onClick = [this](Widget*, ClickArgs&) { close(); };
}

void AppWindow::setTitle(TrText t)
{
    title_ = std::move(t);
    repushTitle();
}

// EN väg ner till OS-fönstret och TopBar, delad av `setTitle` och av
// språkbytets repush. Två vägar kan glida isär så att titeln tappas i
// den ena.
void AppWindow::repushTitle()
{
    win_->setTitle(title_.str());
    if (topBar_ && topBar_->titleLabel()) {
        topBar_->titleLabel()->setText(title_);   // settern invaliderar
    }
}

// .NET-semantiken: controlBox av släcker alla tre; min/max styr styckvis.
// Dolda knappar tar ingen plats i flexen — resten packas om automatiskt.
void AppWindow::setControlBox(bool on)
{
    controlBox_ = on;
    applyCaptionBoxes();
}

void AppWindow::setMinimizeBox(bool on)
{
    minimizeBox_ = on;
    applyCaptionBoxes();
}

void AppWindow::setMaximizeBox(bool on)
{
    maximizeBox_ = on;
    applyCaptionBoxes();
}

void AppWindow::applyCaptionBoxes()
{
    if (!topBar_)
        return;
    auto apply = [](CaptionButton* b, bool on) {
        if (b)
            on ? b->show() : b->hide();
    };
    apply(topBar_->minButton(), controlBox_ && minimizeBox_);
    // `isFixedSize()` är EN sanning som ALLT chrome läser (window.h).
    // `pushHitRegions` och knapparna läser samma. Ett låst fönster visar därför
    // ingen maximera-knapp som ser tryckbar ut och inte kan göra något:
    // chromet lovar inget OS:et vägrar, precis
    // samma fel som `setMaximizeBox` finns för att undvika.
    apply(topBar_->maxButton(), controlBox_ && maximizeBox_ && !isFixedSize());
    apply(topBar_->closeButton(), controlBox_);
}

// ------------------------------------------------ RAMEN

// Ett ATTRIBUT (som `setShowInTaskbar`): ramen väljs när
// OS-fönstret skapas, och att byta den i ett öppet fönster hade krävt att det
// skapades om. Ett öppet fönster loggar och tar värdet vid nästa öppning.
void AppWindow::setChrome(gfx::Chrome c)
{
    if (open_) {
        if (c != chrome_)
            gfx::log("AppWindow::setChrome: \"%s\" är redan öppet — gäller "
                     "nästa öppning",
                     title_.str().c_str());
        nextChrome_ = c;
        return;
    }
    nextChrome_.reset();
    applyChrome(c);
}

// NAMNLISTEN BYGGS LATT — bara när ett fönster som konstruerades utan
// Custom väljer det, så ett fönster utan namnlist slipper den (varje
// meny, tooltip och dropdown skulle annars bära en dold namnlist). Den läggs
// FÖRST i rotordningen, där konstruktorn lägger den: programikonen står då
// efter den — överst i z och först i träffen, över namnlisten — och trädet är
// det ett fönster konstruerat med Custom har. Inte `unadopt` + `adopt` av
// ikonen: den är en INBYGGD medlem, och `unadopt` nekar dem per kontrakt
// (`isBuiltinMember`).
void AppWindow::applyChrome(gfx::Chrome c)
{
    chrome_ = c;
    if (c == gfx::Chrome::Custom && !topBar_) {
        buildTopBar();   // adopterar baren SIST …
        auto& barn = root_.children_;
        barn.erase(std::find(barn.begin(), barn.end(),
                             static_cast<Widget*>(topBar_)));
        barn.insert(barn.begin(), topBar_);   // … och här FÖRST
    }
    syncTopBar();
    applyCaptionBoxes();
    applyContentSize();   // chromets höjd ändrades — en innehållsstorlek följer
    invalidate();
}

// EN REGEL för namnlistens synlighet: eget chrome och inte genomskinligt
// (fri geometri och en namnlist är oförenliga). En dold bar skär ingen
// remsa, så `chromeInsetH` och programikonens plats följer med.
void AppWindow::syncTopBar()
{
    if (!topBar_)
        return;
    if (chrome_ == gfx::Chrome::Custom && !transparent_)
        topBar_->show();
    else
        topBar_->hide();
}

// Fönstrets hörnradie i FYSISKA px — 0 när fönstret inte är rundat eller
// är maximerat. EN sanning: bakgrundsritningen och TopBars övre hörn läser
// samma metod, annars kan de glida isär med en halv pixel och lämna en
// ljus hårlinje i hörnet.
float AppWindow::cornerRadius() const
{
    if (!roundedChrome_ || isMaximized())
        return 0.0f;
    return App::themeState().style.windowRadius * win_->uiScale();
}

Ctx AppWindow::makeCtx()
{
    // Alla TRE axlarna resolvas HÄR, varje gång, ur det APP-GLOBALA state:t
    // — det är så System-läget följer OS:ets ljus/mörkt (och ett byte av
    // ritning/tema/mått slår igenom i ALLA fönster) vid nästa repaint utan
    // omstart eller polling. `ts.style` är ett VÄRDE och alltid giltigt —
    // ingen nullptr-gren att glömma.
    auto& ts = App::themeState();
    return Ctx{&win_->renderer(), font_.get(), headingFont_.get(),
               win_->uiScale(), win_->contentArea(),
               &ts.theme.resolve(ts.mode), ts.ui, &ts.style, images_.get(),
               display_.pxPerMm, canvas_.get(), fontSet_.get()};
}

// SKÄRMENS mm: backendens mätning med appens
// kalibrering ovanpå. Ett ANTAGET mått loggas EN gång per skärm-id och
// process — orsaken och vägen ut (kalibreringen) i samma rad; ett fönster
// som flyttar fram och tillbaka mellan två skärmar ska inte fylla loggen.
void AppWindow::measureDisplay()
{
    gfx::DisplayMetrics m = gfx::effectiveDisplay(win_->displayMetrics());
    const bool ändrad = m.pxPerMm != display_.pxPerMm ||
                        m.source != display_.source || m.id != display_.id;
    display_ = std::move(m);
    static std::set<std::string> loggade;
    if (display_.source == gfx::DisplaySource::Assumed &&
        loggade.insert(display_.id).second)
        gfx::log("AppWindow(%s): skärmen '%s' är inte mätt — %s. %.2f px/mm "
                 "antas (96 dpi); kalibrera med "
                 "App::setDisplayCalibration", title_.str().c_str(),
                 display_.id.c_str(), display_.reason.c_str(),
                 double(display_.pxPerMm));
    if (ändrad)
        invalidate();
}

// Utseende-API:t — app-globalt: läser/skriver App::themeState och ritar
// om ALLA öppna fönster. Vilken fönster-instans som anropas kvittar.

// Byte av RITNING återställer tema och mått till familjens defaults
//: en palett grindad mot ett recept
// är inte grindad mot ett annat, och en halv kombination är ingen design.
// Vill man blanda gör man det explicit efteråt.
void AppWindow::setUI(UI u)
{
    auto& ts = App::themeState();
    ts.ui    = u;
    ts.theme = defaultTheme(u);
    ts.style = defaultStyle(u);
    App::reloadFontsAll();       // nya måtten kan bära en annan fontstorlek
    App::applyContentSizeAll();  // …och en annan chrome-höjd
    App::invalidateAll();
}

UI AppWindow::ui() const
{
    return App::themeState().ui;
}

void AppWindow::setStyle(const Style& s)
{
    App::themeState().style = s;   // KOPIA — se ThemeState i app.h
    // Måtten äger UI-fontens storlek: utan omladdning ritas de nya måtten
    // med gamla fontens radhöjd och varje autostorlek blir fel. No-op när
    // storleken är oförändrad (reloadFontsAll jämför).
    App::reloadFontsAll();
    // Chromets höjd är också stilmått (`barHeight`/`statusBarH`), så en
    // begärd INNEHÅLLSSTORLEK måste räknas om — annars gäller löftet
    // bara fram till första stilbytet.
    App::applyContentSizeAll();
    App::invalidateAll();
}

const Style& AppWindow::style() const
{
    const auto& ts = App::themeState();
    return ts.style;
}

void AppWindow::setColorsLight(const Theme::Colors& c)
{
    App::themeState().theme.light.colors = c;
    App::invalidateAll();
}

void AppWindow::setColorsDark(const Theme::Colors& c)
{
    App::themeState().theme.dark.colors = c;
    App::invalidateAll();
}

void AppWindow::setTheme(const Theme& t)
{
    App::themeState().theme = t;
    App::invalidateAll();
}

const Theme& AppWindow::theme() const
{
    return App::themeState().theme;
}

void AppWindow::setThemeMode(ThemeMode m)
{
    App::themeState().mode = m;
    App::invalidateAll();
}

ThemeMode AppWindow::themeMode() const
{
    return App::themeState().mode;
}

// Säkerhetsnät: ett fönster som fortfarande är öppet när C++-objektet dör
// (objektet ska normalt överleva sin stängning — användaren äger det) rivs
// direkt så App-registret inte pekar på ett lik.
AppWindow::~AppWindow()
{
    if (open_ && App::instance_)
        App::instance_->executeClose(this);
}

void AppWindow::show()
{
    showOwned(nullptr);
}

void AppWindow::show(AppWindow& owner)
{
    showOwned(&owner);
}

// Init-blocket: öppna OS-fönstret, ladda fonten, koppla
// callbacken, registrera i App (singletonen skapas lat här — första
// fönstret drar igång plattformen). Pumpen bor i App — show är
// icke-blockerande.
void AppWindow::showOwned(AppWindow* owner)
{
    if (open_) {
        // I `onLoad` före den första visningen:
        // fönstret visas när handlern är klar — `show()` där ångrar
        // bara ett `hide()` (eller en dold start).
        if (loading_ && firstShowPending_) {
            showAfterLoad_ = true;
            return;
        }
        if (hidden_) {
            revealHidden();   // dolt fönster: ta fram det (fyrar onShow)
            return;
        }
        if (!pendingClose_) {
            win_->raise();   // redan öppet — lyft det i stället
            noteLoadActivation(false);
        }
        return;
    }
    if (showing_) {
        // Reentrant show under pågående show. Kan nås via popup-stängningens
        // onClose nedan: `open_` är fortfarande falskt så vakten ovan
        // släpper igenom, och utan den här raden hade fönstret initierats
        // TVÅ gånger (en gång inifrån handlern, en gång när vi återvänder).
        gfx::log("AppWindow::show: anropad inifrån sin egen show — ignoreras");
        return;
    }
    // Vakten släpps UTTRYCKLIGEN före `onLoad` (nedan): fönstret är då öppet,
    // så `open_`-vakten tar ett reentrant `show()`, och en handler som river
    // fönstret får inte lämna en vakt som skriver i det döda objektet.
    struct ShowGuard {
        bool* flag;
        ~ShowGuard()
        {
            if (flag)
                *flag = false;
        }
    } showGuard{&showing_};
    showing_ = true;

    App* app = &glaze::app();
    if (!app->ok_) {
        gfx::log("AppWindow::show: plattformen kunde inte initieras");
        return;
    }

    // POPUP-ORDNINGEN ÄR ETT PROTOKOLLKRAV, inte en stilfråga: xdg-shell
    // tillåter bara att den ÖVERSTA popupen förstörs
    // (`xdg_wm_base.not_the_topmost_popup`), så en ny GRABBANDE popup får
    // aldrig skapas medan en gammal ännu lever. Stängningen är annars
    // UPPSKJUTEN till pump-svansen medan öppningen sker SYNKRONT i
    // dispatch — klickar man dropdown B medan A är öppen gick alltså B:s
    // grab ut med A kvar, och A revs därefter fast B fanns. Verkställ den
    // uppskjutna stängningen här i stället, före OS-fönstret.
    //
    // ANKARET ÄR `owner`, inte `this`: vår egen `owner_` sätts först långt
    // nedanför (efter init), så `this` hade haft en TOM ägarkedja — och en
    // nästlad popup (undermeny i en meny) hade då stängt sin egen förälder.
    // `owner` bär kedjan som ska överleva; vi själva är ännu inte öppna och
    // hoppas över ändå.
    if (popup_)
        app->closePopupsNow(owner);

    // RAMEN som valdes medan fönstret var öppet gäller
    // från den här öppningen — före innehållsstorleken, för namnlisten är
    // chrome och dess höjd räknas in där.
    if (nextChrome_) {
        applyChrome(*nextChrome_);
        nextChrome_.reset();
    }

    // INNEHÅLLSSTORLEKEN löses HÄR och inte där appen bad om
    // den: chromet är färdigbyggt först nu — genererad kod emitterar
    // `setClientSize`/`setContentSize` FÖRST och `statusBar.show()` senare
    // i samma initializeComponent.
    applyContentSize();

    gfx::WindowDesc desc;
    desc.title  = title_.str();
    desc.width  = initW_;
    desc.height = initH_;
    desc.posX   = initX_;   // default = kWindowPosDefault (OS:et väljer)
    desc.posY   = initY_;
    // STARTPLATSEN — räknad HÄR ur
    // skärmarna, i placeringsrymden (`startPlacementFor`), och IGEN precis
    // före den första visningen med den storlek `onLoad` gav
    // (`revealHidden`).
    if (const std::optional<gfx::Pointf> p = startPlacementFor(owner)) {
        desc.posX = static_cast<int>(std::lround(p->x));
        desc.posY = static_cast<int>(std::lround(p->y));
    }
    // STARTLÄGET — vid fönstrets FÖRSTA synlighet, i backenden. En popup
    // och ett offscreen-fönster får det aldrig, och en MODAL startar aldrig
    // minimerad: den hade blockerat all input utan att synas (samma fälla
    // som den dolda modalen nedan).
    if (!popup_ && !offscreen_ && isModal_ &&
        startState_ == gfx::WindowState::Minimized)
        gfx::log("AppWindow::show: \"%s\" är modal och startar aldrig "
                 "minimerad — öppnas i normalläge",
                 title_.str().c_str());
    desc.startState = effectiveStartState();
    // STARTGRÄNSERNA: settrarna före show() lagras i
    // AppWindow och måste kopieras HIT — utan de fyra raderna går det att
    // implementera både fasaden och alla fem backendmetoderna utan att en
    // gräns satt i konstruktorn någonsin når OS-fönstret.
    // De EFFEKTIVA gränserna: en axel som följer
    // innehållet är låst — anpassningen efter init sätter de nya.
    desc.minWidth  = effMinW();
    desc.minHeight = effMinH();
    desc.maxWidth  = effMaxW();
    desc.maxHeight = effMaxH();
    // ALLTID ÖVERST — som gränsraderna ovan: utan
    // den här raden når ett attribut satt i konstruktorn ALDRIG
    // OS-fönstret, hur väl fasaden och de sex backendarna än är byggda.
    desc.topmost = topmost_;
    // AKTIVITETSFÄLTET — som raderna ovan.
    desc.showInTaskbar = showInTaskbar_;
    desc.chrome = chrome_;
    if (owner && owner->open_)
        desc.owner = owner->win_.get();   // OS-Z-ordning/minimering gratis
    if (popup_) {
        desc.noActivate = true;   // visas utan att stjäla fokus
        desc.toolWindow = true;   // ingen aktivitetsfälts-post
    }
    // Verktygsfönstret UTAN popupens noActivate — systemfältsmenyns värd
    // (`TrayHostWindow`): ingen knapp och inte i Alt+Tab, men den tar fokus,
    // för det är dess fokusförlust som stänger menyn vid ett klick utanför.
    if (toolWindow_)
        desc.toolWindow = true;
    // AKTIVERINGEN — de flytande dockningsfönstrens
    // interna attribut, samma läxa som raderna ovan: utan kopian når de
    // aldrig OS-fönstret. `noActivate_` för alltid (verktygsfältet),
    // `showInactive_` bara den första visningen (panelen).
    if (noActivate_)
        desc.noActivate = true;
    if (showInactive_)
        desc.showInactive = true;
    if (hasAnchor_ && owner && owner->open_) {
        // Popup-kontraktet: backenden placerar fönstret
        // relativt ankarrectet i ägarens klientyta och äger flip/klamp.
        desc.popupAnchorWindow = owner->win_.get();
        desc.popupAnchorRect   = anchorRect_;
    }
    if (transparent_) {
        desc.transparent = true;
        desc.chrome      = gfx::Chrome::None;   // fri geometri — inget chrome
    }
#if defined(__linux__)
    // RUNDADE FÖNSTER. Hörnen är inte målade utan
    // BORTKLIPPTA, så ytan måste bära per-pixel-alfa — men till skillnad
    // från `setTransparent()` behåller vi chromet: namnlisten är en widget
    // och det är appen som äger hörnen.
    //
    // Bara Linux: macOS rundar sina fönster
    // själv och Windows har ingen rundning ännu. De två backendarna tvingar
    // fortfarande bort chromet för en transparent yta, så begäran får inte
    // gå dit förrän de relaxats — annars tappar fönstret sin namnlist.
    else if (chrome_ == gfx::Chrome::Custom &&
             App::themeState().style.windowRadius > 0.0f) {
        desc.transparent = true;
    }
#endif
    // DOLD START: `hide()` före öppningen skapar
    // OS-fönstret dolt, och `show()` tar fram det senare (`setVisible(true)`
    // — fasaden lovar att ett dolt fönster går att visa i efterhand). ALDRIG
    // för en modal: den hade blockerat all input utan att synas.
    if (hidden_ && isModal_)
        gfx::log("AppWindow::show: \"%s\" är modal och startar aldrig dold — "
                 "hide() före öppningen ignoreras",
                 title_.str().c_str());
    const bool dold = hidden_ && !isModal_;
    // `onLoad` FÖRE SYNLIGHETEN (WinForms
    // `Load`): varje fönster utom popuperna och offscreen-fönstren skapas
    // DOLT, `onLoad` fyras, och fönstret visas sedan (`revealHidden`) — så en
    // plats, ett läge eller en storlek som sätts i `onLoad` gäller från första
    // bildrutan i stället för att hoppa efter att fönstret syns. Den första
    // `setVisible(true)` gör i varje backend det init:s visning hade gjort
    // (startläget, `showInactive`); det är den dolda startens väg. Popuperna
    // visas i init: Waylands `xdg_popup.grab` vill ha tryckets färska
    // serial, och ingen popup sätter geometri i `onLoad`.
    const bool skjutUpp = !offscreen_ && !popup_;
    desc.hidden = offscreen_ || dold || skjutUpp;   // offscreen: visas aldrig
    if (!win_->init(desc))                // loggar själv vid fel
        return;

    // Rundningen bekräftas EFTER init: `desc.transparent` är en BEGÄRAN, och
    // X11 utan ARGB-visual degraderar den till opakt (backenden loggar).
    // Att lita på begäran hade gett SVARTA hörn i stället för genomskinliga
    // — ett bortklippt hörn på en opak yta är ett hål, inte en form.
    roundedChrome_ = desc.transparent && !transparent_ && win_->transparent();

    // PROGRAMIKONEN till OS-fönstret — när det finns.
    // Fasaden hade lagrat den före init också, men ramarna byggs först här,
    // så ett fönster som aldrig visas aldrig avkodar en bild.
    applyWindowIcon();

    // Skärmen fönstret står på — mätt nu när OS-fönstret
    // finns; ritytans mm går genom `makeCtx` härifrån.
    measureDisplay();

    reloadFont();
    // Texturcachen lever parvis med fonten: skapas här, rivs i
    // executeClose FÖRE font_ (bilderna hör till samma renderer).
    images_ = std::make_unique<ImageCache>(win_->renderer());
    // Ritytans resurser — efter bildcachen och rivs före
    // den i executeClose: de hör till samma renderer.
    canvas_ = std::make_unique<CanvasResources>(win_->renderer());
    // Textfonterna — tomma här, laddade lat per variant;
    // rivs i executeClose i fontens block.
    fontSet_ = std::make_unique<FontSet>();

    // Hit-test-kartan + caption-höjden ur trädet FÖRE första framen —
    // annars gäller backendens default tills första repainten.
    pushHitRegions(chromeCtx(makeCtx()));

    // Innehållspasset (→ retained-RT:n): full eller partiell omritning
    // beroende på damage — se renderContent. Kan även anropas direkt av
    // backenden under modal OS-resize.
    win_->setRenderCallback([this](gfx::Window& w) { renderContent(w); });

    // Overlay-lagret ritas efter composite + chrome, direkt i backbuffern
    // (självstädande — se overlay()-kontraktet i widget.h). Ramverkets
    // debug-damage-lager ritas OVANPÅ appens — det ska alltid synas.
    win_->setOverlayCallback([this](gfx::Window&) {
        if (!font_)
            return;   // samma vakt som renderContent
        // Profilerarens overlay — och slutet på en bild som ritats utanför
        // pumpen.
        const detail::WindowPass mät(*this, ProfilePhase::Overlay);
        Ctx c = makeCtx();
        overlay(c);
        // Widgetarnas egna overlay-lager (ritytans
        // gummiband och HUD) efter appens.
        drawWidgetOverlays(c);
        // Drag-återkopplingen OVANPÅ appens overlay — dragbilden
        // ska aldrig hamna under en tooltip appen ritat — och UNDER
        // damage-debuggen, som alltid ska synas.
        drawDragFeedback(c);
        drawDebugDamage(c);
    });

    open_         = true;
    pendingClose_ = false;
    // Dold start: öppet men osynligt, precis som efter `hide()` — ritas inte
    // förrän `show()`. (Ett init som misslyckades ovan lämnar begäran kvar
    // till nästa försök; `App::executeClose` nollar den när fönstret stängs.)
    // Ett fönster som visas EFTER `onLoad` är också dolt tills dess,
    // och fram till den första visningen gäller startvärdena
    // (`firstShowPending_`, B80-2).
    hidden_           = dold || skjutUpp;
    firstShowPending_ = skjutUpp;
    loadActivated_    = {};   // ett ägt fönster ur ett tidigare liv räknas inte
    // Ett nytt liv: inte aktivt förrän OS:et säger det,
    // ingen väntande `onResize`, och varje widget får `first` igen.
    active_ = false;
    resizeQueue_.clear();
    resizeChain_ = 0;
    root_.resizeSeen_.reset();
    forEachWidget(root_, [](Widget& w) { w.resizeSeen_.reset(); });
    // FÖNSTRET FÖLJER INNEHÅLLET — mätt först nu, med fonten
    // (`applyContentSize` ovan körs före init, där ingen font finns
    // och Linux-proxyn saknar renderer), och igen efter `onLoad`
    // (`revealHidden`). Popuperna och offscreen-fönstren (glaze-render) får
    // sin storlek här.
    if (sizeToContent_ != SizeToContent::Manual)
        refit();
    // Ett nytt liv: geometrin läses ur OS-fönstret igen, inte ur kortet från
    // förra stängningen. Ingen känd pekarposition
    // heller — den sätts av den första rörelsen i det nya OS-fönstret.
    closedGeometry_.reset();
    mouseInside_  = false;
    // Hela ytan ritas i första framen — genom `invalidate()`, så att också
    // den här fulla invalideringen stegar `Widget::damageStamp()`.
    invalidate();
    // Ett nytt liv börjar utan drag — ett fönster som stängdes mitt i ett
    // (pumpen levererar inget efter executeClose) får inte vakna med en
    // session som pekar in i ett gammalt tillstånd.
    drag_         = DragSession{};
    // …och utan ett mitten- eller högertryck:
    // släppet kom aldrig till det förra OS-fönstret, och
    // widgeten ska inte få drag av vanliga rörelser i det nya. Inga hållna
    // knappar, inga halva dubbelklick och inget tangentminne heller — släppen
    // gick till det förra fönstret (`App::executeClose` tömde minnet tyst).
    middleWidget_ = nullptr;
    rightWidget_  = nullptr;
    for (AppPress& p : appPress_)   // …och utan appens tryck
        p.end();
    buttonsDown_  = 0;
    for (ClickTrack& t : clicks_)
        t = ClickTrack{};
    keyTargets_.clear();
    // …och utan en hållen styrplattegest: dess End kommer aldrig
    // till det nya OS-fönstret.
    gestureWidget_ = nullptr;
    // …och utan en tooltip som väntar eller visas: timern dog med
    // det förra OS-fönstret, och dess pekare får inte överleva in i nästa.
    tip_          = ToolTipState{};
    if (owner && owner->open_) {
        owner_ = owner;
        owner->owned_.push_back(this);   // kaskaden: barnen före ägaren
    }
    app->windows_.push_back(this);

    // SPRÅKET kan ha bytts medan fönstret var stängt — `App::
    // languageChangedAll` når bara ÖPPNA fönster. Före livscykeln, så att
    // onLoad ser trädet i det språk som gäller (`Widget::languageChanged`).
    languageChangedTree();
    // …och trädet står nu i ett öppet fönster (`Widget::shownChanged`)
    // — också vid en dold start, där frågan svarar dolt.
    root_.notifyShownChanged();

    // Livscykeln: onLoad EN gång per objekt (efter ctor:ns
    // initializeComponent, före första ritningen — pumpen har inte ritat
    // än), sedan onShow vid varje visning. En DOLD start har inte visats:
    // onShow kommer med det `show()` som tar fram fönstret.
    //
    // `onLoad` KÖRS MED FÖNSTRET DOLT: `hide()` där gör starten
    // dold, `show()` ångrar det, `close()` stänger utan att fönstret någonsin
    // syns — och en handler som river fönstret är tillåten: vägen
    // slutar då, utan att röra en medlem. Vakten släpps före handlern (se
    // `ShowGuard`).
    showing_       = false;
    showGuard.flag = nullptr;
    showAfterLoad_ = !dold;
    if (!loadFired_) {
        loadFired_ = true;
        loading_   = true;
        // OS-fönstrets id är LIVETS: en modal pump i handlern kan ha stängt
        // fönstret och öppnat det igen, och det nya livet har då visat sig
        // självt (utan `onLoad` — den fyras en gång per objekt).
        const gfx::WindowId liv   = win_->id();
        const bool          lever = fireLifecycle(onLoad);
        if (!lever)
            return;   // handlern rev fönstret
        loading_ = false;
        if (win_->id() != liv)
            return;
    }
    if (!skjutUpp) {
        // Popuperna och offscreen-fönstren visades i init (eller aldrig) — och
        // en popup som stängdes synkront i sin `onLoad` (en nyare popup
        // öppnades där) har inget att visa.
        if (!dold && open_)
            fireLifecycle(onShow);
        return;
    }
    if (open_ && !pendingClose_ && showAfterLoad_)
        revealHidden();
}

// ETT DOLT FÖNSTER VISAS — `show()` efter `hide()` eller en dold start, och den
// FÖRSTA visningen direkt efter `onLoad`. Den första räknar om
// STARTPLATSEN med den storlek fönstret har nu: `onLoad`
// får ha ändrat både storleken (meddelanderutan mäter sig där) och begäran
// (`setLocation`), och `CenterOwner` ska centrera den storlek som syns. Läget
// har backenden redan (`maximize()` före den första visningen byter
// startläget). Sist `onShow` — dess handler får riva fönstret.
void AppWindow::revealHidden()
{
    if (firstShowPending_) {
        // FÖNSTRET FÖLJER INNEHÅLLET efter `onLoad`
        // —före startplatsen, så `CenterOwner` centrerar den
        // anpassade storleken.
        if (sizeToContent_ != SizeToContent::Manual)
            refit();
        // DEN FÖRSTA `onResize` FÖRE DEN FÖRSTA SYNLIGA BILDRUTAN: en
        // vandring utan ritning med ritningens regler, och leveransen SYNKRONT
        // — samma kontext som `onLoad` (fönstret dolt, ingen frame pågår). En
        // handler får riva eller stänga fönstret; livet och OS-fönstrets id
        // frågas efteråt, som efter `onLoad`. Fasen före den första visningen
        // står kvar under handlern: `hide()` där gör starten dold, och ett
        // vetat `close()` fortsätter hit igen (`App::flushPendingCloses`).
        showAfterLoad_ = true;
        const std::weak_ptr<char> liv = lifeToken();
        const gfx::WindowId       id  = win_->id();
        {
            const Ctx c   = makeCtx();
            const Ctx cch = chromeCtx(c);
            noteSize(root_, c.area, c.scale);
            for (const auto& ch : root_.children())
                noteSizesIn(*ch, isChromeRoot(*ch) ? cch : c);
        }
        deliverResizes();
        if (liv.expired() || !open_ || pendingClose_ || win_->id() != id ||
            !firstShowPending_ || !showAfterLoad_)
            return;
    }
    const bool första = firstShowPending_;
    if (första) {
        firstShowPending_ = false;
        if (const std::optional<gfx::Pointf> p = startPlacementFor(owner_))
            win_->setPosition(static_cast<int>(std::lround(p->x)),
                              static_cast<int>(std::lround(p->y)));
    }
    hidden_ = false;
    win_->setVisible(true);
    noteLoadActivation(första);
    invalidate();
    // Trädet syns igen (`Widget::shownChanged`), och de ägda
    // fönstren får veta (`ownerVisibilityChanged`).
    root_.notifyShownChanged();
    notifyOwnedVisibility(true);
    // DEN FÖRSTA VISNINGEN AKTIVERAR MIG — ett ägt fönster som `onLoad`
    // öppnade och som då tog aktiveringen får den tillbaka, som när ägaren
    // redan syntes.
    // Bara ett som lever, är öppet, syns och fortfarande är mitt.
    if (första) {
        const LoadActivated barn = std::exchange(loadActivated_, {});
        if (barn.w && !barn.life.expired() && barn.w->open_ &&
            !barn.w->pendingClose_ && !barn.w->hidden_ &&
            barn.w->owner_ == this)
            barn.w->win_->raise();
    }
    fireLifecycle(onShow);
}

// ETT FÖNSTER SOM AKTIVERAS MEDAN ÄGAREN KÖR SIN `onLoad` —
// ägaren minns det senaste, för sin egen första visning
// efter handlern tar annars aktiveringen ifrån det. Popuper, offscreen-
// och noActivate-fönster tar aldrig aktiveringen, och inte heller den FÖRSTA
// visningen av ett `showInactive`-fönster.
void AppWindow::noteLoadActivation(bool förstaVisningen)
{
    if (!owner_ || !owner_->loading_ || popup_ || offscreen_ || noActivate_ ||
        (showInactive_ && förstaVisningen))
        return;
    owner_->loadActivated_ = LoadActivated{this, lifeToken()};
}

// STARTPLATSEN i placeringsrymden, eller
// inget svar: ett popup-ankare och förvalet lämnar `initX_`/`initY_` orörda
// (systemfältsmenyns värd bär sin plats där), och en tom skärmlista (Wayland,
// ingen skärmrymd) låter kompositören välja. Storleken är den
// fönstret har just nu (`initW_`/`initH_` — begäran före init, OS-fönstrets
// efter `onLoad`).
std::optional<gfx::Pointf> AppWindow::startPlacementFor(
    const AppWindow* owner) const
{
    if (hasAnchor_ || popup_ || startPosition_ == StartPosition::Default)
        return std::nullopt;
    detail::StartRequest req;
    req.pos         = startPosition_;
    req.logical     = gfx::Pointf{static_cast<float>(locX_),
                                  static_cast<float>(locY_)};
    req.logicalSize = gfx::Sizef{static_cast<float>(initW_),
                                 static_cast<float>(initH_)};
    // Ägarens AKTUELLA klientyta — bara en öppen, synlig och inte
    // minimerad ägare; annars centreras på skärmen. Hela klientytan:
    // `contentArea` är ytan under en OS-titelrad, och origo är klientens.
    if (owner && owner->open_ && !owner->hidden_ && !owner->offscreen_ &&
        owner->win_->placement().state != gfx::WindowState::Minimized) {
        const gfx::Rectf  ca = owner->win_->contentArea();
        const gfx::Pointf a  = owner->win_->clientToScreen(0.0f, 0.0f);
        const gfx::Pointf b  = owner->win_->clientToScreen(ca.x + ca.w,
                                                            ca.y + ca.h);
        req.ownerRect = gfx::Rectf{a.x, a.y, b.x - a.x, b.y - a.y};
    }
    return detail::startPlacement(req, gfx::screens());
}

// ------------------------------------------------ innehållsytan

// Chromets höjd i LOGISKA px — EN sanning, delad med layouten.
//
// TopBar räknas bara vid `Chrome::Custom`: vid `Native` ritar OS:et
// namnlisten UTANFÖR klientytan, och vid `None` finns ingen. En DOLD bar
// skär ingen remsa och räknas därför inte heller — precis samma regel som
// dock-carvingen följer, vilket är hela poängen med att måttet härleds här
// i stället för att appen kopierar in `barHeight` i sin egen kod.
float AppWindow::chromeInsetH() const
{
    const Style& st = App::themeState().style;
    float        h  = 0.0f;
    if (chrome_ == gfx::Chrome::Custom && topBar_ && topBar_->isVisible())
        h += topBar_->height > 0.0f ? topBar_->height : st.barHeight;
    // MENYRADEN är klientyta men skär ändå en remsa ur innehållsytan —
    // samma axelregel som de andra barerna: satt höjd vinner, annars
    // stilens `menuRowH` (en menyradsrubrik är en menyrad).
    if (menuBar.isVisible())
        h += menuBar.height > 0.0f ? menuBar.height : st.menuRowH;
    if (statusBar.isVisible())
        h += statusBar.height > 0.0f ? statusBar.height : st.statusBarH;
    return h;
}

// Klientstorleken ur fönstrets FAKTISKA yta. `contentArea()` är allt UNDER
// titelraden (fasadens ord), så titelraden läggs tillbaka — vid Custom är
// `titlebarHeight()` 0 eftersom namnlisten är vår egen widget, och då är de
// två samma sak. Enheten blir LOGISKA px, samma som settrarna talar.
void AppWindow::trackClientSize()
{
    if (!open_ || !win_)
        return;
    const gfx::Rectf a = win_->contentArea();
    const float      s = win_->uiScale();
    if (s <= 0.0f || a.w <= 0.0f || a.h <= 0.0f)
        return;
    initW_ = static_cast<int>(a.w / s + 0.5f);
    initH_ = static_cast<int>((a.h + win_->titlebarHeight()) / s + 0.5f);
}

gfx::Sizef AppWindow::clientSize() const
{
    return gfx::Sizef{static_cast<float>(initW_),
                      static_cast<float>(initH_)};
}

gfx::Sizef AppWindow::contentSize() const
{
    return gfx::Sizef{static_cast<float>(initW_),
                      static_cast<float>(initH_) - chromeInsetH()};
}

// Innehållsytan → klientytan. Uppskjuten upplösning: begäran lagras och
// räknas om varje gång chromet KAN ha ändrats, så appen aldrig behöver
// känna ramverkets tal. Utan begäran (contentW_ == 0) är den en no-op —
// det är den som gör att Widget::setVisible kan kalla den fritt.
void AppWindow::applyContentSize()
{
    // GRÄNSERNA I INNEHÅLLSMÅTT följer chromet också, och FÖRE
    // storleken — `refit`s ordning: en låst innehållsyta får aldrig hålla kvar
    // den gamla klientgränsen när statusraden visas (OS:et hade klämt den nya
    // storleken till den). Före `show()` en no-op; `showOwned` kopierar då
    // de effektiva gränserna till `WindowDesc`.
    if (minInContent_ || maxInContent_) {
        applySizeLimits();
        applyCaptionBoxes();
    }
    // En anpassad axel går genom `refit` — när
    // innehållet går att mäta. Före init (ingen font) gäller begäran rakt av,
    // och `showOwned` anpassar efter init.
    if (sizeToContent_ != SizeToContent::Manual && canFit()) {
        refit();
        return;
    }
    if (contentW_ <= 0 || contentH_ <= 0)
        return;
    const int w = contentW_;
    const int h = contentH_ + chromeInsetPx();
    if (w == initW_ && h == initH_)
        return;   // oförändrat — rör inte OS-fönstret
    initW_ = w;
    initH_ = h;
    win_->setSize(w, h);
}

// Klientytan rakt av — med en anpassad axel genom
// samma beräkning som `setContentSize`: axeln vinner också här, och
// det senast tillämpade måttet glöms så rättelsen alltid körs.
void AppWindow::setClientSize(int w, int h)
{
    contentW_ = contentH_ = 0;   // rå klientstorlek vinner — se setContentSize
    initW_ = w;
    initH_ = h;
    if (sizeToContent_ != SizeToContent::Manual) {
        sizeRequestNew_ = true;
        fitWanted_.reset();
        if (canFit()) {
            refit();
            return;
        }
    }
    win_->setSize(w, h);
}

// FÖNSTRET FÖLJER INNEHÅLLET — attributet. Före
// `show()` lagras det; `showOwned` mäter efter init. Tillbaka till
// `Manual` släpper låsningen och lämnar storleken där den är.
void AppWindow::setSizeToContent(SizeToContent s)
{
    if (s == sizeToContent_)
        return;
    sizeToContent_ = s;
    fitWanted_.reset();
    fitClientW_ = fitClientH_ = 0;
    fitEmptyLogged_ = false;
    if (!open_)
        return;
    if (s == SizeToContent::Manual)
        applySizeLimits();
    else if (canFit())
        refit();
    applyCaptionBoxes();   // låst eller inte — maximeraknappen följer
    invalidate();
}

// LÅS ELLER GOLV — samma form som attributet ovan: före
// `show()` lagras läget, i drift glöms det senast tillämpade måttet och
// anpassningen körs, så GrowOnly → GrowAndShrink låser till innehållet direkt.
// `Manual` struntar i läget — då finns ingen anpassad axel att röra.
void AppWindow::setSizeToContentMode(SizeToContentMode m)
{
    if (m == sizeToContentMode_)
        return;
    sizeToContentMode_ = m;
    fitWanted_.reset();
    if (!open_ || sizeToContent_ == SizeToContent::Manual)
        return;
    if (canFit())
        refit();
    applyCaptionBoxes();   // låst eller golv — maximeraknappen följer
    invalidate();
}

// `notifyContentInsets`s ingång: ett barn visades eller
// doldes. `Manual` — synkront, no-op utan begärd innehållsstorlek.
// En anpassad axel POSTAS (se headern): körd i samma pumpvarv före framen, så
// ingen bildruta ritas i fel storlek.
void AppWindow::contentInsetsChanged()
{
    if (sizeToContent_ == SizeToContent::Manual) {
        applyContentSize();
        return;
    }
    postRefit();
}

// EN anpassning åt gången i postkön, med fönstrets livstecken. Före init är
// det ingenting: `showOwned` anpassar ändå (och `glaze::app()` hade skapat
// App mitt i en konstruktor).
void AppWindow::postRefit()
{
    if (!open_ || refitPosted_)
        return;
    refitPosted_ = true;
    const std::weak_ptr<char> liv = lifeToken();
    glaze::app().post([this, liv] {
        if (liv.expired())
            return;
        refitPosted_ = false;
        if (open_ && !pendingClose_ && sizeToContent_ != SizeToContent::Manual &&
            canFit())
            refit();
    });
}

// Innehållet går att mäta: ett öppet fönster med font (`makeCtx` läser den).
bool AppWindow::canFit() const
{
    return open_ && font_ && win_ && win_->id() != 0;
}

// Det ÖNSKADE innehållsmåttet, oavrundat: rotens barn utom de
// inbyggda raderna — `chromeInsetH()` räknar menyraden och statusraden, och
// namnlisten och programikonen står utanför flödet. `freeW`/`freeH` är
// innehållsytans mått på de FRIA axlarna (och fönstrets aktuella eller begärda
// bredd); en anpassad axel räknas ur barnen, bredden först, höjden med den
// kända bredden.
gfx::Sizef AppWindow::fitContent(const Ctx& c, float freeW, float freeH) const
{
    const bool fitW = fitsWidth(sizeToContent_);
    const bool fitH = fitsHeight(sizeToContent_);
    const Widget* const skip[] = {&menuBar, &statusBar};
    gfx::Sizef want{freeW, freeH};
    if (fitW)
        want.w = root_.extentOf(c, std::nullopt, skip).w;
    if (fitH) {
        // HÖJDEN VID DEN FAKTISKA BREDDEN: i `GrowOnly` är
        // innehållets bredd bara GOLVET, och fönstret får vara bredare — då
        // mäts höjden med den bredd det har, så ett brett fönster får vara
        // lågt. Låst är bredden innehållets, och höjden mäts med den.
        const float bredd =
            fitW && sizeToContentMode_ == SizeToContentMode::GrowOnly
                ? std::max(freeW, want.w)
                : want.w;
        want.h = root_.extentOf(c, bredd, skip).h;
    }
    return want;
}

// ANPASSNINGEN. Maximerat eller minimerat rörs inte
// (`WindowRestored` postar en ny). Den fria axeln är begärans när en ny
// begäran väntar, annars fönstrets AKTUELLA (användarens drag vinner).
// Innehåll med 0 på en anpassad axel (en tom dialog) behåller den fria
// storleken där — loggat en gång. Läget avgör den anpassade
// axeln: LÅST är storleken innehållets, som GOLV är den max(begäran eller
// aktuell storlek, innehållet) inom de effektiva gränserna. Ordningen är
// bärande:
//   1. `fitWanted_` FÖRE `setSize` — D3D11 ritar synkront i `WM_SIZE`, och
//      den nästlade ritningens fråga (`fitDue`) ska se det nya måttet;
//   2. de NYA gränserna före storleken — en låst axel kan då aldrig hålla
//      kvar den gamla storleken (`DockFloatWindow::fitToolBar` lyfter i
//      stället gränserna till 0, men den byter då stil på ett fast fönster).
void AppWindow::refit()
{
    if (!canFit() || sizeToContent_ == SizeToContent::Manual)
        return;
    const gfx::WindowState läge = win_->placement().state;
    if (win_->maximized() || läge == gfx::WindowState::Minimized)
        return;
    const bool  fitW  = fitsWidth(sizeToContent_);
    const bool  fitH  = fitsHeight(sizeToContent_);
    const Ctx   c     = makeCtx();
    const float inset = chromeInsetH();
    const int   insetPx = chromeInsetPx();   // som applyContentSize
    const bool  ny    = std::exchange(sizeRequestNew_, false);
    // Klientstorleken på de FRIA axlarna: begärans, eller fönstrets aktuella
    // (`initW_/initH_` följer användarens drag, `trackClientSize`). Måttet på
    // en fri axel räknas ur den LEVANDE innehållsytan — `fitDue` frågar med
    // samma, så de två aldrig tycker olika om samma fönster.
    int   w     = initW_;
    int   h     = initH_;
    float freeW = c.area.w / c.scale;
    float freeH = static_cast<float>(initH_) - inset;
    if (ny) {
        if (contentW_ > 0)
            w = contentW_;
        if (contentH_ > 0)
            h = contentH_ + insetPx;
        freeW = static_cast<float>(w);
        freeH = static_cast<float>(h) - inset;
    }
    gfx::Sizef want = fitContent(c, freeW, freeH);
    if ((fitW && want.w <= 0.0f) || (fitH && want.h <= 0.0f)) {
        if (!fitEmptyLogged_)
            gfx::log("setSizeToContent i \"%s\": innehållet har inget mått på "
                     "en anpassad axel — fönstrets storlek gäller där",
                     title_.str().c_str());
        fitEmptyLogged_ = true;
    }
    // Innehållets KLIENTMÅTT på de anpassade axlarna, avrundat UPPÅT
    // (meddelanderutans regel), minst 1; 0 = inget mått.
    const int innehållW =
        fitW && want.w > 0.0f ? std::max(1, static_cast<int>(std::ceil(want.w)))
                              : 0;
    const int innehållH =
        fitH && want.h > 0.0f
            ? std::max(1, static_cast<int>(std::ceil(want.h)) + insetPx)
            : 0;
    fitWanted_ = want;
    if (sizeToContentMode_ == SizeToContentMode::GrowOnly) {
        // GOLVET: innehållet är min, och storleken är begäran
        // eller den aktuella, upplyft till golvet och inom appens max —
        // fönstret VÄXER när innehållet passerar det och KRYMPER ALDRIG
        // SJÄLVT. Utan mått på axeln finns inget golv (appens gränser gäller).
        fitClientW_ = innehållW;
        fitClientH_ = innehållH;
        const auto inom = [](int v, int lo, int hi) {
            v = std::max(v, lo);
            return hi > 0 ? std::min(v, hi) : v;
        };
        if (fitW)
            w = inom(w, effMinW(), effMaxW());
        if (fitH)
            h = inom(h, effMinH(), effMaxH());
    } else {
        // LÅST: storleken ÄR innehållet, och en axel utan mått behåller sin
        // storlek och låses där.
        if (innehållW > 0)
            w = innehållW;
        if (innehållH > 0)
            h = innehållH;
        fitClientW_ = fitW ? w : 0;
        fitClientH_ = fitH ? h : 0;
    }
    applySizeLimits();
    applyCaptionBoxes();
    if (w == initW_ && h == initH_ && !ny)
        return;   // oförändrat — rör inte OS-fönstret
    initW_ = w;
    initH_ = h;
    win_->setSize(w, h);
}

// RITNINGENS FRÅGA — efter varje ritad frame i ett fönster med
// en anpassad axel som inte är maximerat: det som ändrar måtten utan en
// synlighetsändring (en `setText`, ett råfält, ett språk-, DPI- eller
// stilbyte). En mätning av rotens barn per frame, bara i sådana fönster.
// Jämförelsen är på de ANPASSADE axlarna av det oavrundade måttet.
void AppWindow::fitDue()
{
    if (sizeToContent_ == SizeToContent::Manual || refitPosted_ || !canFit() ||
        win_->maximized())
        return;
    const Ctx        c    = makeCtx();
    const gfx::Sizef want = fitContent(c, c.area.w / c.scale, c.area.h / c.scale);
    const bool fitW = fitsWidth(sizeToContent_);
    const bool fitH = fitsHeight(sizeToContent_);
    if (!fitWanted_ || (fitW && want.w != fitWanted_->w) ||
        (fitH && want.h != fitWanted_->h))
        postRefit();
}

// STORLEKSGRÄNSERNA. EN lagrad sanning, tre konsumenter:
// `showOwned` kopierar den till `WindowDesc` (startgränserna — utan den
// vägen når en gräns satt i konstruktorn ALDRIG OS-fönstret),
// settrarna efter `show()` går direkt till OS-fönstret, och
// `pushHitRegions` läser `isFixedSize()` för att sluta visa resize-pekare.
void AppWindow::applySizeLimits()
{
    if (open_)
        win_->setSizeLimits(effMinW(), effMinH(), effMaxW(), effMaxH());
}

// DE EFFEKTIVA GRÄNSERNA — kontraktet i headern.
// Utan anpassning på axeln (`fitClient*` 0) appens rakt av. LÅST är den
// anpassade storleken både min och max; som GOLV är innehållet min, och appens
// max gäller men aldrig under innehållet — innehållet vinner, precis som det
// låsta läget struntar i appens gränser på axeln.
int AppWindow::effMinW() const
{
    if (fitClientW_ <= 0)
        return minW_;
    return sizeToContentMode_ == SizeToContentMode::GrowOnly
               ? std::max(minW_, fitClientW_)
               : fitClientW_;
}

int AppWindow::effMaxW() const
{
    if (fitClientW_ <= 0)
        return maxW_;
    if (sizeToContentMode_ == SizeToContentMode::GrowOnly)
        return maxW_ > 0 ? std::max(maxW_, fitClientW_) : 0;
    return fitClientW_;
}

int AppWindow::effMinH() const
{
    if (fitClientH_ <= 0)
        return appMinH();
    return sizeToContentMode_ == SizeToContentMode::GrowOnly
               ? std::max(appMinH(), fitClientH_)
               : fitClientH_;
}

int AppWindow::effMaxH() const
{
    if (fitClientH_ <= 0)
        return appMaxH();
    if (sizeToContentMode_ == SizeToContentMode::GrowOnly)
        return appMaxH() > 0 ? std::max(appMaxH(), fitClientH_) : 0;
    return fitClientH_;
}

// APPENS HÖJDGRÄNSER I KLIENTMÅTT — en gräns i innehållsmått får
// chromets höjd NU, så den följer statusraden och stilen utan att lagras om.
int AppWindow::appMinH() const
{
    return minInContent_ && minH_ > 0 ? minH_ + chromeInsetPx() : minH_;
}

int AppWindow::appMaxH() const
{
    return maxInContent_ && maxH_ > 0 ? maxH_ + chromeInsetPx() : maxH_;
}

// Chromets höjd i HELA logiska px — avrundad, som innehållsstorleken alltid
// har räknats (`applyContentSize`, `refit`).
int AppWindow::chromeInsetPx() const
{
    return static_cast<int>(chromeInsetH() + 0.5f);
}

// ALLTID ÖVERST. Samma tvådelade väg som storleksgränserna:
// före `show()` lagras begäran och följer med i `WindowDesc`, efter går
// den direkt till OS-fönstret.
void AppWindow::setTopmost(bool on)
{
    topmost_ = on;
    if (open_)
        win_->setTopmost(on);
}

// Svaret är vad som FAKTISKT gäller, inte vad som begärdes — ett öppet
// fönster frågar OS-fönstret (Wayland svarar alltid false). Före show()
// finns bara begäran, och den ÄR då det bästa svaret (samma resonemang som
// `clientSize()` för före-show).
bool AppWindow::topmost() const
{
    return open_ ? win_->topmost() : topmost_;
}

// AKTIVITETSFÄLTET. Bara ATTRIBUTET, till skillnad från
// `setTopmost` ovan: fasaden har ingen sättare efter init (Windows kräver
// dölj–byt–visa), så ett öppet fönster får en loggrad och värdet väntar på
// nästa öppning.
void AppWindow::setShowInTaskbar(bool on)
{
    if (open_ && on != showInTaskbar_)
        gfx::log("AppWindow::setShowInTaskbar(%s): \"%s\" är redan öppet — "
                 "gäller nästa öppning",
                 on ? "true" : "false", title_.str().c_str());
    showInTaskbar_ = on;
}

// ------------------------------- PLATSEN OCH LÄGET

// Ett ATTRIBUT som läses när fönstret öppnas (`showOwned`) — samma regel som
// aktivitetsfältet: ett öppet fönster loggar, och värdet gäller nästa gång.
// FÖRE DEN FÖRSTA VISNINGEN (`onLoad`, en dold start som inte visats)
// läses det ännu: `revealHidden` räknar startplatsen precis före visningen.
void AppWindow::setStartPosition(StartPosition p)
{
    if (open_ && !firstShowPending_ && p != startPosition_)
        gfx::log("AppWindow::setStartPosition: \"%s\" är redan öppet — "
                 "gäller nästa öppning",
                 title_.str().c_str());
    startPosition_ = p;
}

// FÖRE öppningen är positionen startplatsen (Manual, klämd in i arbetsytan
// när fönstret öppnas). EFTER flyttar den OS-fönstret — omräknad med
// primärskärmens skala och UTAN klämning (en flytande panel får dras
// delvis utanför skärmen, som ett OS-fönster), och bara i normalläge: ett
// maximerat fönsters position är OS:ets, och ett minimerat står inte där det
// återställs. Wayland saknar skärmrymden; fasaden loggar (`setPosition`).
// FÖRE DEN FÖRSTA VISNINGEN (`onLoad`, en dold start som inte visats)
// är den startplatsen som före öppningen: klämd in i arbetsytan när
// fönstret visas.
void AppWindow::setLocation(int x, int y)
{
    if (!open_ || firstShowPending_) {
        locX_          = x;
        locY_          = y;
        hasLocation_   = true;
        startPosition_ = StartPosition::Manual;
        return;
    }
    if (win_->placement().state != gfx::WindowState::Normal) {
        gfx::log("AppWindow::setLocation: \"%s\" är maximerat eller "
                 "minimerat — positionen ignoreras",
                 title_.str().c_str());
        return;
    }
    const gfx::Pointf p = detail::toPlacement(
        gfx::Pointf{static_cast<float>(x), static_cast<float>(y)},
        detail::primaryScale(gfx::screens()));
    win_->setPosition(static_cast<int>(std::lround(p.x)),
                      static_cast<int>(std::lround(p.y)));
}

// FÖRE öppningen är läget STARTLÄGET — `showOwned` för det till
// backenden, som tillämpar det vid fönstrets första synlighet. EFTER är det
// fönsterkontrollerna — som före den första visningen själva byter
// startläget, i AppWindow och i backenden.
void AppWindow::setWindowState(gfx::WindowState s)
{
    if (!open_) {
        startState_ = s;
        return;
    }
    switch (s) {
    case gfx::WindowState::Normal:    restore();  break;
    case gfx::WindowState::Minimized: minimize(); break;
    case gfx::WindowState::Maximized: maximize(); break;
    }
}

// STARTLÄGET som det TILLÄMPAS — EN regel för `showOwned` och för
// läsarna före den första visningen: en popup och ett offscreen-fönster får
// det aldrig, och en MODAL startar aldrig minimerad (den hade blockerat all
// input utan att synas — samma fälla som den dolda modalen).
gfx::WindowState AppWindow::effectiveStartState() const
{
    if (popup_ || offscreen_)
        return gfx::WindowState::Normal;
    if (isModal_ && startState_ == gfx::WindowState::Minimized)
        return gfx::WindowState::Normal;
    return startState_;
}

gfx::WindowState AppWindow::windowState() const
{
    if (open_)
        return firstShowPending_ ? effectiveStartState()
                                 : win_->placement().state;
    if (closedGeometry_)
        return closedGeometry_->state;
    return startState_;
}

// OS-fönstrets geometri: normalrutan ur fasaden, positionen omräknad till
// logiska px med primärskärmens skala. Kräver att OS-fönstret finns.
WindowGeometry AppWindow::liveGeometry() const
{
    const gfx::WindowPlacement p = win_->placement();
    WindowGeometry             g;
    g.width       = p.width;
    g.height      = p.height;
    g.state       = p.state;
    g.hasPosition = p.hasPosition;
    if (p.hasPosition) {
        const gfx::Pointf l = detail::toLogical(
            gfx::Pointf{static_cast<float>(p.x), static_cast<float>(p.y)},
            detail::primaryScale(gfx::screens()));
        g.x = static_cast<int>(std::lround(l.x));
        g.y = static_cast<int>(std::lround(l.y));
    }
    return g;
}

// Öppet: OS-fönstret. Stängt: kortet från stängningen (`App::executeClose`).
// Före första öppningen — och före den första VISNINGEN:
// begäran.
WindowGeometry AppWindow::geometry() const
{
    if (open_ && !firstShowPending_)
        return liveGeometry();
    if (!open_ && closedGeometry_)
        return *closedGeometry_;
    return requestedGeometry();
}

// BEGÄRAN — klientstorleken (OS-fönstrets när det finns: `initW_`/`initH_`
// följer `setContentSize`), startpositionen och startläget. Före öppningen är
// läget attributet; i ett öppet fönster som inte visats det som tillämpas.
WindowGeometry AppWindow::requestedGeometry() const
{
    WindowGeometry g;
    g.x           = locX_;
    g.y           = locY_;
    g.hasPosition = hasLocation_;
    g.width       = initW_;
    g.height      = initH_;
    g.state       = open_ ? effectiveStartState() : startState_;
    return g;
}

// ------------------------------------------- PEKARNA

void AppWindow::setUseWaitCursor(bool on)
{
    if (useWaitCursor_ == on)
        return;
    useWaitCursor_ = on;
    refreshCursor();
}

// Omprövningen med den SENAST KÄNDA pekarpositionen — samma väg som en
// musrörelse tar, så capture- och tryckprioriteten i `updateCursor` gäller
// också här. Utan en känd position (pekaren utanför fönstret, eller ingen
// rörelse än i det här OS-fönstret) finns ingenting att pröva: formen sätts
// av nästa rörelse.
void AppWindow::refreshCursor()
{
    if (!open_ || !mouseInside_)
        return;
    const Ctx c = makeCtx();
    updateCursor(widgetAt(lastMouse_.x, lastMouse_.y, c), lastMouse_.x,
                 lastMouse_.y);
}

void AppWindow::setMinimumSize(int w, int h)
{
    minW_         = w;
    minH_         = h;
    minInContent_ = false;   // klientformen gäller — den satt sist
    sizeLimitsChanged();
}

void AppWindow::setMaximumSize(int w, int h)
{
    maxW_         = w;
    maxH_         = h;
    maxInContent_ = false;
    sizeLimitsChanged();
}

void AppWindow::sizeLimitsChanged()
{
    applySizeLimits();
    // Gränserna kan göra fönstret LÅST (min == max), och då ska
    // maximera-knappen bort — knappuppsättningen är en funktion av
    // gränserna och måste räknas om när de ändras.
    applyCaptionBoxes();
    invalidate();   // hit-kartan ritas om ur trädet vid nästa repaint
}

// GRÄNSERNA I INNEHÅLLSMÅTT — kontraktet i headern. Lagrade som
// innehållsmått med formen bredvid; klientmåttet räknas när gränsen används.
void AppWindow::setMinimumContentSize(int w, int h)
{
    if (w < 0 || h < 0) {
        gfx::log("AppWindow::setMinimumContentSize(%d, %d) i \"%s\": negativt "
                 "mått (0 = obegränsat) — ignoreras",
                 w, h, title_.str().c_str());
        return;
    }
    minW_         = w;
    minH_         = h;
    minInContent_ = true;
    sizeLimitsChanged();
}

void AppWindow::setMaximumContentSize(int w, int h)
{
    if (w < 0 || h < 0) {
        gfx::log("AppWindow::setMaximumContentSize(%d, %d) i \"%s\": negativt "
                 "mått (0 = obegränsat) — ignoreras",
                 w, h, title_.str().c_str());
        return;
    }
    maxW_         = w;
    maxH_         = h;
    maxInContent_ = true;
    sizeLimitsChanged();
}

// Gränserna FÖRST, sedan storleken (`setContentSize` → `applyContentSize`,
// som skickar gränserna till OS-fönstret före `setSize` igen): fönstret får
// aldrig klämmas av en gammal gräns på väg till den nya storleken.
void AppWindow::setFixedContentSize(int w, int h)
{
    if (w <= 0 || h <= 0) {
        gfx::log("AppWindow::setFixedContentSize(%d, %d) i \"%s\": måtten "
                 "måste vara positiva — ignoreras",
                 w, h, title_.str().c_str());
        return;
    }
    minW_ = maxW_ = w;
    minH_ = maxH_ = h;
    minInContent_ = maxInContent_ = true;
    sizeLimitsChanged();
    setContentSize(w, h);
}

gfx::Sizef AppWindow::minimumSize() const
{
    return gfx::Sizef{static_cast<float>(minW_),
                      static_cast<float>(appMinH())};
}

gfx::Sizef AppWindow::maximumSize() const
{
    return gfx::Sizef{static_cast<float>(maxW_),
                      static_cast<float>(appMaxH())};
}

// I INNEHÅLLSMÅTT: en klientgräns minus chromets höjd. En minimigräns lägre
// än chromet begränsar inte innehållet (0); en maxgräns lägre än chromet
// lämnar högst en px — 0 hade betytt obegränsat.
gfx::Sizef AppWindow::minimumContentSize() const
{
    const int h = minInContent_ || minH_ <= 0
                      ? minH_
                      : std::max(0, minH_ - chromeInsetPx());
    return gfx::Sizef{static_cast<float>(minW_), static_cast<float>(h)};
}

gfx::Sizef AppWindow::maximumContentSize() const
{
    const int h = maxInContent_ || maxH_ <= 0
                      ? maxH_
                      : std::max(1, maxH_ - chromeInsetPx());
    return gfx::Sizef{static_cast<float>(maxW_), static_cast<float>(h)};
}

void AppWindow::setContentSize(int w, int h)
{
    if (w <= 0 || h <= 0) {
        // Ogiltig begäran ignoreras HELT — hade den lagrats vore fönstret
        // kvar med en begäran som `applyContentSize` sedan vägrar lösa, och
        // storleken hade tyst slutat följa med vid stilbyte.
        gfx::log("setContentSize(%d, %d): måtten måste vara positiva "
                 "(ignoreras)", w, h);
        return;
    }
    contentW_ = w;
    contentH_ = h;
    // Nollställ cachen så applyContentSize verkligen skriver: initW_/initH_
    // kan råka vara exakt de tal vi vill sätta, och då hade jämförelsen i
    // applyContentSize hoppat över `win_->setSize` för ett redan öppet
    // fönster.
    initW_ = initH_ = 0;
    // En NY begäran: med en anpassad axel
    // ger den den fria axeln vid nästa anpassning, och det senast tillämpade
    // måttet glöms så rättelsen alltid körs.
    sizeRequestNew_ = true;
    fitWanted_.reset();
    applyContentSize();
}

// Fonten i aktuell uiScale — anropas ur showOwned, vid
// WindowDisplayScaleChanged (skalbyte i drift på alla backends: portalens
// textskalning/Xft.dpi på Linux, per-monitor-DPI på Windows,
// backingScaleFactor på Mac) OCH vid byte av UI/Style (måtten äger
// fontstorleken). Gamla font-objektet rivs av unique_ptr:n — atlas-texturen
// hör till samma renderer, så bytet är säkert.
//
// STORLEKEN kommer ur `style().uiFontSize`, inte ur `gfx::fontsize::kUi`:
// radhöjden ensam sätter golvet för varje kontrollhöjd, så en kompakt Style
// måste kunna sänka den. Fasadens konstant är fortfarande enda SIFFERkällan
// — Glazes och Slates Regular sätter fältet till just den.
void AppWindow::reloadFont()
{
    if (win_->id() == 0)
        return;   // OS-fönstret inte initierat — renderern finns inte än
    const float px = style().uiFontSize * win_->uiScale();
    if (!font_ || px != fontPx_) {
        font_   = win_->renderer().loadFont(gfx::FontFamily::UI, px,
                                            gfx::FontStyle::Regular);
        fontPx_ = px;
        if (!font_->valid())
            gfx::log("Hittade ingen systemfont — text uteblir");
    }

    // ROLLFONTERNA — EAGER. Mängden storlekar är SLUTEN
    // (Style räknar upp dem), så de skapas HÄR och aldrig lat i ritvägen.
    // Samma no-op-jämförelse som brödtexten: ett rent färgbyte river
    // ingen fungerande atlas.
    const float hpx = style().headingFontSize * win_->uiScale();
    if (!headingFont_ || hpx != headingPx_) {
        headingFont_ = win_->renderer().loadFont(gfx::FontFamily::UI, hpx,
                                                 gfx::FontStyle::Regular);
        headingPx_   = hpx;
    }
}

// Dölj utan att stänga: OS-ytan göms, objektet och trädet
// lever vidare; show() tar fram fönstret igen. Fyrar onHide.
void AppWindow::hide()
{
    // DOLD START: före öppningen finns inget att dölja —
    // `hidden_` blir begäran och `showOwned` skapar OS-fönstret dolt. Ingen
    // onHide: fönstret har aldrig synts.
    if (!open_) {
        hidden_ = true;
        return;
    }
    // FÖRE DEN FÖRSTA VISNINGEN — i `onLoad`,
    // eller i ett veto mot ett `close()` därifrån (`App::flushPendingCloses`
    // fortsätter annars till visningen) — är det också en DOLD START: fönstret
    // visas inte, och ingen onHide — det har aldrig synts. Under en dold start
    // som redan gäller är det ingenting. Aldrig för en modal, av samma skäl som
    // vid öppningen.
    if (firstShowPending_) {
        if (isModal_) {
            gfx::log("AppWindow::hide: \"%s\" är modal och startar aldrig "
                     "dold — hide() före den första visningen ignoreras",
                     title_.str().c_str());
            return;
        }
        showAfterLoad_ = false;
        loadActivated_ = {};   // ingen första visning direkt efter onLoad
        return;
    }
    if (hidden_)
        return;
    toolTipCancel(false);   // ett dolt fönsters tooltip ska inte sväva kvar
    // …och inga hållna tangenter: släppet kommer aldrig
    // till ett dolt fönster. Tangentminnet tas ut NU, med mängden, och
    // levereras som släpp med `cancelled` nedan — ett
    // läge som en tangent slog på får sitt slut också när fönstret döljs.
    heldKeys_.reset();
    std::vector<KeyTarget> släpp = std::exchange(keyTargets_, {});
    hidden_ = true;
    active_ = false;   // ett dolt fönster är inte aktivt
    win_->setVisible(false);
    // Trädet syns inte längre (`Widget::shownChanged`) — det
    // som bara går medan det syns stannar NU, inte vid nästa ritning (som
    // ett dolt fönster aldrig gör) — och de ägda fönstren får veta
    // (`ownerVisibilityChanged`).
    root_.notifyShownChanged();
    notifyOwnedVisibility(false);
    if (!releaseKeyTargets(std::move(släpp)))
        return;   // en `onKeyUp`-handler rev fönstret
    fireLifecycle(onHide);
}

// run()-kärnan: visa + pumpa tills stängt — EN pump, alla ingångar
// (huvudfönster och modala dialoger delar implementation; modaliteten är
// BARA filtret i App::dispatch, returvärdet BARA result_). Stängs ägaren
// medan en modal pumpar kaskad-stänger App modalen först — pumpen märker
// det och run returnerar (DialogResult::None).
bool AppWindow::runInternal(AppWindow* owner)
{
    App& a = glaze::app();
    if (!a.ok_)
        return false;
    if (open_) {
        gfx::log("AppWindow::run: fönstret är redan öppet");
        win_->raise();
        return false;
    }

    result_ = DialogResult::None;
    showOwned(owner);
    if (!open_)
        return false;   // show misslyckades (loggat)

    if (isModal_)
        a.modalStack_.push_back(this);
    a.pumpUntilClosed(*this);   // nästlad pump; executeClose plockar
                                // bort fönstret ur modalstacken

    // Ytterst pump-nivå: kvarlevande fönster är nästan alltid ett appfel
    // — säg det högt men låt dem leva (main kan köra run() på ett av dem).
    if (a.pumpDepth_ == 0)
        for (AppWindow* o : a.windows_)
            gfx::log("AppWindow::run: fönstret \"%s\" lever kvar efter run "
                     "— glömd stängning?", o->title().c_str());
    return true;
}

int AppWindow::run()
{
    return runInternal(nullptr) ? 0 : 1;
}

DialogResult AppWindow::run(AppWindow& owner)
{
    runInternal(&owner);
    return result_;
}

void AppWindow::closeWithResult(DialogResult r)
{
    result_ = r;
    close();
}

// ---------------------------- STANDARD- OCH AVBRYTKNAPPEN
//
// Kontraktet står vid deklarationerna i app_window.h. Pekaren bär knappens
// LIVSTECKEN: knappen är normalt en värdemedlem som dör före fönstrets bas,
// och en sättning före `adopt` (den genererade ordningen) har inget fönster
// att fråga. Allt som ANVÄNDER knappen prövar därför tecknet, fönstret och
// trädet när frågan ställs, aldrig när knappen sätts.
void AppWindow::setAcceptButton(Button& b)
{
    if (AppWindow* f = b.appWindow(); f && f != this)
        gfx::log("AppWindow(%s)::setAcceptButton(%s): knappen sitter i ett "
                 "annat fönster — den tar ingen Enter här",
                 title_.str().c_str(), b.name.c_str());
    Button* const förut = defaultButton();
    acceptBtn_  = &b;
    acceptLife_ = b.lifeToken();
    defaultButtonChanged(förut);
}

void AppWindow::setAcceptButton(std::nullptr_t)
{
    Button* const förut = defaultButton();
    acceptBtn_          = nullptr;
    acceptLife_.reset();
    defaultButtonChanged(förut);
}

Button* AppWindow::acceptButton() const
{
    return acceptLife_.expired() ? nullptr : acceptBtn_;
}

// Avbrytknappen RITAS inte annorlunda — Escape syns inte i fönstret — så dess
// sättare invaliderar ingenting.
void AppWindow::setCancelButton(Button& b)
{
    if (AppWindow* f = b.appWindow(); f && f != this)
        gfx::log("AppWindow(%s)::setCancelButton(%s): knappen sitter i ett "
                 "annat fönster — den tar ingen Escape här",
                 title_.str().c_str(), b.name.c_str());
    cancelBtn_  = &b;
    cancelLife_ = b.lifeToken();
}

void AppWindow::setCancelButton(std::nullptr_t)
{
    cancelBtn_ = nullptr;
    cancelLife_.reset();
}

Button* AppWindow::cancelButton() const
{
    return cancelLife_.expired() ? nullptr : cancelBtn_;
}

Button* AppWindow::liveAccept() const
{
    Button* const b = acceptButton();
    return b && b->appWindow() == this ? b : nullptr;
}

Button* AppWindow::liveCancel() const
{
    Button* const b = cancelButton();
    return b && b->appWindow() == this ? b : nullptr;
}

bool AppWindow::usable(const Button& b) const
{
    return b.appWindow() == this && shownInTree(b) && b.isEnabled();
}

// STANDARDKNAPPEN — Win32-modellen: en användbar fokuserad knapp trycker
// själv på Enter (`Button::keyDown`), så den är standard; annars den
// användbara acceptknappen. Utan acceptknapp ingen alls, så ett fönster som
// aldrig satt en påverkas inte. `dynamic_cast` är svaret på "är det
// fokuserade en knapp" — en egen knappsubklass räknas, och fokus på något
// annat lämnar acceptknappen som standard.
Button* AppWindow::defaultButton() const
{
    Button* const a = liveAccept();
    if (!a)
        return nullptr;
    if (auto* f = dynamic_cast<Button*>(focusedWidget_); f && usable(*f))
        return f;
    return usable(*a) ? a : nullptr;
}

bool AppWindow::isDefaultButton(const Button& b) const
{
    return defaultButton() == &b;
}

void AppWindow::defaultButtonChanged(Button* förut)
{
    Button* const nu = defaultButton();
    if (nu == förut)
        return;
    if (förut)
        förut->invalidate();
    if (nu)
        nu->invalidate();
}

// KANDIDATERNA är alltid två — acceptknappen och en fokuserad knapp — så en
// ändring som rör någon av dem ritar om båda; en som inte rör dem ändrar
// inte vilken som är standard. Bytet av själva kandidaterna (sättaren,
// fokuset) går genom `defaultButtonChanged` i stället.
void AppWindow::defaultButtonMayChange(const Widget& ändrad)
{
    Button* const a = liveAccept();
    if (!a)
        return;   // ingen acceptknapp ⇒ ingen standardknapp, före som efter
    const auto under = [&ändrad](const Widget* w) {
        for (; w; w = w->parent())
            if (w == &ändrad)
                return true;
        return false;
    };
    if (!under(a) && !under(focusedWidget_))
        return;
    a->invalidate();
    if (auto* f = dynamic_cast<Button*>(focusedWidget_); f && f != a)
        f->invalidate();
}

// EN ÖPPEN POPUP FÖRE DIALOGKNAPPARNA. En
// handler kan öppna en färgväljare, en datumväljare eller en dropdown (`open()`)
// utan att ge den fokus — en knapp som öppnar en väljare behåller sitt — och
// då går Return och Escape förbi popupen, som bara får tangenter genom sin
// kontroll. Kontrollen är den första i trädordning (i praktiken är EN popup
// öppen: `App::closePopupsNow` stänger den förra när en ny öppnas), och den
// följs till sitt fokusombud, för det är där tangenterna tas när kontrollen
// har fokus (datumväljarens fält). Roten bär också chrome-träden.
Widget* AppWindow::openPopupKeyTarget()
{
    std::vector<Widget*> kvar{&root_};
    while (!kvar.empty()) {
        Widget* const w = kvar.back();
        kvar.pop_back();
        if (w->hasOpenPopup())
            return proxyTarget(w);
        const std::vector<Widget*>& barn = w->children();
        kvar.insert(kvar.end(), barn.rbegin(), barn.rend());   // trädordning
    }
    return nullptr;
}

// Fönster-kontrollerna — tunna wrappers över fasaden (win_ finns alltid,
// tvåfasen gör metoderna no-op-säkra före show).
// FÖRE DEN FÖRSTA VISNINGEN (`onLoad`, en dold start som inte visats)
// byter de tre STARTLÄGET: här, så att `windowState()` och
// `geometry()` svarar med det, och i backenden, som tillämpar det när fönstret
// visas. En modal startar aldrig minimerad (`effectiveStartState`).
void AppWindow::minimize()
{
    if (firstShowPending_) {
        if (isModal_) {
            gfx::log("AppWindow::minimize: \"%s\" är modal och startar aldrig "
                     "minimerad — ignoreras före den första visningen",
                     title_.str().c_str());
            return;
        }
        startState_ = gfx::WindowState::Minimized;
    }
    win_->minimize();
}

void AppWindow::maximize()
{
    if (firstShowPending_)
        startState_ = gfx::WindowState::Maximized;
    win_->maximize();
}

void AppWindow::restore()
{
    if (firstShowPending_)
        startState_ = gfx::WindowState::Normal;
    win_->restore();
}

bool AppWindow::isMaximized() const
{
    if (firstShowPending_)
        return effectiveStartState() == gfx::WindowState::Maximized;
    return win_->maximized();
}

// Hit-test-kartan: FÖNSTRET är källan — regionerna
// räknas ur widget-trädet med samma geometri som ritningen (chrome-ctx:t)
// och trycks ner per repaint. Prioritetsordning (första träffen vinner i
// backenden): resize-hörn, resize-kanter, TopBars barn (knapp-roller/
// Client — spacers hoppas över = tom caption-yta att dra i), sist hela
// TopBar-recten som Caption. Ingen träff = Client. Caption-höjden ur
// trädet följer med (settbar barhöjd ⇒ rätt klientyta).
void AppWindow::pushHitRegions(const Ctx& cch)
{
    std::vector<gfx::HitRegion> regions;
    float captionH = 0.0f;

    // `transparent_` (setTransparent) släcker chromet helt — men ett RUNDAT
    // fönster har kvar sin namnlist och måste därför fortfarande kunna
    // dras och storleksändras.
    if (chrome_ == gfx::Chrome::Custom && (!transparent_ || roundedChrome_)) {
        const gfx::Rectf client = cch.area;
        const float      s      = cch.scale;

        // FAST FÖNSTER: inga resize-grepp. Utan den här
        // raden visar VÅRT chrome en resize-pekare på ett fönster OS:et
        // vägrar resiza — exakt det "lovar något det inte kan"-fel som
        // `setMaximizeBox` redan undviker. Sanningen är `isFixedSize()`, samma
        // som backenden
        // maskar bort WS_THICKFRAME/Resizable ur.
        if (!win_->maximized() && !isFixedSize()) {
            const float b = kResizeBorder * s;
            const float g = kCornerGrip * s;
            using R      = gfx::HitRole;
            auto rr = [&](float x, float y, float w, float h, R role) {
                regions.push_back({gfx::Rectf{x, y, w, h}, role});
            };
            rr(0.0f, 0.0f, g, g, R::ResizeTopLeft);
            rr(client.w - g, 0.0f, g, g, R::ResizeTopRight);
            rr(0.0f, client.h - g, g, g, R::ResizeBottomLeft);
            rr(client.w - g, client.h - g, g, g, R::ResizeBottomRight);
            rr(0.0f, 0.0f, client.w, b, R::ResizeTop);
            rr(0.0f, client.h - b, client.w, b, R::ResizeBottom);
            rr(0.0f, 0.0f, b, client.h, R::ResizeLeft);
            rr(client.w - b, 0.0f, b, client.h, R::ResizeRight);
        }

        if (topBar_ && topBar_->isVisible()) {
            Widget* tbw = topBar_;   // childArea via basens publika kontrakt
            const gfx::Rectf barRect = tbw->resolve(cch);
            captionH = barRect.h / s;

            Ctx cbar  = cch;
            cbar.area = tbw->childArea(cch);
            for (Widget* ch : tbw->children()) {
                if (!ch->isVisible())
                    continue;
                if (dynamic_cast<Spacer*>(ch))
                    continue;   // luckorna ÄR dragytan
                gfx::HitRole role = gfx::HitRole::Client;
                if (auto* lbl = dynamic_cast<Label*>(ch)) {
                    // Inert titel-label = dragyta (annars vinner labelns
                    // Client-rect över barens Caption — första träffen
                    // gäller). En klickbar Label förblir Client.
                    if (!lbl->onClick)
                        role = gfx::HitRole::Caption;
                } else if (auto* cb = dynamic_cast<CaptionButton*>(ch)) {
                    switch (cb->kind()) {
                    case CaptionButton::Kind::Minimize:
                        role = gfx::HitRole::MinButton;
                        break;
                    case CaptionButton::Kind::Maximize:
                        role = gfx::HitRole::MaxButton;
                        break;
                    case CaptionButton::Kind::Close:
                        role = gfx::HitRole::CloseButton;
                        break;
                    }
                }
                regions.push_back({ch->resolve(cbar), role});
            }
            // PROGRAMIKONEN är ett ROTBARN i chrome-trädet
            // och inget barn till baren — men dess plats ligger i baren och
            // är KLICKBAR, inte dragyta, så den måste före barens Caption
            // (första träffen vinner). Efter resize-greppen: kanten är
            // fortfarande fönstrets.
            if (appIcon.isVisible()) {
                const Widget&    ikon = appIcon;
                const gfx::Rectf r    = ikon.resolve(cch);
                if (r.w > 0.0f && r.h > 0.0f)
                    regions.push_back({r, gfx::HitRole::Client});
            }
            regions.push_back({barRect, gfx::HitRole::Caption});
        }
    }

    win_->setCaptionHeight(captionH);
    win_->setHitRegions(std::move(regions));
}

// Uppskjuten stängning: verkställs av App i pump-svansen — säkert att kalla
// ur vilken handler som helst. ETT UNDANTAG: ett POPUP-fönster som står i kön
// rivs redan när nästa popup öppnas (`App::closePopupsNow`, xdg-shells
// ordningskrav), alltså tidigare i varvet och utan vetorunda.
void AppWindow::close()
{
    requestClose(CloseReason::UserClosing);
}

// SKÄLET följer med begäran. En väntande stängning byter inte skäl
// — utom mot ett appavslut, som vinner (deklarationen säger varför).
void AppWindow::requestClose(CloseReason skäl)
{
    if (!open_)
        return;
    if (pendingClose_) {
        if (skäl == CloseReason::AppQuit)
            closeReason_ = skäl;
        return;
    }
    pendingClose_ = true;
    closeReason_  = skäl;
    if (App::instance_)
        App::instance_->queueClose(this);
}

// Innehållspasset — allt som ritas till retained-RT:n. Full omritning
// när:
//   - fullDamage_ (AppWindow::invalidate — semantiken bevaras),
//   - animating_ (frame()-hooken ritar tidsberoende innehåll),
//   - RT:n inte bär förra framens innehåll (nyss resized/degraderat läge),
//   - damage-listan slår i tröskeln (många/stora rects).
// Annars partiell: per damage-rect klipp → bakgrund → frame() → träd-
// vandringen ritar allt vars paintBounds skär recten, i trädordning
// (z-ordning = barnordning per nivå). Chromet är HELT utanför: det ritas
// per present ovanpå compositen, aldrig ur damage.
void AppWindow::renderContent(gfx::Window& win)
{
    if (!font_ || !images_ || !canvas_)
        return;   // fönstret är mitt i öppning/stängning — inget att rita med
    // Profilerarens innehåll — hela passet, också skadans
    // efterarbete nedan; utanför pumpen öppnar det bilden.
    const detail::WindowPass mät(*this, ProfilePhase::Content);

    // RITYTANS BILDER: vräkningen över budgeten sker
    // HÄR, före all ritning — aldrig mitt i en frame, där en batchad
    // ritning kan peka på texturen. Och en bild som inte hann laddas upp
    // under framens uppladdningsbudget väntar på NÄSTA frame: den begärs när
    // framen är klar, vilken väg den än tog ut (en frame till, aldrig en
    // loop — varje frame laddar upp minst en bild). Båda budgetarna läses
    // här, så ett byte gäller från fönstrets nästa frame.
    images_->trim(App::instance_->canvasMemoryBudget(),
                  App::instance_->canvasUploadBudget());
    // Ritytans resurser släpps också BARA här: det döda
    // (ombyggda chunkar och körningar, rivna rytor) och det som ersattes mitt
    // i förra framen.
    canvas_->trim();
    // …och fältens fria textstorlekar över taket (font_set.h) —
    // samma ställe av samma skäl: ingen batch pekar på en atlas här.
    if (fontSet_)
        fontSet_->trim();
    struct VäntandeBilder {
        AppWindow& w;
        ~VäntandeBilder()
        {
            if (w.images_->uploadsDeferred())
                w.invalidate();
        }
    } väntande{*this};
    // EFTER FRAMEN, vilken väg den än tog ut: de
    // storlekar ritningen upptäckte levereras i pumpens svans (`onResize` —
    // aldrig användarkod i render-callbacken, som på Windows och macOS
    // körs inne i OS:ets storleksloop), och ett fönster som följer innehållet
    // frågar om måttet har ändrats.
    struct EfterFramen {
        AppWindow& w;
        ~EfterFramen()
        {
            w.scheduleResizes();
            w.fitDue();
        }
    } efter{*this};

    Ctx c = makeCtx();
    win.setChromeColors(chromeColorsFrom(c.theme->colors));
    win.setChromeRadius(cornerRadius());   // ramen följer ett rundat hörn

    // Spegla maximerings-läget till maximize-knappens restore-glyf — per
    // repaint, ingen event-bokföring behövs (billig bool-läsning).
    if (topBar_ && topBar_->maxButton())
        topBar_->maxButton()->setRestoreGlyph(win_->maximized());
    const Ctx cch = chromeCtx(c);   // TopBars area (hela klientytan)
    // BILDENS STEG FÖRE kartan och skadeinsamlingen: ett steg som
    // ändrar tillstånd (dockningens modellsynk) invaliderar, och skadan tas
    // med i den här bilden — också för en widget som skadan inte når.
    for (const auto& ch : root_.children())
        stepTree(*ch);
    pushHitRegions(cch);            // kartan följer färsk geometri

    // Genomskinliga fönster ritas alltid fullt: bakgrunds-alfan (0) kan
    // inte återställas per damage-rect med fillRect (alfa 0 ritar inget).
    bool full = fullDamage_ || animating_ || transparent_ || roundedChrome_ ||
                !win.retainedValid();
    fullDamage_ = false;

    // Hela klientytan (fysiska px) — innehållsytan + titelraden. Damage
    // klipps hit; ytan bakom chromet ägs ändå av compositen.
    const gfx::Rectf client{0.0f, 0.0f, c.area.x + c.area.w,
                            c.area.y + c.area.h};

    std::vector<gfx::Rectf> rects;
    if (!full) {
        // Damage genom trädet — SAMMA Ctx-vandring som ritningen:
        // union gammal+ny yta per dirty widget, klippt mot
        // förälderkedjans klipp (start = klientytan).
        for (const auto& ch : root_.children())
            collectDamage(*ch, isChromeRoot(*ch) ? cch : c, client, rects);

        // Finkorniga rects (Widget::invalidateRect — t.ex. caret-blinket):
        // konsumeras här, samma snap/klipp som per-widget-rects.
        for (const auto& r : extraDamage_) {
            const gfx::Rectf dmg = intersectRect(snapOut(r), client);
            if (dmg.w > 0.0f && dmg.h > 0.0f)
                rects.push_back(dmg);
        }
        extraDamage_.clear();

        if (rects.empty())
            return;   // ren present (invalidateOverlay/chrome-hover)

        float area = 0.0f;
        for (const auto& r : rects)
            area += r.w * r.h;
        if (rects.size() > kMaxDamageRects ||
            area > kMaxDamageArea * client.w * client.h)
            full = true;   // administrationen äter vinsten — rita allt
    }

    // Rotens storlek (`root().onResize`) — innehållsytan; rotens `resolve` är
    // 0 × 0. Barnen noteras i vandringarna nedan.
    noteSize(root_, c.area, c.scale);

    if (full) {
        // Klassiska vägen — ritar allt.
        // Transparenta fönster börjar från HELT genomskinligt (alfa 0) —
        // formen är det frame()/widgetarna ritar.
        detail::profileCount(ProfileCount::FullDamage);
        detail::profileCount(ProfileCount::DamageRects);
        extraDamage_.clear();   // full omritning täcker allt
        c.r->clear(transparent_ || roundedChrome_ ? gfx::Color{0, 0, 0, 0}
                                                  : c.theme->colors.windowBg);
        // Rundat fönster: bakgrunden ÄR formen. Ytan börjar helt
        // genomskinlig och fönsterkroppen läggs som en rundad rekt över
        // hela klientytan — hörnpixlarna förblir alfa 0 och skärmen bakom
        // syns igenom. Maximerat ritas fyrkantigt: en båge mot skärmkanten
        // är ett hål, inte en form.
        if (roundedChrome_)
            c.r->fillRoundedRect(client, cornerRadius(), c.theme->colors.windowBg);
        frame(c);
        for (const auto& ch : root_.children())
            drawTree(*ch, isChromeRoot(*ch) ? cch : c);
        recordDamage({client});
        return;
    }

    // Partiell omritning: varje damage-rect byggs upp från grunden inom
    // sitt klipp. Överlappande rects ritar samma pixlar två gånger —
    // deterministisk ritning gör det osynligt (bakgrunden läggs först).
    detail::profileCount(ProfileCount::DamageRects, rects.size());
    for (const auto& dr : rects) {
        c.r->pushClip(dr);
        c.r->fillRect(dr, c.theme->colors.windowBg);   // clear ignorerar klipp
        frame(c);
        for (const auto& ch : root_.children())
            drawTreePartial(*ch, isChromeRoot(*ch) ? cch : c, dr);
        c.r->popClip();
    }
    recordDamage(rects);
}

// STEGVANDRINGEN: `frameStep` för varje synlig widget, en gång per
// bild, före skadeinsamlingen och oavsett skadan och ytan — en dockningsvärd
// i en inaktiv flik synkar och återupptar sin väntande flytt också i en
// partiell bild som inte når den. Ritar ingenting och invaliderar ingenting
// själv, och resolvar ingenting: en ren pekarvandring, så den kostar inget
// märkbart i stora träd.
void AppWindow::stepTree(Widget& w)
{
    if (!w.isVisible())
        return;
    w.frameStep();
    for (const auto& ch : w.children())
        stepTree(*ch);
}

// Rit-vandringen (full väg): widgeten själv först, sedan barnen mot
// childArea med containerns klipp. lastRect_ sparas i FÖNSTER-koordinater
// med widgetens FAKTISKA Ctx — det är sanningen damage-insamlingen läser.
void AppWindow::drawTree(Widget& w, const Ctx& c)
{
    w.dirty_ = false;
    if (!w.isVisible()) {
        w.hasLastRect_ = false;
        // barnen ritas inte heller — städa deras bokföring
        forEachWidget(w, [](Widget& ch) { ch.hasLastRect_ = false; });
        return;
    }
    // Widgetens egen rect bärs i ctx:t under draw() — Draw-anropen utgår
    // från den, så ritkoden slipper både resolve() och c.scale.
    Ctx wc = c;
    wc.box = w.resolve(c);
    noteSize(w, wc.box, c.scale);   // onResize: upptäckten
    if (hasArea(wc.box))            // bildens steg: stepTree, före
        w.draw(wc);
    w.lastRect_    = w.paintBounds(c);
    w.hasLastRect_ = true;

    if (!w.children().empty()) {
        Ctx cc  = c;
        cc.area = w.childArea(c);
        const bool doClip = w.clipsChildren();
        if (doClip)
            c.r->pushClip(w.clipRect(c), w.clipRadius(c));
        // DET FRÄMRE BARNET ritas SIST — barnordningen står
        // orörd, men just det här barnet hamnar överst.
        Widget* fram = frontChildOf(w);
        for (const auto& ch : w.children())
            if (ch != fram)
                drawTree(*ch, cc);
        if (fram)
            drawTree(*fram, cc);
        if (doClip)
            c.r->popClip();
    }
    if (hasArea(wc.box))
        w.drawAfterChildren(wc);   // ovanpå barnen, utanför barnklippet
}

// Rit-vandringen (partiell väg): rita det vars paintBounds skär damage-
// recten; en klippande container vars klippyta inte skär recten kan
// hoppa över hela sitt delträd (oklippta barn kan sticka utanför sin
// förälder och måste alltid besökas).
void AppWindow::drawTreePartial(Widget& w, const Ctx& c, const gfx::Rectf& dmg)
{
    if (!w.isVisible())
        return;   // bokföringen städas av drawTree/collectDamage-vägarna

    Ctx wc = c;
    wc.box = w.resolve(c);   // se drawTree
    noteSize(w, wc.box, c.scale);   // före överlappsprovet, som i drawTree

    const gfx::Rectf pb    = w.paintBounds(c);
    const bool       egen  = overlaps(pb, dmg);
    const bool       ritas = egen && hasArea(wc.box);   // se hasArea
    if (egen) {
        if (ritas)
            w.draw(wc);
        w.lastRect_    = pb;
        w.hasLastRect_ = true;
    }

    if (!w.children().empty()) {
        const bool doClip = w.clipsChildren();
        if (doClip && !overlaps(w.clipRect(c), dmg)) {
            // hela delträdet utanför damage-recten — men efter-barnen-
            // lagret kan ändå skära den (paintBounds gjorde det ovan)
            if (ritas)
                w.drawAfterChildren(wc);
            return;
        }
        Ctx cc  = c;
        cc.area = w.childArea(c);
        if (doClip)
            c.r->pushClip(w.clipRect(c), w.clipRadius(c));
        Widget* fram = frontChildOf(w);   // sist, som i drawTree
        for (const auto& ch : w.children())
            if (ch != fram)
                drawTreePartial(*ch, cc, dmg);
        if (fram)
            drawTreePartial(*fram, cc, dmg);
        if (doClip)
            c.r->popClip();
    }
    // Ovanpå barnen: ritas när NÅGOT i widgetens yta rörts — ett omritat
    // barn under listen får aldrig ligga överst (pb omsluter barnen).
    if (ritas)
        w.drawAfterChildren(wc);
}

// ---------------------------------------------- onResize
//
// UPPTÄCKTEN — i ritvandringen, direkt efter `resolve`: storleken
// finns bara där (layouten är tillståndslös). Ingenting utan en satt handler,
// ingen sidoeffekt i `resolve` och ingen användarkod här. Jämförelsen är EXAKT
// mot den senast LEVERERADE storleken; en ändring köas med en vakt (en ny
// widget på samma adress är ingen dubblett) och med den RITADE storleken, som
// en ny ritning före svansen skriver över (mellanlägen sammanfogas). En dold
// widget ritas inte, och en i en inaktiv dockningsflik (synlig men med tom
// rekt) frågas `shownInTree` — ingen 0 × 0-händelse.
void AppWindow::noteSize(Widget& w, const gfx::Rectf& box, float scale)
{
    if (!w.onResize || scale <= 0.0f)
        return;
    const gfx::Sizef s{box.w / scale, box.h / scale};
    for (ResizeEntry& e : resizeQueue_)
        if (e.w == &w && e.guard.ok()) {
            e.drawn = s;
            return;
        }
    if (w.resizeSeen_ && w.resizeSeen_->w == s.w && w.resizeSeen_->h == s.h)
        return;
    if (!shownInTree(w))
        return;
    std::erase_if(resizeQueue_, [&w](const ResizeEntry& e) { return e.w == &w; });
    resizeQueue_.push_back(ResizeEntry{&w, Widget::HandlerGuard(w), s});
}

// Samma vandring som `drawTree`, utan ritning — den första leveransen före den
// första synliga bildrutan. Dolda delträd hoppas över.
void AppWindow::noteSizesIn(Widget& w, const Ctx& c)
{
    if (!w.isVisible())
        return;
    noteSize(w, w.resolve(c), c.scale);
    if (w.children().empty())
        return;
    Ctx cc  = c;
    cc.area = w.childArea(c);
    for (const auto& ch : w.children())
        noteSizesIn(*ch, cc);
}

// EN leverans per fönster åt gången, med fönstrets livstecken — postad efter
// framen (`renderContent`s scope-vakt) i RAMKÖN (`detail::postFrameWork`):
// körd först i nästa pumpvarvs svans, före appens postade
// funktioner och utan deras budget, och före nästa frame (pumpen sover inte
// med postat arbete).
void AppWindow::scheduleResizes()
{
    if (resizeQueue_.empty() || resizePosted_)
        return;
    resizePosted_ = true;
    const std::weak_ptr<char> liv = lifeToken();
    detail::postFrameWork([this, liv] {
        if (liv.expired())
            return;
        resizePosted_ = false;
        deliverResizes();
    });
}

// LEVERANSEN. Kön tas ut; ett stängt fönster levererar ingenting.
// Per post med hållbar vakt, satt handler, kvar i mitt fönster och fortfarande
// SYNLIG i trädet: storleken räknas OM ur `actualSize()` — har den
// ändrats sedan ritningen (en tidigare handler i samma omgång rörde den)
// väntar posten på sin nästa ritning i stället för att rapportera oritad
// geometri, och lika med den senast levererade är den ingenting
// (A → B → A före svansen). Efter VARJE handler frågas fönstrets liv, OS-
// fönstrets id och en begärd stängning. Efter en körd handler ritas fönstret
// om. KEDJESPÄRREN: omgångar i följd där en handler körde och ingen riktig
// inmatning kom emellan räknas; vid `kResizeChainMax` levereras ingenting
// förrän nästa händelse (loggat en gång) — en svängning eller en växande
// handler blir aldrig en evig frame-loop. Den spärrade storleken skrivs inte
// som sedd: den levereras vid nästa ritning efter en händelse.
void AppWindow::deliverResizes()
{
    std::vector<ResizeEntry> kö = std::exchange(resizeQueue_, {});
    if (kö.empty() || !open_ || pendingClose_)
        return;
    App* const          a   = App::instance_;
    const std::uint64_t seq = a ? a->eventSeq_ : 0;
    if (seq != resizeChainSeq_) {
        resizeChainSeq_ = seq;
        resizeChain_    = 0;
    }
    if (resizeChain_ >= kResizeChainMax) {
        if (resizeChain_ == kResizeChainMax)
            gfx::log("onResize i \"%s\" har hållit fönstret ritande i %d "
                     "bildrutor utan att något annat hänt — levereras inte "
                     "förrän nästa händelse",
                     title_.str().c_str(), kResizeChainMax);
        resizeChain_ = kResizeChainMax + 1;   // loggat — tyst till händelsen
        return;
    }
    const std::weak_ptr<char> liv   = lifeToken();
    const gfx::WindowId       id    = win_->id();
    bool                      körde = false;
    for (ResizeEntry& p : kö) {
        if (!p.guard.ok() || p.w->liveOwner() != this || !p.w->onResize ||
            !shownInTree(*p.w))
            continue;
        const gfx::Sizef nu = p.w->actualSize();
        if (nu.w != p.drawn.w || nu.h != p.drawn.h)
            continue;   // oritad geometri — nästa ritning noterar den
        if (p.w->resizeSeen_ && p.w->resizeSeen_->w == nu.w &&
            p.w->resizeSeen_->h == nu.h)
            continue;
        ResizeArgs args;
        args.first        = !p.w->resizeSeen_;
        args.previous     = p.w->resizeSeen_.value_or(gfx::Sizef{});
        p.w->resizeSeen_  = nu;
        körde             = true;
        p.w->fireEvent(p.w->onResize, args);
        if (liv.expired())
            return;   // handlern rev fönstret
        if (!open_ || pendingClose_ || win_->id() != id)
            break;
    }
    if (!körde) {
        resizeChain_ = 0;
        return;
    }
    ++resizeChain_;
    if (open_)
        invalidate();   // en användar-handler har kört (husets regel)
}

// Damage-insamlingen: trädordning, samma Ctx-vandring som
// ritningen. Damage-rects klipps mot förälderkedjans klipp (clipAcc) —
// annars spökpixlar utanför en scrollad/klippt panel. Dolda grenar
// besöks också: deras dirty-flaggor och lastRect_ ska städas.
void AppWindow::collectDamage(Widget& w, const Ctx& c,
                              const gfx::Rectf& clipAcc,
                              std::vector<gfx::Rectf>& rects)
{
    if (w.dirty_) {
        w.dirty_ = false;

        gfx::Rectf dmg{};
        bool       has = false;
        if (w.hasLastRect_) {
            dmg = w.lastRect_;
            has = true;
        }
        if (w.isVisible()) {
            const gfx::Rectf pb = w.paintBounds(c);
            dmg = has ? unionRect(dmg, pb) : pb;
            has = true;
        } else {
            w.hasLastRect_ = false;   // ritas inte längre — bara städas
        }
        if (has) {
            dmg = intersectRect(snapOut(dmg), clipAcc);
            if (dmg.w > 0.0f && dmg.h > 0.0f)
                rects.push_back(dmg);
        }
    }

    if (w.children().empty())
        return;
    Ctx cc  = c;
    cc.area = w.childArea(c);
    gfx::Rectf acc = clipAcc;
    if (w.clipsChildren())
        acc = intersectRect(acc, snapOut(w.clipRect(c)));
    for (const auto& ch : w.children())
        collectDamage(*ch, cc, acc, rects);
}

// Damage-debug: kom ihåg vad som just ritades om så overlayen kan visa
// det. En färg per OMRITNING (inte per rect) — cyklingen gör upprepade
// omritningar av samma yta synliga.
void AppWindow::recordDamage(const std::vector<gfx::Rectf>& rects)
{
    if (!debugDamage_ || rects.empty())
        return;
    const auto color =
        static_cast<std::uint8_t>(damageGen_++ % kDamageColorCount);
    const std::uint64_t now = gfx::ticksMs();
    for (const auto& r : rects)
        damageMarks_.push_back(DamageMark{r, color, now});
    // Tak mot obegränsad tillväxt (animation + debug igång länge)
    constexpr std::size_t kMaxMarks = 256;
    if (damageMarks_.size() > kMaxMarks)
        damageMarks_.erase(damageMarks_.begin(),
                           damageMarks_.end() - kMaxMarks);
    overlayAnimating_ = true;   // presents rullar medan markörer tonar
}

// Ramverkets overlay-lager: tunn kontur + ~10 % fyllning per damage-rect,
// linjär uttoning. Ritas efter compositen, direkt i backbuffern — kan per
// konstruktion inte påverka retained-innehållet eller damage-beräkningen.
void AppWindow::drawDebugDamage(const Ctx& c)
{
    if (damageMarks_.empty()) {
        overlayAnimating_ = false;
        return;
    }

    const std::uint64_t now = gfx::ticksMs();
    damageMarks_.erase(
        std::remove_if(damageMarks_.begin(), damageMarks_.end(),
                       [now](const DamageMark& m) {
                           return now - m.bornMs >= kDamageFadeMs;
                       }),
        damageMarks_.end());

    for (const auto& m : damageMarks_) {
        const float fade =
            1.0f - static_cast<float>(now - m.bornMs) /
                       static_cast<float>(kDamageFadeMs);
        gfx::Color col = kDamageColors[m.color];
        col.a = static_cast<std::uint8_t>(26.0f * fade);    // ~10 % fyllning
        c.r->fillRect(m.rect, col);
        col.a = static_cast<std::uint8_t>(230.0f * fade);   // skarp kontur
        c.r->strokeRect(m.rect, col);
    }

    // Sista markören uttonad → tillbaka till att sova i waitEvent
    overlayAnimating_ = !damageMarks_.empty();
}

// OVERLAY-VANDRINGEN. Den körs vid VARJE present, så den
// är tvådelad: först en billig vandring som bara frågar `isVisible()` och
// `hasOverlay()` (ingen geometri — och ingenting mer när ingen widget har ett
// lager, det vanliga fallet), sedan per träff den geometri som behövs.
//
// Klippet byggs med RENDERERNS stack och samma steg som `drawTree` tar:
// förfäderna yttersta först, var och en med `clipRect`/`clipRadius` i sitt
// eget Ctx och barnens area ur `childArea` — så lagret klipps exakt som
// widgetens innehåll, rundade hörn inräknade. drag_drop.cpp snittar i
// stället rektanglarna för hand, för den RITAR en ram inom snittet; här är
// det klippet själv som ska gälla för det widgeten ritar.
//
// Basklippet är ytan trädet ritas i: innehållsytan, eller klientytan för
// namnlistens träd. Utan det hade en widget direkt under roten — som inte
// klipper — kunnat rita sitt lager över namnlisten, som innehållet aldrig
// når (chromet ritas ovanpå compositen).
//
// DET FRÄMRE BARNET TÄCKER: ett lager i ett
// SYSKON-delträd till en förälders `frontChild` ligger UNDER det främre
// barnet, men lagren ritas efter hela compositen — en rityta i huvudytan
// hade annars ritat sina markeringar rakt genom en öppen overlay. Lagret
// ritas därför i basytan MINUS varje sådant främre barns yta: upp till fyra
// rektanglar per täckande barn, med widgetens `drawOverlay` en gång per bit
// (lagret är ren ritning, så en bit till ritar bara mer av samma bild).
namespace {

// R minus O — de delar av R som ligger ovanför, under, till vänster och till
// höger om O (högst fyra, inga överlapp). Utan snitt: R själv.
std::vector<gfx::Rectf> utom(const gfx::Rectf& r, const gfx::Rectf& o)
{
    const float x0 = std::max(r.x, o.x);
    const float y0 = std::max(r.y, o.y);
    const float x1 = std::min(r.x + r.w, o.x + o.w);
    const float y1 = std::min(r.y + r.h, o.y + o.h);
    if (x1 <= x0 || y1 <= y0)
        return {r};
    std::vector<gfx::Rectf> ut;
    if (y0 > r.y)
        ut.push_back({r.x, r.y, r.w, y0 - r.y});
    if (y1 < r.y + r.h)
        ut.push_back({r.x, y1, r.w, r.y + r.h - y1});
    if (x0 > r.x)
        ut.push_back({r.x, y0, x0 - r.x, y1 - y0});
    if (x1 < r.x + r.w)
        ut.push_back({x1, y0, r.x + r.w - x1, y1 - y0});
    return ut;
}

// Ligger `w` i delträdet under `rot` (eller är den)?
bool iDelträdet(const Widget& w, const Widget& rot)
{
    for (const Widget* p = &w; p; p = p->parent())
        if (p == &rot)
            return true;
    return false;
}

} // namespace

void AppWindow::drawWidgetOverlays(const Ctx& c)
{
    std::vector<Widget*> lager;
    const std::function<void(Widget&)> samla = [&](Widget& w) {
        for (Widget* ch : w.children()) {
            if (!ch->isVisible())
                continue;   // ett dolt delträd ritas inte, och inte dess lager
            if (ch->hasOverlay())
                lager.push_back(ch);
            samla(*ch);
        }
    };
    samla(root_);
    if (lager.empty())
        return;

    const Ctx cch = chromeCtx(c);
    std::vector<Widget*> kedja;
    for (Widget* w : lager) {
        kedja.clear();
        const Widget* top = w;
        for (Widget* p = w->parent(); p && p != &root_; p = p->parent()) {
            kedja.push_back(p);
            top = p;
        }
        Ctx wc = isChromeRoot(*top) ? cch : c;
        const gfx::Rectf bas = wc.area;
        c.r->pushClip(wc.area);
        int nivåer = 1;
        std::vector<gfx::Rectf> täckt;   // främre barn ovanför lagret
        for (auto it = kedja.rbegin(); it != kedja.rend(); ++it) {
            if ((*it)->clipsChildren()) {
                c.r->pushClip((*it)->clipRect(wc), (*it)->clipRadius(wc));
                ++nivåer;
            }
            wc.area = (*it)->childArea(wc);
            Widget* fram = frontChildOf(**it);
            if (fram && fram->isVisible() && !iDelträdet(*w, *fram))
                täckt.push_back(fram->resolve(wc));
        }
        wc.box = w->resolve(wc);
        // Utan yta inget lager (hasArea) — men klippnivåerna poppas nedan.
        if (hasArea(wc.box) && täckt.empty()) {
            w->drawOverlay(wc);
        } else if (hasArea(wc.box)) {
            std::vector<gfx::Rectf> bitar{bas};
            for (const gfx::Rectf& o : täckt) {
                std::vector<gfx::Rectf> kvar;
                for (const gfx::Rectf& b : bitar)
                    for (const gfx::Rectf& d : utom(b, o))
                        kvar.push_back(d);
                bitar = std::move(kvar);
            }
            for (const gfx::Rectf& b : bitar) {
                c.r->pushClip(b);
                w->drawOverlay(wc);
                c.r->popClip();
            }
        }
        while (nivåer-- > 0)
            c.r->popClip();
    }
}

Widget* AppWindow::widgetAt(float mx, float my, const Ctx& c)
{
    // Rotens barn bakifrån: senast tillagda ligger överst. TopBar (första
    // barnet) hit-testas mot chrome-ctx:t — dess remsa ligger utanför
    // innehållsytan så ordningen spelar ingen roll för överlapp.
    const Ctx cch = chromeCtx(c);
    const auto& top = root_.children();
    for (auto it = top.rbegin(); it != top.rend(); ++it)
        if (Widget* hit = widgetAtRec(**it, isChromeRoot(**it) ? cch : c, mx, my))
            return hit;
    return nullptr;
}

// Rekursiv hit-test: barnen först (de ritas ovanpå), bakifrån per nivå.
// Klippta barn träffas inte utanför klippytan; hit-testen följer widgetens
// REKTANGEL (resolve), aldrig den ritade formen.
Widget* AppWindow::widgetAtRec(Widget& w, const Ctx& c, float mx, float my)
{
    if (!w.isVisible())
        return nullptr;

    if (!w.children().empty() &&
        (!w.clipsChildren() || w.clipRect(c).contains(mx, my))) {
        Ctx cc  = c;
        cc.area = w.childArea(c);
        // Det främre barnet träffas FÖRST — det ritas överst.
        Widget* fram = frontChildOf(w);
        if (fram)
            if (Widget* hit = widgetAtRec(*fram, cc, mx, my))
                return hit;
        for (auto it = w.children().rbegin(); it != w.children().rend(); ++it)
            if (*it != fram)
                if (Widget* hit = widgetAtRec(**it, cc, mx, my))
                    return hit;
    }
    return w.resolve(c).contains(mx, my) ? &w : nullptr;
}

// Det främre barnet (`Widget::frontChild`) — bara när det verkligen är ett av
// mina barn; ett annat svar ignoreras.
Widget* AppWindow::frontChildOf(const Widget& w)
{
    Widget* fram = w.frontChild();
    return fram && fram->parent() == &w ? fram : nullptr;
}

// Ctx:t en widget resolvar mot: förälderkedjans childArea-vandring från
// roten ner till widgetens förälder — kedjan slutar vid FÖNSTERROTEN per
// kontrakt (koordinater vandrar aldrig över fönstergränsen; fönster äger
// fönster via owner(), som aldrig bär koordinater). Behavior-krokar
// (mouseDown/mouseDrag/caretBlink/mouseWheel) får ALLTID sitt Ctx härifrån.
Ctx AppWindow::ctxFor(const Widget& w)
{
    std::vector<const Widget*> chain;
    const Widget* top = &w;
    for (const Widget* p = w.parent(); p && p != &root_; p = p->parent()) {
        chain.push_back(p);
        top = p;
    }
    Ctx c = makeCtx();
    // Chrome-trädet (TopBar + dess barn, programikonen) placeras mot hela
    // klientytan — samma växling som rit-/hit-test-vandringarna gör per
    // rotbarn.
    if (isChromeRoot(*top))
        c = chromeCtx(c);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        c.area = (*it)->childArea(c);
    return c;
}

// SYNS widgeten i trädet? (se app_window.h) — tooltipens och
// standardknappens prövning.
bool AppWindow::shownInTree(const Widget& w)
{
    for (const Widget* x = &w; x; x = x->parent()) {
        if (!x->isVisible())
            return false;
        if (x->parent() && !x->parent()->showsChildren())
            return false;
    }
    return true;
}

// FOKUSOMBUDET: en komposit som har ett ombud tar aldrig fokus
// själv — det går vidare till ombudet, och vidare om ombudet i sin tur har
// ett. Bara en ÄTTLING godtas (kontraktet i widget.h), så varje steg går
// DJUPARE i ett ändligt träd och kedjan kan inte loopa.
Widget* AppWindow::proxyTarget(Widget* w)
{
    while (w) {
        Widget* const ombud = w->focusProxy();
        if (!ombud || ombud == w)
            break;
        bool ättling = false;
        for (const Widget* p = ombud->parent(); p; p = p->parent())
            if (p == w) {
                ättling = true;
                break;
            }
        if (!ättling)
            break;
        w = ombud;
    }
    return w;
}

// Flyttar fokus (klick eller Tab). Flaggan sätts före kroken så
// focusGained/focusLost ser sitt nya tillstånd; båda widgetarnas ytor
// invalidateras så fokusringen syns/släcks (per-widget-damage — regel 1).
// Textwidgets (wantsTextInput): OS-textinmatningen och caret-timern följer
// fokuset — timern ägs HÄR, widgeten får bara blinkfasen via caretBlink.
void AppWindow::setFocus(Widget* w)
{
    // FOKUSOMBUDET (`proxyTarget`). Jämförelsen nedan sker EFTER
    // upplösningen: ett klick på stegaren medan fältet redan har fokus är
    // ingen fokusändring alls.
    w = proxyTarget(w);

    if (focusedWidget_ == w)
        return;
    Widget* const från = focusedWidget_;   // för fokushändelserna (sist)
    // STANDARDKNAPPEN följer fokus: en fokuserad knapp är
    // standard, och när fokus lämnar den går rollen tillbaka till
    // acceptknappen — båda ritas om när de skiljer sig (nedan, före
    // fokushändelserna). Krokarna emellan kör ingen användarkod.
    Button* const förraStandard = defaultButton();
    if (focusedWidget_) {
        Widget* old   = focusedWidget_;
        old->focused_ = false;
        if (old->wantsTextInput()) {
            win_->stopTimer(kCaretTimerId);
            win_->setTextInputActive(false);
            caretPhase_ = false;
            const Ctx c = ctxFor(*old);   // widgetens containers Ctx
            old->caretBlink(c, false);
        }
        old->focusLost();
        old->invalidate();
    }
    focusedWidget_ = w;
    if (w) {
        w->focused_ = true;
        if (w->wantsTextInput()) {
            win_->setTextInputActive(true);
            win_->startTimer(kCaretTimerId, kCaretBlinkMs);
            caretPhase_ = true;
            const Ctx c = ctxFor(*w);
            w->caretBlink(c, true);
        }
        w->focusGained();
        w->invalidate();
    }
    defaultButtonChanged(förraStandard);
    postFocusEvents(från, w);
}

// FOKUS INOM — kontraktet står vid `Widget::onFocus` i widget.h.
//
// KEDJORNA är jag och mina förfäder upp till, men inte med, fönstrets rot.
// Det som står i BÅDA kedjorna har inte bytt "fokus inom" och fyrar inget;
// resten av den gamla kedjan tappade fokus (innerst först, kedjans egen
// ordning) och resten av den nya fick det (ytterst först, baklänges). Båda
// kedjorna räknas NU, med trädet som det är: `detachTree` anropar oss före
// trädmutationen, så en widget på väg ut har ännu sina förfäder.
//
// I FÖNSTRETS KÖ och POSTAT: varje händelse bär en `HandlerGuard` tagen vid
// bytet, så en widget som dör, lossas eller tappar sitt fönster innan
// leveransen får ingenting. Vilka som kommer med avgörs vid BYTET — en
// handler som sätts mellan bytet och svansen får inte ett byte som redan
// skett. En leverans postas per byte; den som hittar kön tom gör ingenting.
void AppWindow::postFocusEvents(Widget* från, Widget* till)
{
    const auto kedja = [this](Widget* w) {
        std::vector<Widget*> k;
        for (Widget* p = w; p && p != &root_; p = p->parent())
            k.push_back(p);
        return k;
    };
    const std::vector<Widget*> ut = kedja(från);
    const std::vector<Widget*> in = kedja(till);
    const auto finns = [](const std::vector<Widget*>& k, const Widget* w) {
        return std::find(k.begin(), k.end(), w) != k.end();
    };

    // CONTAINERNS KROK (`Widget::childFocusChanged`) — med SAMMA
    // kedjor och SYNKRONT, före något postas: föräldern till varje widget
    // vars fokus inom bytte får veta det. Ingen användarkod körs och trädet
    // ändras inte (kontraktet i widget.h), så kedjorna står sig hela vägen.
    for (Widget* w : ut)
        if (!finns(in, w) && w->parent())
            w->parent()->childFocusChanged(*w);
    for (Widget* w : in)
        if (!finns(ut, w) && w->parent())
            w->parent()->childFocusChanged(*w);

    FocusQueue&       kö   = *focusQueue_;
    const std::size_t före = kö.size();
    for (Widget* w : ut)
        if (!finns(in, w) && w->onFocusLost)
            kö.push_back({w, Widget::HandlerGuard(*w), false});
    for (auto it = in.rbegin(); it != in.rend(); ++it)
        if (!finns(ut, *it) && (*it)->onFocus)
            kö.push_back({*it, Widget::HandlerGuard(**it), true});
    if (kö.size() == före)
        return;

    glaze::app().post([this, kön = focusQueue_, liv = lifeToken()] {
        deliverFocusEvents(this, liv, *kön);
    });
}

// LEVERANSEN — kön framifrån, en händelse i taget. Händelsen KOPIERAS ut och
// tas bort FÖRE handlern: en handler som pumpar nästlat kör en andra
// leverans, som då börjar på nästa händelse och aldrig levererar samma två
// gånger. Varje händelses vakt prövas FÖRE dess leverans, och den frågar
// fönstrets liv också — en tidigare handler i samma kö får ha rivit fönstret,
// och då slopas resten (kön själv lever i leveransens `shared_ptr`).
// `fönster` rörs bara när `liv` lever.
void AppWindow::deliverFocusEvents(AppWindow* fönster,
                                   const std::weak_ptr<char>& liv,
                                   FocusQueue& kö)
{
    bool körde = false;
    while (!kö.empty()) {
        const FocusEvent h = kö.front();
        kö.pop_front();
        if (!h.guard.ok())
            continue;   // död, lossad, eller fönstret dog
        FocusArgs e;
        if (h.gained && h.w->onFocus) {
            h.w->onFocus(h.w, e);
            körde = true;
        } else if (!h.gained && h.w->onFocusLost) {
            h.w->onFocusLost(h.w, e);
            körde = true;
        }
    }
    if (körde && !liv.expired())
        fönster->invalidate();   // handlers har kört — som efter ett klick
}

// COMMITTA DET SOM REDIGERAS — kontraktet står i app_window.h.
// Kedjan och vakterna tas FÖRST: en commits handler får flytta fokus, riva
// en widget längre upp eller hela fönstret.
//
// SYNKRONT ÄVEN NÄR EN HANDLER FLYTTAR FOKUS: en handler i kedjan (ett
// editorfälts egen `onCommit`) som flyttar fokus gör att kompositen ovanför
// committar i sin `focusLost` — och den POSTAR (`postEvent`). Medan djupet är
// > 0 hamnar sådana händelser i fönstrets commit-kö, och den töms framifrån
// innan vi återvänder (kontraktet och ordningen står i app_window.h).
// Omritningen sker bara om en handler faktiskt körde (`handlerRuns_`).
void AppWindow::commitEdits()
{
    struct Länk {
        Widget*              w;
        Widget::HandlerGuard guard;
    };
    std::vector<Länk> kedja;
    for (Widget* w = focusedWidget_; w && w != &root_; w = w->parent())
        kedja.push_back({w, Widget::HandlerGuard(*w)});
    const std::weak_ptr<char> fönster  = lifeToken();
    const std::uint64_t       körtFöre = handlerRuns_;
    // DJUPET räknas ner när vi lämnar — också genom ett undantag ur en
    // handler — och bara om fönstret fortfarande lever att räkna i.
    struct Djup {
        AppWindow*          w;
        std::weak_ptr<char> liv;
        ~Djup()
        {
            if (!liv.expired())
                --w->commitDepth_;
        }
    };
    ++commitDepth_;
    const Djup djup{this, fönster};
    for (const Länk& l : kedja) {
        if (l.guard.ok())
            l.w->commitEdit();
        if (fönster.expired())
            return;   // en handler rev fönstret — inget kvar att röra
    }
    if (!drainCommitQueue())
        return;
    if (handlerRuns_ != körtFöre)
        invalidate();
}

// KÖN FRAMIFRÅN — händelsen tas ut FÖRE anropet, så en rekursiv
// `commitEdits()` eller en nästlad pump som tömmer samma kö börjar på nästa.
bool AppWindow::drainCommitQueue()
{
    const std::weak_ptr<char> liv = lifeToken();
    while (!commitQueue_.empty()) {
        std::function<void()> händelse = std::move(commitQueue_.front());
        commitQueue_.pop_front();
        händelse();   // vaktad av `Widget::postEvent`
        if (liv.expired())
            return false;
    }
    return true;
}

// Grab-stacken: toppen vinner (meny → undermeny). Samma widget kan bara
// stå en gång — grabInput flyttar den till toppen.
void AppWindow::pushGrab(Widget* w)
{
    removeGrab(w);
    grabStack_.push_back(w);
}

void AppWindow::removeGrab(Widget* w)
{
    grabStack_.erase(std::remove(grabStack_.begin(), grabStack_.end(), w),
                     grabStack_.end());
}

// Ligger w inom grab-toppens delträd? Tryck där är INTE "utanför"
// (klick i menyn själv ska inte stänga den).
bool AppWindow::inGrabbedSubtree(Widget* w) const
{
    if (grabStack_.empty())
        return false;
    const Widget* top = grabStack_.back();
    for (const Widget* p = w; p; p = p->parent())
        if (p == top)
            return true;
    return false;
}

// TRYCKEN OCH CAPTUREN GICK FÖRLORADE utan ett släpp
// — se app_window.h. EN väg för varje avbrott, och HELA
// BOKFÖRINGEN NOLLAS FÖRE NÅGON KROK:
// drag-and-drop-avbrottets handlers och
// `mouseCancel` är användarkod, och en nästlad pump där hade annars levererat
// ett släpp till en gammal mottagare — och en ny capture eller ett nytt tryck
// som en handler själv satte hade tagits bort efteråt. Mottagarna sparas först
// med var sin anknytningsvakt och knappen de hade fått släppet för: den
// tryckta `Left`, en ANNAN fångande `Left` med vänsterknappen nere och annars
// `None` (`buttonsDown_` — den tryckta kan redan ha lossats mitt i trycket),
// mitten- och högertrycket `Middle`/`Right`. Var och en får beskedet bara om
// vakten står sig och den inte har tagit sin roll igen under avbrottet; efter
// varje frågas fönstrets livstecken, och ett besked som körde en handler ritar
// om hela fönstret. Ingen `…Up` — inget släpp har skett.
bool AppWindow::dropPointer()
{
    const std::weak_ptr<char> liv = lifeToken();
    struct Mottagare {
        Widget*              w;
        Widget::HandlerGuard guard;
        gfx::MouseButton     knapp;
        bool                 fångad;   // capturens besked (inte ett tryck)
    };
    std::vector<Mottagare> mottagare;
    const bool vänster =
        (buttonsDown_ & mouseBit(gfx::MouseButton::Left)) != 0;
    if (Widget* p = pressedWidget_)
        mottagare.push_back({p, Widget::HandlerGuard(*p),
                             gfx::MouseButton::Left, false});
    if (Widget* f = capturedWidget_; f && f != pressedWidget_)
        mottagare.push_back(
            {f, Widget::HandlerGuard(*f),
             vänster ? gfx::MouseButton::Left : gfx::MouseButton::None, true});
    if (Widget* m = middleWidget_)
        mottagare.push_back({m, Widget::HandlerGuard(*m),
                             gfx::MouseButton::Middle, false});
    if (Widget* r = rightWidget_)
        mottagare.push_back({r, Widget::HandlerGuard(*r),
                             gfx::MouseButton::Right, false});
    // APPENS TRYCK — målen får `onMouseCancel` EFTER widgetarnas
    // egna avbrott, och bokföringen nollas här med resten.
    struct AppMottagare {
        Widget*              w;
        Widget::HandlerGuard guard;
        gfx::MouseButton     knapp;
    };
    std::vector<AppMottagare> appMottagare;
    for (const gfx::MouseButton b : {gfx::MouseButton::Left,
                                     gfx::MouseButton::Middle,
                                     gfx::MouseButton::Right}) {
        // Trycket är SLUT — också ett vars `onMouseDown` kör just nu.
        if (Widget* const w = appPressFor(b)->end())
            appMottagare.push_back({w, Widget::HandlerGuard(*w), b});
    }

    capturedWidget_ = nullptr;
    if (pressedWidget_) {
        pressedWidget_->pressed_ = false;
        pressedWidget_->invalidate();   // tryckutseendet slocknar
        pressedWidget_ = nullptr;
    }
    middleWidget_ = nullptr;
    rightWidget_  = nullptr;
    ++middleSeq_;   // trycken är SLUT — också ett som en krok routar just nu
    ++rightSeq_;    // (`routeAuxDown`): det återställs inte när kroken svarar
    buttonsDown_  = 0;

    dragCancel();   // källans/målets handlers
    if (liv.expired())
        return false;
    for (const Mottagare& m : mottagare) {
        if (!m.guard.ok())
            continue;   // död, lossad eller i ett annat fönster sedan bilden
        const bool igen =
            m.fångad                         ? capturedWidget_ == m.w
            : m.knapp == gfx::MouseButton::Left ? pressedWidget_ == m.w
                                                : auxWidget(m.knapp) == m.w;
        if (igen)
            continue;   // tog sin roll igen under avbrottet — den står kvar
        const bool körde = m.w->mouseCancel(m.knapp);
        if (liv.expired())
            return false;
        if (körde)
            invalidate();   // en handler kördes — som efter ett klick
    }
    for (const AppMottagare& m : appMottagare) {
        if (!m.guard.ok() || !m.w->onMouseCancel)
            continue;
        if (appPressFor(m.knapp)->w == m.w)
            continue;   // ett nytt tryck på samma mål under avbrottet — det lever
        m.w->fireMouse(m.w->onMouseCancel, ctxFor(*m.w), mousePosition(),
                       m.knapp);
        if (liv.expired())
            return false;
        invalidate();   // en handler kördes
    }
    return true;
}

// CAPTURE ÖVER FÖNSTERGRÄNSEN — se app_window.h. OS-
// capturen har ETT fönster: lyckas övertagandet släpper varje annat av
// appens fönster sitt tryck, för ett kvarstående `pressedWidget_` där hade
// fått nästa rörelse över det fönstret som ett drag. Bokföringen sätts FÖRE
// de andra fönstrens krokar — OS:et levererar redan trycket hit, och en
// nästlad pump i en krok ska hitta mottagaren. Registret kopieras och varje
// fönster prövas mot det levande registret före anropet.
//
// ÅTERVALIDERINGEN: en krok i ett annat fönster
// kan ha stängt det här fönstret, flyttat eller rivit `w`, eller tagit
// OS-capturen vidare till ett tredje fönster (ett eget övertagande, som då
// släppte vår explicita capture). Övertagandet gäller bara så länge fönstret
// är öppet och inte på väg att stängas, `w` sitter kvar här med capturen och
// fönstret håller OS-capturen för trycket — och det prövas efter VARJE krok,
// inte bara sist: har trycket gått vidare får slingan inte fortsätta släppa
// de andra fönstrens tryck, för ett av dem kan vara den nya mottagaren.
// Annars STÄDAS övertagandet: den explicita capturen tas tillbaka om den
// fortfarande är `w`s, och OS-capturen släpps om fönstret håller den —
// trycket hade annars stått kvar här utan någon som tar emot det. Svaret är
// då false.
bool AppWindow::takePointerCapture(Widget* w)
{
    if (!open_ || pendingClose_ || !w || !win_->captureMouse())
        return false;
    const std::weak_ptr<char>  liv  = lifeToken();
    const Widget::HandlerGuard vakt(*w);
    const auto                 vårt = [this, w, &vakt] {
        return open_ && !pendingClose_ && vakt.ok() && capturedWidget_ == w &&
               win_->hasMouseCapture();
    };
    capturedWidget_                     = w;
    const std::uint8_t            före  = buttonsDown_;
    const std::vector<AppWindow*> andra = glaze::app().windows();
    for (AppWindow* o : andra) {
        if (o == this)
            continue;
        const auto& nu = glaze::app().windows();
        if (std::find(nu.begin(), nu.end(), o) == nu.end())
            continue;   // stängd och borttagen under en tidigare krok
        // KNAPPARNA följer trycket hit — FÖRE det andra
        // fönstrets krokar, som bokföringen: släppet kommer hit och släcker
        // dem, och ett avbrott här ger den fångande `Left`.
        buttonsDown_ =
            static_cast<std::uint8_t>(buttonsDown_ | o->buttonsDown_);
        o->dropPointer();
        if (liv.expired())
            return false;   // fönstret dog — OS-capturen med dess HWND
        if (!vårt())
            break;
    }
    if (vårt())
        return true;
    if (capturedWidget_ == w)
        capturedWidget_ = nullptr;
    if (win_->hasMouseCapture())
        win_->releaseMouseCapture();
    buttonsDown_ = före;   // trycket kom aldrig hit
    return false;
}

// Blink-omstart med synlig fas (Widget::restartCaretBlink när careten
// flyttats/text skrivits): startTimer med samma id börjar om perioden.
void AppWindow::restartCaretTimer(Widget* w)
{
    if (w != focusedWidget_ || !w->wantsTextInput())
        return;
    win_->startTimer(kCaretTimerId, kCaretBlinkMs);
    caretPhase_ = true;
}

// Fokuslistan: DJUPET-FÖRST över trädet — översta nivån i
// adopt-/add-ordning, in i varje container i dess barnordning. Dolda
// grenar ger inga Tab-stopp.
// FÖNSTERSTÄDNINGEN vid borttagning. Kontraktet och den bindande
// ordningen står i app_window.h; här är de tre sakerna som är lätta att
// missa:
//
//   1. `setFocus(nullptr)` FÖRST och innan trädet rörs — den vandrar
//      förälderkedjan (`ctxFor`) för att släcka careten, och stoppar
//      caret-timern + `setTextInputActive(false)` på köpet.
//   2. DAMAGE: widgetens `lastRect_` är de pixlar den SENAST ritade på.
//      Lämnar den trädet måste den ytan ritas om av någon annan, så recten
//      skördas till `extraDamage_`. Utan det står ett spöke kvar.
//   3. TILLSTÅNDSFLAGGORNA nollas per nod. Ett träd som adopteras tillbaka
//      ska inte komma tillbaka hovrat eller nedtryckt.
bool AppWindow::isBuiltinMember(const Widget& w) const
{
    return &w == static_cast<const Widget*>(topBar_) || &w == &menuBar ||
           &w == &statusBar || &w == &appIcon;
}

bool AppWindow::isChromeRoot(const Widget& w) const
{
    return &w == static_cast<const Widget*>(topBar_) || &w == &appIcon;
}

// Tre tillstånd: INGEN ikon (`noIcon` — tom lista), appens
// EGEN bild, eller FÖRVALET med `programFirst` — exe:ns och paketets egen
// ikon vinner då över ramverkets (`detail::osIcon`, samma regel som
// namnlistens ritning). `appIcon.hide()` rör inte OS-ikonen.
void AppWindow::applyWindowIcon()
{
    if (popup_ || offscreen_)
        return;   // ingen aktivitetsfältspost att visa den i
    gfx::WindowIcon ikon;
    if (!appIcon.isNoIcon())
        ikon = detail::osIcon(appIcon.icon(), appIcon.smallIcon());
    win_->setIcon(ikon);
}

void AppWindow::detachTree(Widget& w)
{
    // Ligger `x` i delträdet under `w`?
    auto iSubtree = [&w](const Widget* x) {
        for (const Widget* p = x; p; p = p->parent())
            if (p == &w)
                return true;
        return false;
    };

    // STANDARDKNAPPEN: lämnar
    // acceptknappen eller den fokuserade knappen trädet kan rollen gå till en
    // ANNAN knapp, också i en annan panel — den som står kvar ritas om.
    // Före mutationen, medan kedjan är hel; ingen användarkod.
    defaultButtonMayChange(w);
    if (focusedWidget_ && iSubtree(focusedWidget_))
        setFocus(nullptr);
    if (pressedWidget_ && iSubtree(pressedWidget_))
        pressedWidget_ = nullptr;
    for (ClickTrack& t : clicks_)
        if (t.widget && iSubtree(t.widget))
            t.widget = nullptr;   // inget halvt dubbelklick
    // Mitten- och högertrycket avbryts
    // TYST, som trycket ovan: varken `…Up` eller `mouseCancel` — ingen
    // användarkod under en trädmutation. Tangentminnets länkar behöver ingen
    // rad: deras vakter slopar en widget som lossats.
    if (middleWidget_ && iSubtree(middleWidget_))
        middleWidget_ = nullptr;
    if (rightWidget_ && iSubtree(rightWidget_))
        rightWidget_ = nullptr;
    // …och appens tryck: målet får varken `onMouseUp` eller
    // `onMouseCancel` — samma tystnad, och sekvensen stegas så att en
    // `onMouseDown` som kör just nu ser att trycket är slut.
    for (AppPress& p : appPress_)
        if (p.w && iSubtree(p.w))
            p.end();
    // Styrplattans gest släpps lika tyst — ingen Cancel till en
    // widget som just lämnar trädet. Nästa Update väljer om, som en Begin.
    if (gestureWidget_ && iSubtree(gestureWidget_))
        gestureWidget_ = nullptr;
    if (capturedWidget_ && iSubtree(capturedWidget_))
        capturedWidget_ = nullptr;
    grabStack_.erase(std::remove_if(grabStack_.begin(), grabStack_.end(),
                                    iSubtree),
                     grabStack_.end());

    // DRAG-SESSIONEN — dess två pekare är de enda i fönstret som
    // en handler MITT I ett drag kan få att dingla, och det här är stället
    // där det upptäcks. INGEN användarkod körs härifrån (samma gräns som
    // resten av städningen): ett mål som lämnar trädet får bara sin
    // `dragLeave`-krok — widgetens egen städning, `focusLost`-prejudikatet —
    // och en KÄLLA som lämnar trädet avslutar draget TYST. Appen rev den
    // själv och vet om det; att fyra dess `onDragEnd` mitt i ett `unadopt`
    // hade varit en andra plats i ramverket där användarkod körs synkront
    // under en trädmutation.
    if (drag_.target && iSubtree(drag_.target)) {
        Widget* t    = drag_.target;
        drag_.target = nullptr;
        drag_.effect = DragEffect::None;
        t->dragLeave();
        invalidateOverlay();
    }
    // TOOLTIPEN: en källa som lämnar trädet får inte stå kvar som
    // pekare — popupen stängs och väntan släpps. Ingen användarkod körs.
    if ((tip_.source && iSubtree(tip_.source)) ||
        (tip_.shown && iSubtree(tip_.shown)))
        toolTipCancel(false);
    if (tip_.blocked && iSubtree(tip_.blocked))
        tip_.blocked = nullptr;

    if (drag_.source && iSubtree(drag_.source)) {
        drag_.source = nullptr;
        if (drag_.phase == DragPhase::Pending) {
            drag_ = DragSession{};
        } else if (drag_.phase == DragPhase::Active) {
            if (Widget* t = drag_.target) {
                t->dropEffect_ = DragEffect::None;
                t->invalidate();
                t->dragLeave();
            }
            drag_ = DragSession{};
            invalidateOverlay();
        }
        // Finishing: släppet pågår redan — bara pekaren nollas, så
        // `dragEnd` hoppar över en källa som inte finns.
    }

    // Per nod: skörda damage och nolla widget-ägt tillstånd.
    auto städa = [this](Widget& n) {
        if (n.hasLastRect_) {
            extraDamage_.push_back(n.lastRect_);
            needsRedraw_    = true;
            n.hasLastRect_  = false;
        }
        n.dirty_      = false;
        n.hovered_    = false;
        n.pressed_    = false;
        n.focused_    = false;
        n.dropEffect_ = DragEffect::None;
    };
    städa(w);
    forEachWidget(w, städa);
}

// RIVNINGSVÄGEN FÖR INNEHÅLLET — kontraktet står vid deklarationen. Caret-
// timern och textinmatningen släcks utan `caretBlink` (widgeten kan vara
// död), och tooltipens popup stängs utan att källan läses.
void AppWindow::forgetWidgets()
{
    if (open_ && focusedWidget_) {
        win_->stopTimer(kCaretTimerId);
        win_->setTextInputActive(false);
    }
    caretPhase_      = false;
    focusedWidget_   = nullptr;
    pressedWidget_   = nullptr;
    for (ClickTrack& t : clicks_)
        t.widget = nullptr;
    middleWidget_    = nullptr;
    rightWidget_     = nullptr;
    ++middleSeq_;   // trycken är slut — en krok som routar ett återställer inget
    ++rightSeq_;
    for (AppPress& p : appPress_)   // appens tryck likaså
        p.end();
    gestureWidget_   = nullptr;
    capturedWidget_  = nullptr;
    grabStack_.clear();
    // Tangentminnet står kvar: varje länk bär en vakt som frågar widgetens
    // livstecken FÖRST (`HandlerGuard::ok`), så den döda läses aldrig, och de
    // levande i kedjan får sitt släpp.
    drag_ = DragSession{};
    toolTipCancel(false);
    tip_ = ToolTipState{};
    invalidate();
}

// KOMPOSIT-STOPPET ligger HÄR och ingen annanstans:
// en widget som svarar `focusesChildren()` ERSÄTTER sina barn som tabbstopp.
//
// `continue` står INNANFÖR kandidat-grenen med flit. Ett ovillkorligt
// `continue` hade varit fel: en komposit som är `!focusable()` eller
// `!isEnabled()` hade då varken lagts till själv ELLER låtit sina barn
// synas, alltså gjort hela grenen otabbbar i stället för att vara ett stopp.
//
// `showsChildren()` står EFTER kandidat-grenen och är
// ovillkorlig: en widget som gömmer sitt innehåll (en inaktiv flik i en
// dockningsyta) får själv vara ett stopp, men vägen in i barnen är stängd
// vad den än svarar på `focusable()`.
void AppWindow::collectFocusable(Widget& w, std::vector<Widget*>& out)
{
    for (Widget* ch : w.children()) {
        if (!ch->isVisible())
            continue;
        if (ch->focusable() && ch->isEnabled()) {
            out.push_back(ch);
            if (ch->focusesChildren())
                continue;   // jag ERSÄTTER mina barn som tabbstopp
        }
        if (!ch->showsChildren())
            continue;       // mina barn syns inte just nu
        collectFocusable(*ch, out);
    }
}

// TAB-LISTAN som EN sanning. `collectFocusable`/`moveFocus` är
// privata och `handleEvent` går inte att nå från ett test, så listan vore
// omätbar — och en grind som inte kan se ordningen kan inte se ett stopp
// heller. Publik och byggd av SAMMA två steg som Tab-vandringen använder:
// DFS-insamling, sedan STABIL sort på `tabIndex` där de auto-satta bär sin
// egen listposition som nyckel.
//
// `moveFocus` konsumerar den — ingen parallellkopia av sorteringen.
std::vector<Widget*> AppWindow::focusOrder()
{
    std::vector<Widget*> order;
    collectFocusable(root_, order);

    // Nyckeln per element: satt tabIndex, annars den egna DFS-positionen.
    // Sorteras som par så indexen följer med elementen.
    std::vector<std::pair<int, Widget*>> keyed(order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        keyed[i] = {order[i]->tabIndex >= 0 ? order[i]->tabIndex
                                            : static_cast<int>(i),
                    order[i]};
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) {
                         return a.first < b.first;
                     });
    for (std::size_t i = 0; i < keyed.size(); ++i)
        order[i] = keyed[i].second;
    return order;
}

// Tab-vandringen: nästa/föregående fokuserbara widget i tabIndex-ordningen,
// med wrap. Finns ingen fokuserbar händer inget.
//
// AUTO-NUMRET ÄR LISTPOSITIONEN. Det finns ingen lagrad räknare och ingen
// utdelning vid adoption: en räknare delar ut nummer i ADOPTIONSordning,
// och den sammanfaller med DFS bara så länge trädet byggs uppifrån och ner
// i ett svep — adopteras ett barn senare till en container som redan
// sitter i fönstret glider de två isär. Här räknas nyckeln fram ur den
// FÄRDIGA listan vid varje tryck (pull, ingen cache), precis som färgerna
// resolvas per repaint.
//
// Rör ingen sin siffra är key[i] == i, sorteringen en no-op och ordningen
// exakt DFS-ordningen.
// Sorteringen är STABIL, så lika nycklar (dubbletter, eller en satt siffra
// som kolliderar med en listposition) behåller sin inbördes DFS-ordning.
void AppWindow::moveFocus(int dir)
{
    if (Widget* nästa = nextFocus(dir))
        setFocus(nästa);
}

// Stoppet Tab (+1) eller Shift+Tab (−1) landar på — `moveFocus`s EN sanning,
// publik för att kunna grindas precis som `focusOrder()`.
//
// VAR STÅR VI NU? Det fokuserade, ELLER dess närmaste FÖRFADER i listan.
// Ett fokusombud står aldrig själv i Tab-listan — kompositen gör
// det — så med bara en exakt jämförelse hittades inte talfältets inre fält,
// och Tab därifrån hoppade till fönstrets FÖRSTA stopp i stället för till
// nästa efter talfältet. Uppåtsökningen ändrar ingenting för en widget som
// står i listan själv (den hittas i första varvet), och en vanlig container
// är aldrig ett stopp, så bara kompositer påverkas.
Widget* AppWindow::nextFocus(int dir)
{
    // EN sanning: listan OCH sorteringen kommer ur `focusOrder()`, som är
    // publik och därmed grindbar. En egen kopia här hade varit den andra
    // sanningen som en grind sedan inte mäter.
    std::vector<Widget*> order = focusOrder();
    const int            n     = static_cast<int>(order.size());
    if (n == 0)
        return nullptr;

    int  start  = dir > 0 ? -1 : n;   // utan fokus: Tab → första, Shift+Tab → sista
    bool hittad = false;
    for (const Widget* f = focusedWidget_; f && !hittad; f = f->parent())
        for (int i = 0; i < n; ++i)
            if (order[i] == f) {
                start  = i;
                hittad = true;
                break;
            }

    return order[((start + dir) % n + n) % n];
}

// HÖGERKLICKETS ROUTING — en ROUTED LOOKUP och inte
// bubbling: vi letar uppåt i `parent()`-kedjan från widgeten under pekaren
// och anropar FÖRSTA som har en satt `onContextMenu`. Den widgeten är
// `sender` (regel 6 säger att sender är den som avfyrade eventet); den
// TRÄFFADE widgeten läggs i args som `source`.
//
// `over` är nullptr på TOM fönsteryta — `widgetAt` svarar inte med roten
// där — och det hanteras explicit: sökningen startar då i `root_`, alltså
// "fönstret självt". Utan den raden hade högerklick på tom yta tappats,
// vilket är precis det ställe en snabbmeny oftast behövs.
//
// En INAKTIVERAD widget hoppas över och sökningen fortsätter uppåt: en
// disabled kontroll svarar inte på något annat heller, men panelen den
// sitter i ska fortfarande kunna ge sin meny.
bool AppWindow::fireContextMenu(Widget* over, float x, float y)
{
    for (Widget* w = over ? over : &root_; w; w = w->parent()) {
        if (!w->isEnabled() || !w->onContextMenu)
            continue;
        ContextMenuArgs a;
        a.x      = x;
        a.y      = y;
        a.source = over;
        const std::weak_ptr<char> liv = lifeToken();
        w->onContextMenu(w, a);
        if (!liv.expired())   // handlern får ha rivit fönstret
            invalidate();     // …eller öppnat en meny/ändrat vad som helst
        return true;
    }
    return false;
}

// MITTEN- OCH HÖGERTRYCKETS ROUTING —
// hjulets form och inte högerklickets: kroken FRÅGAS uppåt i `parent()`-kedjan
// och den första som svarar true tar trycket. En ritytas mittendrag är
// beteende och ingen handler, och en yta inuti en annan (en karta i en panel)
// ska kunna avstå så att den yttre får panorera. Fönstrets rot frågas inte —
// den är ingen yta, bara bärare av barnen (hjulets regel). Högerknappen är
// samma familj med egen bokföring (`rightWidget_`).
//
// ETT KVARHÄNGANDE TRYCK med samma knapp — släppet tappades — har anroparen
// redan avbrutit (`cancelLostPress`), och `over` är
// räknad OM efter det: avbrottets handler kan ha ändrat trädet — också rivit
// widgeten under pekaren.
//
// Explicit capture (`setPointerCapture`) tar hela musen, och då frågas bara
// den fångande — samma som hjulet och vänstertrycket.
//
// En krok kan köra en handler (ritytans `onMouseDown`), och en handler
// kan pumpa NÄSTLAT (en modal dialog): fönstret får då fokusförlust medan
// kroken ännu kör. Den tillfrågade står därför som PRELIMINÄR mottagare under
// sin krok — avbrottet når den med `mouseCancel(b)` — och trycket bär en
// SEKVENSIDENTITET (`auxSeq`): har den ändrats när kroken svarar tog
// trycket slut under den (eller ett nyare började), och ingenting återställs —
// varken mottagaren, en förfaders fråga eller snabbmenyn. Efter kroken frågas
// FÖNSTRETS liv före allt annat, och widgetens vakt innan den blir trycket
// eller innan dess förälder frågas — en som tog bort sig själv tar inget tryck,
// och då lever trycket otaget (snabbmenyn).
bool AppWindow::routeAuxDown(gfx::MouseButton b, Widget* over, float x, float y)
{
    const std::weak_ptr<char> liv = lifeToken();
    const std::uint64_t       id  = ++auxSeq(b);   // DET HÄR tryckets identitet
    enum class Svar { Tog, Avböjde, Lämnade, Slut };
    const auto prova = [&](Widget& w) {
        const Widget::HandlerGuard vakt(w);
        const Ctx                  c = ctxFor(w);
        auxWidget(b)                 = &w;   // preliminär mottagare
        const bool tog = b == gfx::MouseButton::Right ? w.rightDown(c, x, y)
                                                      : w.middleDown(c, x, y);
        if (liv.expired() || auxSeq(b) != id)
            return Svar::Slut;   // fönstret dog, eller trycket tog slut under kroken
        if (!vakt.ok()) {
            if (auxWidget(b) == &w)
                auxWidget(b) = nullptr;
            return Svar::Lämnade;
        }
        if (tog)
            return Svar::Tog;   // den preliminära mottagaren står kvar
        auxWidget(b) = nullptr;
        return Svar::Avböjde;
    };
    if (Widget* fångad = capturedWidget_)
        return !fångad->isEnabled() || prova(*fångad) != Svar::Slut;
    for (Widget* w = over; w && w != &root_; w = w->parent()) {
        if (!w->isEnabled())
            continue;
        switch (prova(*w)) {
        case Svar::Tog:
        case Svar::Lämnade:
            return true;
        case Svar::Slut:
            return false;
        case Svar::Avböjde:
            break;   // `w` sitter kvar (vakten) — förälderns tur
        }
    }
    return true;
}

Widget*& AppWindow::auxWidget(gfx::MouseButton b)
{
    return b == gfx::MouseButton::Right ? rightWidget_ : middleWidget_;
}

std::uint64_t& AppWindow::auxSeq(gfx::MouseButton b)
{
    return b == gfx::MouseButton::Right ? rightSeq_ : middleSeq_;
}

// ETT TAPPAT SLÄPP — se app_window.h. Widgetens eget tryck först, appens sedan,
// och efter varje krok frågas fönstrets liv innan något mer rörs.
bool AppWindow::cancelLostPress(gfx::MouseButton b)
{
    const std::weak_ptr<char> liv    = lifeToken();
    bool                      avbröt = false;
    Widget*                   gammal = nullptr;
    if (b == gfx::MouseButton::Left) {
        if ((gammal = pressedWidget_)) {
            gammal->pressed_ = false;
            gammal->invalidate();
            pressedWidget_ = nullptr;
        }
    } else if ((gammal = auxWidget(b))) {
        auxWidget(b) = nullptr;
        ++auxSeq(b);   // det tappade trycket är slut
    }
    if (gammal) {
        avbröt           = true;
        const bool körde = gammal->mouseCancel(b);
        if (liv.expired())
            return true;
        if (körde)
            invalidate();   // en handler kördes
    }
    if (appPressFor(b)->w) {
        avbröt = true;
        appMouseCancel(b);   // fönstrets liv frågar anroparen
    }
    return avbröt;
}

// ------------------------------------------------- APPENS MUS
//
// Kontraktet står vid fälten i widget.h, bokföringen vid `AppPress` i
// app_window.h. Varje funktion här kör användarkod, och efter den frågas
// fönstrets livstecken INNAN en medlem läses.

AppWindow::AppPress* AppWindow::appPressFor(gfx::MouseButton b)
{
    switch (b) {
    case gfx::MouseButton::Left:   return &appPress_[0];
    case gfx::MouseButton::Middle: return &appPress_[1];
    case gfx::MouseButton::Right:  return &appPress_[2];
    case gfx::MouseButton::None:   break;
    }
    return nullptr;
}

// INGEN BUBBLING: en inaktiverad widget under pekaren ger inget mål —
// varken den eller dess förälder — och den fångande ett bara när den är
// aktiverad, som dess `mouseDown`. Tom fönsteryta är fönstrets rot.
Widget* AppWindow::mouseTarget(Widget* over)
{
    if (capturedWidget_)
        return capturedWidget_->isEnabled() ? capturedWidget_ : nullptr;
    if (!over)
        return &root_;
    return over->isEnabled() ? over : nullptr;
}

AppWindow::AppDown AppWindow::appMouseDown(gfx::MouseButton b, Widget* mål,
                                           float x, float y)
{
    AppPress&           ap = *appPressFor(b);
    const std::uint64_t id = ++ap.seq;   // DET HÄR tryckets identitet
    ap.w                   = mål;        // preliminärt — ett avbrott når det
    ap.taken               = false;
    if (!mål || !mål->onMouseDown)
        return AppDown::Otaget;
    const std::weak_ptr<char>  liv  = lifeToken();
    const Widget::HandlerGuard vakt(*mål);
    const bool tog = mål->fireMouse(mål->onMouseDown, ctxFor(*mål),
                                    gfx::Pointf{x, y}, b);
    if (liv.expired())
        return AppDown::Död;
    invalidate();   // en handler kördes — som efter ett klick
    // Trycket tog slut under handlern (en modal dialog gav fokusförlust, och
    // avbrottet har redan gett `onMouseCancel`) — eller målet lämnade trädet,
    // och då är trycket slut för det utan mer användarkod.
    if (ap.seq != id || !vakt.ok()) {
        if (ap.seq == id)
            ap.w = nullptr;
        return AppDown::Slut;
    }
    ap.taken = tog;
    return tog ? AppDown::Taget : AppDown::Otaget;
}

bool AppWindow::appPressStep(gfx::MouseButton b, float x, float y,
                             Widget::AppPressStep steg)
{
    AppPress& ap = *appPressFor(b);
    Widget*   w  = ap.w;
    if (!w || !ap.taken)
        return w != nullptr;
    if (steg == Widget::AppPressStep::Move && !w->isEnabled())
        return true;   // `mouseDrag`s regel: bara aktiverad
    const std::uint64_t       id  = ap.seq;
    const std::weak_ptr<char> liv = lifeToken();
    w->appPress(ctxFor(*w), x, y, b, steg);
    if (liv.expired())
        return false;
    return ap.seq == id;
}

bool AppWindow::appMouseUp(gfx::MouseButton b, float x, float y, AppUp& ut)
{
    AppPress& ap = *appPressFor(b);
    ut           = AppUp{ap.w, ap.taken, false};
    Widget* w    = ap.end();   // trycket är slut — FÖRE handlern
    if (!w || !w->onMouseUp)
        return true;
    const std::weak_ptr<char>  liv  = lifeToken();
    const Widget::HandlerGuard vakt(*w);
    const bool tog = w->fireMouse(w->onMouseUp, ctxFor(*w), gfx::Pointf{x, y}, b);
    if (liv.expired())
        return false;
    invalidate();   // en handler kördes — som efter ett klick
    if (!vakt.ok())
        ut.target = nullptr;      // målet lämnade trädet under handlern
    ut.handled = tog && !ut.taken;   // ett taget tryck har inget eget släpp
    return true;
}

bool AppWindow::appMouseCancel(gfx::MouseButton b)
{
    Widget* w = appPressFor(b)->end();
    if (!w || !w->onMouseCancel)
        return true;
    const std::weak_ptr<char> liv = lifeToken();
    w->fireMouse(w->onMouseCancel, ctxFor(*w), mousePosition(), b);
    if (liv.expired())
        return false;
    invalidate();   // en handler kördes
    return true;
}

Widget* AppWindow::takenPressOwner() const
{
    for (const AppPress& p : appPress_)
        if (p.w && p.taken)
            return p.w;
    return nullptr;
}

bool AppWindow::holdsTakenPress(const Widget* w) const
{
    for (const AppPress& p : appPress_)
        if (p.taken && p.w == w)
            return true;
    return false;
}

// DUBBELKLICKET (alla tre knapparna): ett
// andra tryck på SAMMA widget inom OS:ets tid (`gfx::doubleClickMs`) och inte
// längre bort än dragtröskeln — samma mått som avgör när ett tryck slutar vara
// ett tryck. Trycket efter ett dubbelklick börjar en ny följd
// (Windows-semantiken), så tre snabba tryck är ETT dubbelklick och inte två.
// Avståndet mäts i SKÄRMENS rymd: ett fönster som följt pekaren under ett drag
// (en flytande panel eller ett verktygsfält) står annars still i
// sin egen rymd, och nästa snabba tryck hade blivit ett dubbelklick som dockar.
// Wayland saknar skärmrymd — där är punkten klientrelativ.
// TOLERANSEN går genom SAMMA avbildning som punkten: skärmrymden är
// fysiska px på Windows och X11 men PUNKTER på macOS, och en tolerans i
// klientens px blir där dubbelt så stor på Retina.
bool AppWindow::countClick(ClickTrack& t, Widget* w, float x, float y)
{
    const std::uint64_t nu  = gfx::ticksMs();
    const float         px  = style().dragThreshold * win_->uiScale();
    const gfx::Pointf   sk  = win_->clientToScreen(x, y);
    const float         tol = win_->clientToScreen(x + px, y).x - sk.x;
    const float         ddx = sk.x - t.x;
    const float         ddy = sk.y - t.y;
    const bool dubbel = w == t.widget && nu - t.ms <= gfx::doubleClickMs() &&
                        ddx * ddx + ddy * ddy <= tol * tol;
    t.widget = dubbel ? nullptr : w;
    t.ms     = nu;
    t.x      = sk.x;
    t.y      = sk.y;
    return dubbel;
}

// STYRPLATTANS GEST — nypningen och vridningen. Målvalet är
// HJULETS (widgeten under pekaren och sedan förfäderna, inaktiverade hoppas
// över, roten frågas inte, explicit capture frågar bara den fångande), men
// sekvensen HÅLLS: den som tog Begin får Update och End/Cancel var pekaren än
// står. En nypning som börjar över kartan ska inte byta mål för att kartan
// zoomat ut så att en grannwidget hamnat under pekaren.
//
// LEVERANSEN: Update bara till en aktiverad widget (`middleDrag`s regel),
// End/Cancel ALLTID — en sekvens ska kunna avslutas också av en widget som
// inaktiverades under den (`middleUp`s). `gestureWidget_` nollas FÖRE den
// sista kroken, så en widget som tas bort i den inte lämnar en pekare kvar.
// Update UTAN en hållen sekvens (en Begin som ingen tog, en gest som började
// innan fönstret fick den, ett mål som togs bort) väljer mål som en Begin och
// levereras som en — en widget ser alltid sekvensen börja med Begin.
//
// PEKAREN ÄR LIVSFARLIG (drag-sessionens regel): en krok kan köra en
// användar-handler (ritytans `onCameraChange`) som tar widgeten ur trädet.
// Under målvalet står kandidaten därför PRELIMINÄRT i `gestureWidget_` medan
// kroken körs — `detachTree` nollar fältet om den lämnar trädet — och efter
// varje anrop läses FÄLTET, aldrig en lokal kopia.
// HJULETS RADER FÖR EN WIDGET. Shift+MUSHJUL rullar i
// sidled — bara ett mushjul (styrplattan har sina två axlar redan) och bara
// när eventet saknar sidled, så ett äkta sidohjul och ett redan bytt event
// aldrig byts tillbaka. Sedan vänds varje axel efter widgetens UPPLÖSTA
// inställning (egen, förfädernas, appens). Riktningen är RELATIV mot OS:et:
// det backenden levererat är redan OS:ets val.
gfx::Pointf AppWindow::wheelFor(const Widget& w, const gfx::Event& e) const
{
    if (!w.followsWheelDirection())
        return {e.wheelX, e.wheelY};
    float wx = e.wheelX, wy = e.wheelY;
    if (!e.precise && (modifiers_ & gfx::kModShift) != 0 && wx == 0.0f &&
        wy != 0.0f) {
        wx = wy;
        wy = 0.0f;
    }
    const InputDevice enhet = e.precise ? InputDevice::Touchpad : InputDevice::Mouse;
    if (w.isWheelInverted(enhet, false))
        wy = -wy;
    if (w.isWheelInverted(enhet, true))
        wx = -wx;
    return {wx, wy};
}

void AppWindow::routeGesture(const gfx::Event& e)
{
    using P = gfx::GesturePhase;
    // Fönstrets liv: en krok får köra en handler som river fönstret,
    // och då läses varken `gestureWidget_` eller något annat efter den.
    const std::weak_ptr<char> liv  = lifeToken();
    const bool                slut = e.phase == P::End || e.phase == P::Cancel;
    if (e.phase == P::Begin)
        gestureWidget_ = nullptr;   // en ny sekvens väljer om (en förlorad End)

    if (Widget* w = gestureWidget_) {
        if (slut)
            gestureWidget_ = nullptr;
        if (slut || w->isEnabled())
            w->gesture(ctxFor(*w), e.mouseX, e.mouseY, e.phase, e.gestureScale,
                       e.gestureRotation);
        return;
    }
    if (slut)
        return;   // ingen sekvens att avsluta

    // Svarar true när frågan är AVGJORD: kandidaten tog Begin (och håller nu
    // sekvensen), lämnade trädet under kroken (och då är det slut) eller rev
    // fönstret.
    const auto fråga = [&](Widget* w) {
        if (!w->isEnabled())
            return false;
        gestureWidget_ = w;   // preliminärt — se PEKAREN ovan
        const bool tog = w->gesture(ctxFor(*w), e.mouseX, e.mouseY, P::Begin,
                                    e.gestureScale, e.gestureRotation);
        if (liv.expired())
            return true;      // fönstret dog — ingenting kvar att röra
        if (gestureWidget_ != w)
            return true;      // togs bort i kroken — ingen sekvens kvar
        if (!tog)
            gestureWidget_ = nullptr;
        return tog;
    };
    if (capturedWidget_) {
        fråga(capturedWidget_);
        return;
    }
    const Ctx c = makeCtx();
    for (Widget* w = widgetAt(e.mouseX, e.mouseY, c); w && w != &root_;
         w = w->parent())
        if (fråga(w))
            return;
}

// MUSPEKAREN. Vem som äger pekaren avgörs i SAMMA ordning som
// mus-routingen: explicit capture, sedan trycket (auto-capture — ett skikt
// som dras svarar ↔ fast pekaren hunnit ut över grannrutan), sedan widgeten
// under pekaren. En inaktiverad widget frågas inte. Ingen cache: backenden
// vet vad den faktiskt visar (en resize-pil över chromet, en ny Wayland-
// enter) och hoppar själv över ett oförändrat val.
void AppWindow::updateCursor(Widget* over, float x, float y)
{
    // Mitten- och högertrycket äger pekaren som
    // vänstertrycket gör: en yta som panoreras behåller sin flytta-pekare
    // över grannen, och ett verktyg som drar med höger knapp sin.
    // Ett tryck appen TOG äger den också: målet följer det tills
    // släppet.
    Widget* const tagen = takenPressOwner();
    Widget* w = capturedWidget_ ? capturedWidget_
              : pressedWidget_  ? pressedWidget_
              : middleWidget_   ? middleWidget_
              : rightWidget_    ? rightWidget_
              : tagen           ? tagen
                                : over;
    gfx::PointerShape s = gfx::PointerShape::Arrow;
    // FÖNSTRETS VÄNTA-PEKARE slår allt, och en widgets SATTA form slår dess
    // `cursorAt` — för widgeten själv, aldrig ärvd.
    if (useWaitCursor_)
        s = gfx::PointerShape::Wait;
    else if (w && w->isEnabled())
        s = w->cursor_ ? *w->cursor_ : w->cursorAt(ctxFor(*w), x, y);
    win_->setCursor(s);
}

// DE HÅLLNA TANGENTERNA — kontraktet står vid `isKeyHeld`
// i app_window.h. En post utanför enumet (ett syntetiskt event med ett
// påhittat värde) är ingen tangent och lämnar mängden orörd.
void AppWindow::setKeyHeld(gfx::Key k, bool held)
{
    const auto i = static_cast<std::size_t>(k);
    if (k == gfx::Key::Unknown || i >= heldKeys_.size())
        return;   // Unknown är många tangenter i en post — hålls aldrig
    heldKeys_.set(i, held);
    if (held)
        return;
    const gfx::Key tvilling = numpadTwin(k);
    if (tvilling != gfx::Key::Unknown)
        heldKeys_.reset(static_cast<std::size_t>(tvilling));
}

bool AppWindow::isKeyHeld(gfx::Key k) const
{
    const auto i = static_cast<std::size_t>(k);
    if (k == gfx::Key::Unknown || i >= heldKeys_.size())
        return false;
    if (heldKeys_.test(i))
        return true;
    // Den DEDIKERADE tangenten hålls också av numpadens navigationsroll —
    // så som en widget ser den, vikt med samma `gfx::navigationKey` som
    // `KeyDown` viker med innan den fokuserade widgeten får tangenten.
    for (std::size_t j = 0; j < heldKeys_.size(); ++j)
        if (heldKeys_.test(j) &&
            gfx::navigationKey(static_cast<gfx::Key>(j)) == k)
            return true;
    return false;
}

bool AppWindow::heldRaw(gfx::Key raw) const
{
    const auto i = static_cast<std::size_t>(raw);
    if (raw == gfx::Key::Unknown || i >= heldKeys_.size())
        return false;
    if (heldKeys_.test(i))
        return true;
    const gfx::Key tvilling = numpadTwin(raw);
    return tvilling != gfx::Key::Unknown &&
           heldKeys_.test(static_cast<std::size_t>(tvilling));
}

// ------------------------------------------- TANGENTERNAS KEDJOR
//
// FOKUSENHETEN — Tab-kontraktets villkor för en komposit, inte "varje
// förfader": en förfader som inte ERSÄTTER sina barn som tabbstopp är inte en
// enhet med dem (ritytan med ett fält på sig, en panel, en dockningspanel som
// inte själv är fokuserbar). Kedjan slutar vid första förfader som inte
// uppfyller villkoret, och roten är aldrig med — utom som start, när ingen
// widget har fokus.
AppWindow::KeyChain AppWindow::keyUnit(Widget& start)
{
    KeyChain k;
    k.push_back(KeyLink{&start, Widget::HandlerGuard(start)});
    for (Widget* p = start.parent(); p && p != &root_; p = p->parent()) {
        if (!(p->focusesChildren() && p->focusable() && p->isEnabled() &&
              p->isVisible()))
            break;
        k.push_back(KeyLink{p, Widget::HandlerGuard(*p)});
    }
    return k;
}

AppWindow::KeyChain AppWindow::currentKeyUnit()
{
    if (!focusedWidget_)
        return keyUnit(root_);   // fönstret utan fokus: rotens tangenter
    if (!focusedWidget_->isVisible() || !focusedWidget_->isEnabled())
        return {};               // den fokuserade får inga tangenter (som keyDown)
    return keyUnit(*focusedWidget_);
}

// Uppslaget är `findKeyTarget`s (tangenten eller dess numpad-tvilling) —
// posten tas sedan ur minnet.
std::optional<AppWindow::KeyTarget> AppWindow::takeKeyTarget(gfx::Key raw)
{
    const KeyTarget* hittad = findKeyTarget(raw);
    if (!hittad)
        return std::nullopt;
    const auto it = keyTargets_.begin() + (hittad - keyTargets_.data());
    KeyTarget  t  = std::move(*it);
    keyTargets_.erase(it);
    return t;
}

const AppWindow::KeyTarget* AppWindow::findKeyTarget(gfx::Key raw) const
{
    const gfx::Key tvilling = numpadTwin(raw);
    for (const KeyTarget& t : keyTargets_)
        if (t.raw == raw || (tvilling != gfx::Key::Unknown && t.raw == tvilling))
            return &t;
    return nullptr;
}

bool AppWindow::hasKeyTarget(gfx::Key raw) const
{
    return findKeyTarget(raw) != nullptr;
}

// SLÄPPET TILL EN KEDJA. Handlarna innerst först, som vid trycket, och
// med vakten frågad före var och en (en widget som dött, lossats eller bytt
// fönster sedan trycket hoppas över — men en INAKTIVERAD får sitt släpp, som
// `mouseUp`). Kroken går till MOTTAGAREN, den som tog tangenten.
// `fireEvent` duger inte: den kopierar args och
// hade tappat `handled`.
bool AppWindow::deliverKeyUp(const KeyTarget& t, KeyArgs a)
{
    const std::weak_ptr<char> liv = lifeToken();
    bool                      körde = false;
    if (!t.skipped)
        for (const KeyLink& l : t.chain) {
            if (!l.guard.ok() || !l.w->onKeyUp)
                continue;
            l.w->noteHandlerRun();
            l.w->onKeyUp(l.w, a);
            körde = true;
            if (liv.expired())
                return false;
            if (a.handled)
                break;
        }
    if (!a.handled && t.recipient < t.chain.size()) {
        const KeyLink& m = t.chain[t.recipient];
        if (m.guard.ok() && m.w->keyUp(a.key, a.mods))
            körde = true;
        if (liv.expired())
            return false;
    }
    if (körde)
        invalidate();   // en handler kördes — som efter ett klick
    return true;
}

bool AppWindow::releaseKeyTargets(std::vector<KeyTarget> poster)
{
    for (const KeyTarget& t : poster) {
        KeyArgs a;
        a.key       = t.key;   // den tryckets `onKeyDown` fick; mods 0
        a.cancelled = true;
        if (!deliverKeyUp(t, a))
            return false;
    }
    return true;
}

// POPUPENS FÖRMEDLING — se app_window.h. Bara
// FÖNSTRET som minns tangenten får släppet: ägarens mängd släpper den (så en
// rityta där slutar ticka), och finns en post går släppet till dess kedja.
// Minns ägaren bara mängden (trycket åts av en meny eller `event()`) får
// ingen kedja något — den såg aldrig trycket.
bool AppWindow::forwardKeyUp(const gfx::Event& e)
{
    for (AppWindow* o = owner_; o && o->open_; o = o->owner_) {
        if (!o->heldRaw(e.key) && !o->hasKeyTarget(e.key))
            continue;
        o->setKeyHeld(e.key, false);
        if (std::optional<KeyTarget> t = o->takeKeyTarget(e.key)) {
            KeyArgs a;
            a.key  = t->key;
            a.mods = e.mods;
            o->deliverKeyUp(*t, a);   // en handler får riva fönster
        }
        return true;
    }
    return false;
}

// POPUPEN SOM TAR TANGENTBORDET — kontraktet vid
// `setPopupTakesKeys`. Den öppna ägda popupen med attributet, som inte är på
// väg att stängas: bara en popup lever åt gången (`App::closePopupsNow`), och
// en popup i popupen får sina tangenter av SIN ägare, genom samma väg.
AppWindow* AppWindow::openKeyPopup() const
{
    for (AppWindow* w : owned_)
        if (w->popup_ && w->popupTakesKeys_ && w->open_ && !w->pendingClose_ &&
            !w->hidden_)
            return w;
    return nullptr;
}

// Tangenten, släppet och tecknet NED till popupen, med popupens `windowId`
// (som Waylands grab hade levererat dem). Varje TRYCK och varje upprepning
// medan popupen är öppen går dit — också en tangent som hölls när den
// öppnades: upprepningen går till det som har tangentbordet, som när ett
// annat fönster aktiveras. SLÄPPET följer TRYCKEN, och båda kan ha sett dem:
// den popup som håller tangenten får det (också en som är på väg att
// stängas — den såg trycket), och ÄGAREN behåller det när den själv håller
// tangenten (tryckt innan popupen öppnades — tryckets post finns här, och en
// rityta där ska sluta ticka). Biten i `keysToPopup_` säger att trycket
// gick BARA till popupen: då sväljs släppet här, också när popupen redan
// har stängts (Escape). `Unknown` (modifierarna) paras aldrig — den går dit
// en öppen popup finns. true = ägaren rör inget mer (eventet var popupens,
// eller en handler i popupen rev ägaren).
bool AppWindow::forwardToKeyPopup(const gfx::Event& e)
{
    using T = gfx::EventType;
    if (e.type != T::KeyDown && e.type != T::KeyUp && e.type != T::TextInput)
        return false;
    const auto bit   = static_cast<std::size_t>(e.key);
    const bool paras = e.type != T::TextInput && e.key != gfx::Key::Unknown &&
                       bit < keysToPopup_.size();
    const auto skicka = [&e](AppWindow& p) {
        gfx::Event f = e;
        f.windowId   = p.window().id();
        p.handleEvent(f);   // en handler får riva fönster
    };
    if (e.type == T::KeyUp && paras) {
        const gfx::Key tvilling = numpadTwin(e.key);
        const auto     tbit     = static_cast<std::size_t>(tvilling);
        const bool     bara =
            keysToPopup_.test(bit) ||
            (tvilling != gfx::Key::Unknown && keysToPopup_.test(tbit));
        keysToPopup_.reset(bit);
        if (tvilling != gfx::Key::Unknown)
            keysToPopup_.reset(tbit);
        const bool ägarens = heldRaw(e.key) || hasKeyTarget(e.key);
        bool       tog     = false;
        for (AppWindow* w : owned_)
            if (w->popup_ && w->popupTakesKeys_ && w->open_ &&
                (w->heldRaw(e.key) || w->hasKeyTarget(e.key))) {
                const std::weak_ptr<char> liv = lifeToken();
                skicka(*w);
                if (liv.expired())
                    return true;
                tog = true;
                break;
            }
        // Ägarens eget tryck släpps här som vanligt; ett tryck som bara gick
        // till popupen är förbrukat — också när popupen redan är borta.
        if (ägarens)
            return false;
        return bara || tog;
    }
    AppWindow* const p = openKeyPopup();
    if (!p) {
        // Ett NYTT tryck här: släppet är ägarens, också om ett gammalt
        // tryck till en popup aldrig fick sitt (biten hade annars ätit det).
        if (e.type == T::KeyDown && paras)
            keysToPopup_.reset(bit);
        return false;
    }
    if (e.type == T::KeyDown && paras && !heldRaw(e.key))
        keysToPopup_.set(bit);   // trycket är bara popupens
    skicka(*p);
    return true;
}

// Aktiveringen går genom SAMMA `activate(stig)` som klick och Enter —
// aldrig en parallell commitväg. PROGRAMMENYN EFTER menyraden:
// den ersätter inget i den, så en genväg båda bär går till
// menyraden. Ett false från menyraden betyder att ingen handler kördes, så
// fönstret lever när programmenyn frågas.
bool AppWindow::tryShortcut(gfx::Key k, std::uint32_t mods)
{
    if (menuBar.isVisible() && menuBar.isEnabled() &&
        menuBar.tryShortcut(k, mods))
        return true;
    return appIcon.isVisible() && appIcon.isEnabled() &&
           appIcon.tryShortcut(k, mods);
}

void AppWindow::notifyOwnedVisibility(bool shown)
{
    const std::vector<AppWindow*> ägda = owned_;
    for (AppWindow* w : ägda)
        if (std::find(owned_.begin(), owned_.end(), w) != owned_.end())
            w->ownerVisibilityChanged(shown);
}

void AppWindow::handleEvent(const gfx::Event& e)
{
    // FÖNSTRETS LIV — taget FÖRST. En handler får riva fönstret den
    // körs i (kontraktet vid `close()` i app_window.h), så efter VARJE anrop
    // som kan köra användarkod frågas `dött()` innan en medlem läses — och
    // vägen slutar där.
    const std::weak_ptr<char> liv  = lifeToken();
    const auto                dött = [&liv] { return liv.expired(); };
    // `onResize`s kedjespärr räknar riktiga
    // händelser HÄR, så också grindarnas syntetiska når räkneverket.
    if (App::instance_)
        App::instance_->noteEvent(e.type);
    // POPUPEN SOM TAR TANGENTBORDET — FÖRST av allt för en tangent
    // eller ett tecken: en öppen ägd popup med `setPopupTakesKeys` får den
    // genom sin egen ingång, och här rörs ingenting — inte modifierarna, de
    // hållna tangenterna, tooltipen eller appens `event()`-krok. Det är
    // Waylands väg (popupens grab levererar till popupen), gjord lika på de
    // tre plattformar där en `noActivate`-popup aldrig får OS-fokus.
    if (forwardToKeyPopup(e))
        return;
    // Pekarens form ur widgeten som NU står under pekaren — efter en handler
    // som kan ha ändrat trädet räknas den om i stället för att lita på en
    // pekare från före den.
    const auto pekareNu = [this](float x, float y) {
        const Ctx c = makeCtx();
        updateCursor(widgetAt(x, y, c), x, y);
    };
    // HOVERN: flaggorna sätts för hela trädet först, utan
    // användarkod, och `onHover` fyras sedan för var och en med vakten frågad
    // före och fönstrets liv efter — en handler får riva widgeten, trädet
    // eller fönstret, och trädet vandras aldrig medan en handler kör.
    struct HoverByte {
        Widget*              w;
        Widget::HandlerGuard guard;
        bool                 in;
    };
    const auto fyraHover = [this, &dött](const std::vector<HoverByte>& byten) {
        for (const HoverByte& b : byten) {
            if (!b.guard.ok())
                continue;
            HoverArgs a;
            a.entered = b.in;
            b.w->onHover(b.w, a);
            if (dött())
                return false;
            invalidate();   // handlern kan ha ändrat vad som helst
        }
        return true;
    };

    // Fönstret först (chrome-knappar, stängning, hit-test-ytor).
    // false = "det här fönstret begär stängning" — App verkställer
    // uppskjutet i pump-svansen.
    if (!win_->processEvent(e)) {
        close();
        return;
    }
    if (win_->takeDirty())        // hover/tryck på fönsterknapparna ändrades
        invalidateOverlay();      // chromet ritas per present — aldrig damage

    // MODIFIERARNA — läget ur varje händelse som bär det
    // (`gfx::Event::mods`: musen, tangenterna,
    // hjulet och gesten), FÖRE appens krok så att också den läser rätt värde
    // ur `modifierKeys()`. Fönsterhändelserna och texten bär ingen mask och
    // får inte nolla den.
    switch (e.type) {
    case gfx::EventType::MouseMotion:
    case gfx::EventType::MouseButtonDown:
    case gfx::EventType::MouseButtonUp:
        // PEKARENS SENAST KÄNDA POSITION — så en
        // formändring utan en rörelse (`Widget::setCursor`, vänta-pekaren,
        // en widget som göms) syns direkt (`refreshCursor`). MouseLeave
        // släcker den.
        lastMouse_   = gfx::Pointf{e.mouseX, e.mouseY};
        mouseInside_ = true;
        modifiers_   = e.mods;
        // KNAPPARNA SOM HÅLLS på samma ställe och av samma
        // skäl: avbrottet läser dem också när appens krok äter trycket.
        if (e.type == gfx::EventType::MouseButtonDown)
            buttonsDown_ =
                static_cast<std::uint8_t>(buttonsDown_ | mouseBit(e.button));
        else if (e.type == gfx::EventType::MouseButtonUp)
            buttonsDown_ =
                static_cast<std::uint8_t>(buttonsDown_ & ~mouseBit(e.button));
        break;
    case gfx::EventType::KeyDown:
    case gfx::EventType::KeyUp:
    case gfx::EventType::Gesture:
        modifiers_ = e.mods;
        break;
    // HJULETS SORT på samma ställe och av samma skäl: kroken
    // `mouseWheel` läser den ur `isPreciseWheel()`/`wheelPhase()`, och appens
    // `event()`-krok nedan ska se samma värde. Masken kommer ur hjulet SJÄLVT
    // — ritytan kretsar på Ctrl/Cmd + styrplattans drag, och ett drag direkt
    // efter ett Ctrl-tryck har ingen mushändelse emellan.
    case gfx::EventType::MouseWheel:
        modifiers_    = e.mods;
        wheelPrecise_ = e.precise;
        wheelPhase_   = e.phase;
        break;
    // …och pekaren LÄMNADE fönstret: positionen är inte längre känd, så en
    // formändring väntar till nästa rörelse in. Före
    // hover-handlarna nedan, som får gömma widgets och därmed ompröva.
    case gfx::EventType::MouseLeave:
        mouseInside_ = false;
        break;
    // AKTIVT FÖNSTER på samma ställe och av samma
    // skäl: appens krok ska läsa rätt ur `isActive()`. En popup blir aldrig
    // aktiv — dess fokus är ägarkedjans rots (Waylands fokusgrupp: ett fokus
    // som går från popupen till ett annat fönster kommer som popupens
    // `WindowFocusLost`, och roten hade annars stått kvar som aktiv).
    case gfx::EventType::WindowFocusGained:
    case gfx::EventType::WindowFocusLost: {
        AppWindow* rot = this;
        while (rot->popup_ && rot->owner_)
            rot = rot->owner_;
        if (!rot->popup_)
            rot->active_ = e.type == gfx::EventType::WindowFocusGained;
        break;
    }
    default:
        break;
    }

    // DE HÅLLNA TANGENTERNA på samma ställe och av samma
    // skäl — appens krok ska läsa rätt ur `isKeyHeld()` — och därför också
    // när kroken sedan äter eventet. Fokusförlusten tömmer: släppet av en
    // tangent som hölls när fokus gick kommer till ett annat fönster.
    //
    // Mängden läses FÖRE ändringen: en KeyDown för en
    // tangent som redan hölls är OS:ets upprepning (`KeyArgs::repeat`, ingen
    // backend behöver ändras), och en KeyUp tar tryckets post ur
    // tangentminnet HÄR — äter appens krok släppet slängs posten med det
    // (inget `onKeyUp`), och `höllsHär` säger om trycket alls gjordes här
    // (popupens förmedling).
    bool                     upprepning = false;
    bool                     höllsHär   = false;
    std::optional<KeyTarget> släppPost;
    switch (e.type) {
    case gfx::EventType::KeyDown:
        upprepning = heldRaw(e.key);
        setKeyHeld(e.key, true);
        break;
    case gfx::EventType::KeyUp:
        höllsHär = heldRaw(e.key) || hasKeyTarget(e.key);
        setKeyHeld(e.key, false);
        släppPost = takeKeyTarget(e.key);
        break;
    case gfx::EventType::WindowFocusLost:
        heldKeys_.reset();
        break;
    default:
        break;
    }

    // TOOLTIPEN — det som GÖMMER den, FÖRE appens krok: tooltipen
    // är ramverkets yta, och ett tryck eller en tangent som appen åt ska
    // ändå ta bort den. Tryck och tangent TYSTAR dessutom källan tills
    // pekaren lämnat den (Windows). Rörelsen hanteras längre ner, där
    // `over` räknas fram, och timern i Timer-grenen.
    switch (e.type) {
    case gfx::EventType::MouseButtonDown:
    case gfx::EventType::KeyDown:
        toolTipCancel(true);
        break;
    case gfx::EventType::MouseWheel:
    case gfx::EventType::Gesture:   // nypningen flyttar innehållet som hjulet
    case gfx::EventType::MouseLeave:
    case gfx::EventType::WindowFocusLost:
        toolTipCancel(false);
        break;
    default:
        break;
    }

    // Appens råa hook — kan äta eventet. Antar att appen ändrat något.
    if (event(e)) {
        if (!dött())
            invalidate();
        return;
    }
    if (dött())
        return;   // kroken rev fönstret utan att äta eventet

    switch (e.type) {
    // Användaren drog i fönsterkanten (eller WM:en ändrade storleken):
    // SPÅRA den nya klientstorleken. `initW_`/`initH_` är det
    // `clientSize()`/`contentSize()` svarar med, och utan den här raden
    // skulle de fortsätta rapportera vad någon en gång BAD OM — en getter
    // som ljuger så fort fönstret rörts. Skrivs i LOGISKA px, samma enhet
    // som settrarna.
    case gfx::EventType::WindowResized:
        trackClientSize();
        invalidate();
        break;

    // Fönsterhändelser som kräver omritning. EventType::None (oöversatta
    // backend-events) faller igenom default — ingen omritning.
    case gfx::EventType::WindowExposed:
    case gfx::EventType::WindowPixelSizeChanged:
    case gfx::EventType::WindowShown:
    // Ur maximerat eller minimerat följer ett fönster med en anpassad axel
    // innehållet igen — anpassningen hoppades
    // över medan det var maximerat.
    case gfx::EventType::WindowRestored:
        if (sizeToContent_ != SizeToContent::Manual)
            postRefit();
        invalidate();
        break;
    case gfx::EventType::WindowMaximized:
    // OS-temabyte: repainten resolvar aktivt tema på nytt (System-läget)
    case gfx::EventType::SystemThemeChanged:
        invalidate();
        break;

    // Skal-/DPI-byte i drift: fonten laddades för gamla uiScale — ladda
    // om i nya storleken FÖRE omritningen (annars suddig/felmätt text).
    // Skärmens mm mäts om också: den ANTAGNA tätheten följer OS-skalan.
    case gfx::EventType::WindowDisplayScaleChanged:
        reloadFont();
        measureDisplay();
        invalidate();
        break;

    // SKÄRMEN BYTTES: fönstret flyttade till en annan
    // skärm eller skärmen bytte läge — en millimeter är ett annat antal px.
    case gfx::EventType::WindowDisplayChanged:
        measureDisplay();
        invalidate();
        break;

    // Fönsterfokus styr även caret-timern: en textwidget ska inte blinka
    // (eller väcka appen) i ett inaktivt fönster — timern stoppas och
    // careten släcks, och kommer tillbaka när fönstret får fokus igen.
    case gfx::EventType::WindowFocusGained:
        if (focusedWidget_ && focusedWidget_->wantsTextInput()) {
            win_->startTimer(kCaretTimerId, kCaretBlinkMs);
            caretPhase_ = true;
            const Ctx c = ctxFor(*focusedWidget_);
            focusedWidget_->caretBlink(c, true);
        }
        invalidate();
        break;
    case gfx::EventType::WindowFocusLost: {
        if (focusedWidget_ && focusedWidget_->wantsTextInput()) {
            win_->stopTimer(kCaretTimerId);
            caretPhase_ = false;
            const Ctx c = ctxFor(*focusedWidget_);
            focusedWidget_->caretBlink(c, false);
        }
        // Fönsterförlust släpper capture och tömmer grab-stacken:
        // ett drag/en meny överlever inte att appen tappar fokus.
        // Det gäller drag-and-drop också: avbrottet är Escapes.
        // …och TRYCKEN: släppet
        // kommer aldrig när fokusbytet tog mus-capture (Windows
        // WM_CANCELMODE), och utan nollningen stod widgeten kvar nedtryckt —
        // och ett skikt kvar mitt i sitt drag — tills nästa släpp
        // någonstans. Inget `…Up`: inget släpp har skett och ett drag ska
        // inte committas; varje mottagare får `mouseCancel` (`dropPointer`).
        // …och TANGENTERNA som hölls får sitt släpp med `cancelled`.
        //
        // NOLLNINGEN FÖRST, KROKARNA SEDAN:
        // grabben och tangentminnet tas här, trycken i `dropPointer` — innan
        // drag-and-drop-avbrottet, `mouseCancel` och `onKeyUp` kör användarkod.
        grabStack_.clear();
        std::vector<KeyTarget> släpp = std::exchange(keyTargets_, {});
        if (!dropPointer())
            return;
        if (!releaseKeyTargets(std::move(släpp)))
            return;
        invalidate();
        break;
    }

    // MUSENS CAPTURE TOGS
    // (`gfx::EventType::MouseCaptureLost`): OS:et tog capture mitt i ett
    // tryck, och släppet kommer aldrig hit. Samma väg som fokusförlustens
    // tryck (`dropPointer` — nollningen, drag-and-drop-avbrottet och
    // `mouseCancel`), men grab, fokus, caret och tangenter rörs inte —
    // fönstret kan ha kvar fokus. Pekaren tillhör widgeten den står över igen.
    case gfx::EventType::MouseCaptureLost:
        if (!dropPointer())
            return;
        refreshCursor();
        invalidate();
        break;

    // Caret-timerns tick: växla blinkfasen hos fokuserad textwidget — den
    // invalidaterar ENBART sin caret-rect (finkornig damage, ingen full
    // omritning). Andra timer-id:n är appens egna: appen ser dem i
    // event()-hooken överst; ramverket rör dem inte.
    case gfx::EventType::Timer:
        if (e.timerId == kCaretTimerId) {
            if (focusedWidget_ && focusedWidget_->wantsTextInput() &&
                focusedWidget_->isVisible() && focusedWidget_->isEnabled()) {
                caretPhase_ = !caretPhase_;
                const Ctx c = ctxFor(*focusedWidget_);
                focusedWidget_->caretBlink(c, caretPhase_);
            } else {
                win_->stopTimer(kCaretTimerId);   // föräldralös — städa
            }
        } else if (e.timerId == kToolTipTimerId) {
            toolTipTimer();   // pekaren har VILAT (src/tooltip.cpp)
        } else if (e.timerId == kDragScrollTimerId) {
            dragScrollTick();   // autoskrollen
        }
        break;

    // Skrivtecken → grab-toppen FÖRE fokuserad widget (menyn tar text-
    // navigering), sedan fokuserad. Konsumerat = full omritning (samma
    // regel som keyDown: widgetens onChange-handler kan ha ändrat allt).
    case gfx::EventType::TextInput:
        // Ett pågående drag är modalt för tangentbordet (se KeyDown) — ett
        // tecken till ett fokuserat fält mitt i ett drag hade ändrat något
        // användaren inte tittar på.
        if (drag_.phase == DragPhase::Active)
            break;
        if (!grabStack_.empty() && grabStack_.back()->isEnabled()) {
            const bool tog = grabStack_.back()->textInput(e.text);
            if (dött())
                return;
            if (tog) {
                invalidate();
                break;
            }
        }
        if (focusedWidget_ && focusedWidget_->isVisible() &&
            focusedWidget_->isEnabled()) {
            const bool tog = focusedWidget_->textInput(e.text);   // onChange
            if (dött())
                return;
            if (tog)
                invalidate();
        }
        break;

    // Interaktions-vägarna nedan använder per-widget-damage (Widget::
    // invalidate — bara den ytan ritas om). Undantaget är användar-
    // handlers: de får mutera vilka fält som helst på vilka widgets som
    // helst (inga setters som kan rapportera), så efter en körd handler
    // begärs FULL omritning.

    case gfx::EventType::MouseMotion: {
        // TRYCKEN APPEN TOG först av allt: målet får rörelsen
        // genom `appPress(Move)` var pekaren än står, en gång per knapp — och
        // aldrig genom hover-kroken nedan.
        for (const gfx::MouseButton b : {gfx::MouseButton::Left,
                                         gfx::MouseButton::Middle,
                                         gfx::MouseButton::Right}) {
            if (!appPressFor(b)->taken)
                continue;
            appPressStep(b, e.mouseX, e.mouseY, Widget::AppPressStep::Move);
            if (dött())
                return;
        }
        // MITTEN- OCH HÖGERDRAGET FÖRE
        // allt annat: widgeten som tog trycket får rörelsen var pekaren än
        // står — också under explicit capture och ett pågående drag-and-drop,
        // som bryter av längre ner. Bara när den är aktiverad, som `mouseDrag`.
        // `fångadFick` minns om den FÅNGANDE redan fått rörelsen den vägen.
        bool fångadFick = false;
        if (Widget* m = middleWidget_; m && m->isEnabled()) {
            fångadFick = m == capturedWidget_;
            m->middleDrag(ctxFor(*m), e.mouseX, e.mouseY);
            if (dött())
                return;
        }
        if (Widget* r = rightWidget_; r && r->isEnabled()) {
            fångadFick = fångadFick || r == capturedWidget_;
            r->rightDrag(ctxFor(*r), e.mouseX, e.mouseY);
            if (dött())
                return;
        }
        // Explicit capture: ALL mus går till den capturande widgeten —
        // ingen hover-vandring (som WinForms-capture). KNAPPMEDVETEN:
        // har den fångande
        // redan fått rörelsen som mitten- eller högerdrag får den ingen
        // `mouseDrag` till för samma rörelse — utom med vänsterknappen nere,
        // vars rörelse `mouseDrag` är. EN rörelse per rörelse och knapp.
        if (capturedWidget_) {
            const bool vänster =
                (buttonsDown_ & mouseBit(gfx::MouseButton::Left)) != 0;
            if (capturedWidget_->isEnabled() && (!fångadFick || vänster)) {
                const Ctx wc = ctxFor(*capturedWidget_);
                capturedWidget_->mouseDrag(wc, e.mouseX, e.mouseY);
                if (dött())
                    return;
            }
            updateCursor(nullptr, e.mouseX, e.mouseY);
            break;
        }
        // DRAG-AND-DROP — FÖRE hover och mouseDrag. Ett aktivt
        // drag äger pekaren: målvalet ersätter hovern (ett mål under lasten
        // är inte "hovrat", det är ett släppmål — som i OS:ens egna drag),
        // och källans mouseDrag står still. Ett tryck på en `allowDrag`-
        // widget som passerar tröskeln BLIR ett drag här, och rörelsen är
        // då dragets första.
        if (drag_.phase == DragPhase::Active) {
            dragMotion(e.mouseX, e.mouseY);
            break;
        }
        if (drag_.phase == DragPhase::Pending &&
            dragThresholdPassed(e.mouseX, e.mouseY)) {
            const bool började = dragBegin(e.mouseX, e.mouseY);
            if (dött())
                return;
            if (började)
                break;
        }
        // Drag-kroken först: nedtryckt widget följer musen (caret-drag i
        // TextInput m.m.) även utanför sin yta — auto-capture-beteendet.
        // Ctx:t är widgetens containers (ctxFor), aldrig rå makeCtx.
        if (pressedWidget_ && pressedWidget_->isEnabled()) {
            const Ctx wc = ctxFor(*pressedWidget_);
            pressedWidget_->mouseDrag(wc, e.mouseX, e.mouseY);   // reglagets onChange
            if (dött())
                return;
        }
        const Ctx c    = makeCtx();
        Widget*   over = widgetAt(e.mouseX, e.mouseY, c);
        std::vector<HoverByte> byten;
        forEachWidget(root_, [&](Widget& w) {
            const bool now = (&w == over) && w.isEnabled();
            if (now != w.hovered_) {
                w.hovered_ = now;
                w.invalidate();
                if (w.onHover)
                    byten.push_back({&w, Widget::HandlerGuard(w), now});
            }
        });
        if (!byten.empty()) {
            if (!fyraHover(byten))
                return;
            over = widgetAt(e.mouseX, e.mouseY, makeCtx());   // trädet kan ha ändrats
        }
        // Hover-kroken: widgeten under pekaren följer rörelsen (hover-rad
        // i en lista). Kontraktet (hård regel 1): kroken invalidaterar
        // BARA vid faktisk ändring — rörelse utan ändring ritar inget. Målet
        // för ett tryck appen tog frågas inte: det fick rörelsen överst
        // (`appPress`) — EN rörelse per rörelse.
        if (over && over->isEnabled() && !holdsTakenPress(over)) {
            const Widget::HandlerGuard vakt(*over);
            const Ctx                  wc = ctxFor(*over);
            over->mouseMove(wc, e.mouseX, e.mouseY);   // ritytans onMouseMove
            if (dött())
                return;
            if (!vakt.ok())
                over = widgetAt(e.mouseX, e.mouseY, makeCtx());
        }
        // MUSPEKAREN — efter hover-kroken, så en widget som räknar
        // sin hover-del i `mouseMove` svarar ur samma läge i `cursorAt`.
        updateCursor(over, e.mouseX, e.mouseY);
        // TOOLTIPEN — vila räknas från den här rörelsen.
        toolTipMotion(over, e.mouseX, e.mouseY);
        break;
    }

    // TRE KNAPPAR, och utflyttningen ur `Left`-villkoret är EXAKT
    // avgränsad: bara `over`-beräkningen och GRAB-OUTSIDE
    // gäller alla. Capture, fokussättning, `pressedWidget_`/`pressed_` och
    // `mouseDown`-kroken stannar Left-only — mer hade varit farligt för den
    // befintliga capture- och tryckbokföringen; mittenknappen
    // och högerknappen har var sin EGEN
    // bokföring, `middleWidget_` och `rightWidget_`.
    //
    // Skälet att grab-outside måste gälla båda: ett HÖGERtryck utanför en
    // öppen meny ska också stänga den, och `App::dispatch`s
    // popup-stängning är redan knappagnostisk — de två vägarna måste vara
    // överens, annars stänger den ena men inte den andra.
    case gfx::EventType::MouseButtonDown: {
        // Ett tryck med en ANNAN knapp mitt i ett drag sväljs: draget äger
        // pekaren tills vänsterknappen släpps.
        if (drag_.phase == DragPhase::Active)
            break;
        const Ctx c    = makeCtx();
        Widget*   over = widgetAt(e.mouseX, e.mouseY, c);

        // Grab-kroken: tryck utanför grab-toppens delträd meddelas
        // toppen (menyn stänger sig själv) — klicket går sedan
        // vidare som vanligt.
        if (!grabStack_.empty() && (!over || !inGrabbedSubtree(over))) {
            MouseDownOutsideArgs a;
            a.x = e.mouseX;
            a.y = e.mouseY;
            grabStack_.back()->mouseDownOutside(a);
            if (dött())
                return;
            invalidate();   // kroken kan ha stängt/ändrat vad som helst
            // …och då kan widgeten under pekaren vara en annan.
            over = widgetAt(e.mouseX, e.mouseY, makeCtx());
        }

        // MITTEN OCH HÖGER, APPENS DEL: ett tappat släpp med
        // samma knapp avbryts först (`cancelLostPress` — widgetens eget tryck,
        // sedan appens), och sedan får tryckets MÅL `onMouseDown`. Tar appen
        // trycket hör det till målet: ingen bubbling genom `middleDown`/
        // `rightDown` och ingen snabbmeny, och dubbelklicket räknas på målet.
        if (e.button == gfx::MouseButton::Middle ||
            e.button == gfx::MouseButton::Right) {
            const gfx::MouseButton b = e.button;
            ClickTrack& följd = clicks_[b == gfx::MouseButton::Right ? 2 : 1];
            if (cancelLostPress(b)) {
                if (dött())
                    return;
                over = widgetAt(e.mouseX, e.mouseY, makeCtx());
            }
            Widget* const mål = mouseTarget(over);
            const AppDown app = appMouseDown(b, mål, e.mouseX, e.mouseY);
            if (app == AppDown::Död)
                return;
            if (app == AppDown::Slut) {
                följd.widget = nullptr;   // ingen följd för ett tryck som tog slut
                pekareNu(e.mouseX, e.mouseY);
                break;
            }
            if (app == AppDown::Taget) {
                const bool dubbel = countClick(följd, mål, e.mouseX, e.mouseY);
                const bool lever  = appPressStep(b, e.mouseX, e.mouseY,
                                                 Widget::AppPressStep::Begin);
                if (dött())
                    return;
                if (lever && dubbel) {
                    appPressStep(b, e.mouseX, e.mouseY,
                                 Widget::AppPressStep::DoubleClick);
                    if (dött())
                        return;
                }
                pekareNu(e.mouseX, e.mouseY);   // målet äger pekaren
                break;
            }
            // Otaget: trycket bubblar — från widgeten under pekaren
            // NU, för en handler kan ha ändrat trädet.
            over = widgetAt(e.mouseX, e.mouseY, makeCtx());
        }

        if (e.button == gfx::MouseButton::Right) {
            // HÖGERTRYCKET — mittenknappens form, och
            // lika lite som den rör det vänsterknappens bokföring: ingen
            // capture, inget fokusbyte, ingen `pressed_`. Trycket BUBBLAR
            // genom `rightDown`; den som tar det äger draget och släppet, och
            // dubbelklicket räknas på den.
            const bool lever =
                routeAuxDown(gfx::MouseButton::Right, over, e.mouseX, e.mouseY);
            if (dött())
                return;
            if (!lever) {
                // Trycket tog slut under en krok (en modal dialog i kroken gav
                // fokusförlust): ingen snabbmeny och inget klick att räkna.
                clicks_[2].widget = nullptr;
                pekareNu(e.mouseX, e.mouseY);
                break;
            }
            if (Widget* r = rightWidget_) {
                if (countClick(clicks_[2], r, e.mouseX, e.mouseY)) {
                    r->rightDoubleClick(ctxFor(*r), e.mouseX, e.mouseY);
                    if (dött())
                        return;
                }
                pekareNu(e.mouseX, e.mouseY);   // trycket äger pekaren
                break;
            }
            // INGEN TOG TRYCKET: snabbmenyn vid trycket — för
            // widgeten under pekaren NU, för en `rightDown` kan ha kört en
            // handler som ändrade trädet. Följden bryts: ett tryck ingen tog
            // är inget första klick.
            clicks_[2].widget = nullptr;
            const bool meny = fireContextMenu(
                widgetAt(e.mouseX, e.mouseY, makeCtx()), e.mouseX, e.mouseY);
            if (dött())
                return;
            // SNABBMENYN TOG TRYCKET: appens mål får
            // `onMouseCancel` — menyn tar pekaren, och släppet kommer olika på
            // olika plattformar.
            if (meny && !appMouseCancel(gfx::MouseButton::Right))
                return;
            break;
        }
        if (e.button == gfx::MouseButton::Middle) {
            // MITTENKNAPPEN — egen bokföring
            // (`middleWidget_`), och lika lite som högerknappen rör den
            // vänsterknappens: inget fokusbyte, ingen `pressed_`. Grab-outside
            // ovan gällde redan. DUBBELKLICKET räknas på den som TOG trycket,
            // och ett tryck ingen tog bryter följden.
            const bool lever =
                routeAuxDown(gfx::MouseButton::Middle, over, e.mouseX, e.mouseY);
            if (dött())
                return;
            if (!lever) {
                clicks_[1].widget = nullptr;   // trycket tog slut under en krok
            } else if (Widget* m = middleWidget_) {
                if (countClick(clicks_[1], m, e.mouseX, e.mouseY)) {
                    m->middleDoubleClick(ctxFor(*m), e.mouseX, e.mouseY);
                    if (dött())
                        return;
                }
            } else {
                clicks_[1].widget = nullptr;
            }
            pekareNu(e.mouseX, e.mouseY);   // trycket äger pekaren
            break;
        }
        if (e.button != gfx::MouseButton::Left)
            break;

        // ETT TAPPAT VÄNSTERSLÄPP:
        // ett nytt tryck medan den förra tryckta står kvar betyder att släppet
        // aldrig kom (appens krok åt det, eller backenden tappade det). Den
        // gamla släcks och får `mouseCancel(Left)` — och appens mål sitt
        // `onMouseCancel` — innan det nya routas, och widgeten under
        // pekaren räknas om efter dem.
        if (cancelLostPress(gfx::MouseButton::Left)) {
            if (dött())
                return;
            over = widgetAt(e.mouseX, e.mouseY, makeCtx());
        }

        // MÅLET och DUBBELKLICKET på det (regeln i
        // `countClick`; målet är widgeten under pekaren, den fångande eller på
        // tom yta roten).
        Widget* const mål    = mouseTarget(over);
        const bool    dubbel =
            mål && countClick(clicks_[0], mål, e.mouseX, e.mouseY);

        // FOKUSFLYTTEN FÖRE appens handler (fokus är fönstrets), för en
        // widget under pekaren; explicit capture flyttar inte fokus.
        // GAV TRYCKET FOKUS? Frågan är `focusedWidget_` före och efter,
        // så ett ombud räknas (talfältets stegare ger fältet fokus) — textfälten
        // läser svaret i sin `mouseDown` (widget.h).
        if (!capturedWidget_ && over && over->isEnabled()) {
            Widget* const fokusFöre = focusedWidget_;
            if (over->focusable())
                setFocus(over);   // klick flyttar fokus (WinForms-mönstret)
            over->pressGaveFocus_ = focusedWidget_ != fokusFöre;
        }

        // APPENS `onMouseDown` före widgetens eget tryck.
        const AppDown app =
            appMouseDown(gfx::MouseButton::Left, mål, e.mouseX, e.mouseY);
        if (app == AppDown::Död)
            return;
        if (app == AppDown::Slut) {
            pekareNu(e.mouseX, e.mouseY);
            break;
        }
        if (app == AppDown::Taget) {
            // APPEN TOG TRYCKET: widgeten får ingenting av det — inget
            // tryck, ingen `pressed()`, inget dubbelklick och inget
            // drag-and-drop. `appPress` säger att trycket finns.
            const bool lever = appPressStep(gfx::MouseButton::Left, e.mouseX,
                                            e.mouseY, Widget::AppPressStep::Begin);
            if (dött())
                return;
            if (lever && dubbel) {
                appPressStep(gfx::MouseButton::Left, e.mouseX, e.mouseY,
                             Widget::AppPressStep::DoubleClick);
                if (dött())
                    return;
            }
            pekareNu(e.mouseX, e.mouseY);   // målet äger pekaren
            break;
        }

        // Explicit capture: trycket går till den capturande widgeten,
        // inte till den under pekaren.
        if (capturedWidget_) {
            if (capturedWidget_->isEnabled()) {
                const Ctx wc = ctxFor(*capturedWidget_);
                capturedWidget_->mouseDown(wc, e.mouseX, e.mouseY);
            }
            break;
        }

        if (over && over->isEnabled()) {
            pressedWidget_  = over;
            over->pressed_  = true;
            over->invalidate();   // bara tryck-utseendet — ingen handler
            const Ctx wc = ctxFor(*over);
            over->mouseDown(wc, e.mouseX, e.mouseY);   // caret-placering m.m.
            // KROKEN KAN HA KÖRT EN HANDLER — reglagets `onChange`,
            // egenskapslistans commit vid ett klick på en annan rad — som rev
            // FÖNSTRET (då slutar vägen här, utan att röra en medlem), eller
            // tog widgeten ur trädet: då nollade `detachTree` trycket, och
            // `over` får inte röras igen.
            if (dött())
                return;
            if (pressedWidget_ != over) {
                pekareNu(e.mouseX, e.mouseY);
                break;
            }
            // Dubbelklicket kommer EFTER trycket, som i WinForms och Qt: det
            // andra trycket har redan valt raden när aktiveringen frågar.
            if (dubbel) {
                over->mouseDoubleClick(wc, e.mouseX, e.mouseY);   // onActivate
                if (dött())
                    return;
                if (pressedWidget_ != over) {
                    pekareNu(e.mouseX, e.mouseY);
                    break;
                }
            }
            // En KÄLLA (`allowDrag`) blir en kandidat: trycket är ett
            // vanligt tryck tills pekaren rört sig mer än tröskeln.
            dragArm(over, e.mouseX, e.mouseY);
        }
        // Trycket kan ha bytt vem som äger pekaren.
        updateCursor(over, e.mouseX, e.mouseY);
        break;
    }

    // Hjul-routing (första konsumenten av MouseWheels muspos): widgeten
    // under pekaren får hjulet, okonsumerat bubblar det via parent()-
    // kedjan till närmsta scrollbara förälder. Ingen användar-handler
    // körs — konsumenten invalidaterar sig själv (panelen = EN rect).
    // Explicit capture tar hjulet rakt av.
    case gfx::EventType::MouseWheel: {
        // RULLNINGENS RIKTNING räknas PER WIDGET
        // (`wheelFor`): en rityta som inte tar hjulet låter föräldern rulla
        // med FÖRÄLDERNS vändning.
        if (capturedWidget_) {
            if (capturedWidget_->isEnabled()) {
                const Ctx         wc = ctxFor(*capturedWidget_);
                const gfx::Pointf r  = wheelFor(*capturedWidget_, e);
                capturedWidget_->mouseWheel(wc, e.mouseX, e.mouseY, r.x, r.y);
            }
            break;
        }
        const Ctx c = makeCtx();
        for (Widget* w = widgetAt(e.mouseX, e.mouseY, c); w && w != &root_;
             w = w->parent()) {
            if (!w->isEnabled())
                continue;
            // Ett hjulsteg kan köra en handler (reglagets, talfältets): efter
            // den frågas fönstret, och `w->parent()` bara om widgeten står kvar.
            const Widget::HandlerGuard vakt(*w);
            const Ctx                  wc  = ctxFor(*w);
            const gfx::Pointf          r   = wheelFor(*w, e);
            const bool tog = w->mouseWheel(wc, e.mouseX, e.mouseY, r.x, r.y);
            if (dött())
                return;
            if (tog || !vakt.ok())
                break;
        }
        // Hjulet RULLAR under ett drag (en lång lista man vill släppa
        // längre ner i), och då har innehållet under pekaren flyttat sig
        // utan en enda musrörelse — målvalet görs om på samma punkt.
        if (drag_.phase == DragPhase::Active)
            dragMotion(e.mouseX, e.mouseY);
        break;
    }

    // STYRPLATTANS NYPNING OCH VRIDNING — målet väljs vid Begin och
    // hålls sekvensen ut (`routeGesture`). Som hjulet kör den ingen
    // användar-handler själv; konsumenten invaliderar sig själv, och en som
    // kör en handler (ritytans `onCameraChange`) ber om full omritning. Och
    // som hjulet kan den flytta innehållet under ett pågående drag.
    case gfx::EventType::Gesture:
        routeGesture(e);
        if (dött())
            return;
        if (drag_.phase == DragPhase::Active)
            dragMotion(e.mouseX, e.mouseY);
        break;

    case gfx::EventType::MouseButtonUp: {
        // APPENS SLÄPP FÖRST — `onMouseUp` till tryckets mål,
        // före widgetens eget släpp och med `pressed()` redan släckt (den
        // tryckta står kvar i `pressedWidget_`, så att en handler som tar bort
        // den nollar fältet). `släpp.handled` = appen TOG släppet: höll målet
        // själv trycket slutar det för det som ett avbrott nedan.
        if (e.button == gfx::MouseButton::Left)
            if (Widget* p = pressedWidget_) {
                p->pressed_ = false;
                p->invalidate();
            }
        // MITTEN- OCH HÖGERSLÄPPET — till
        // widgeten som tog trycket, EN gång, var pekaren än står och även om den
        // inaktiverats. Fältet nollas FÖRE krokarna — appens och widgetens: där
        // är `middlePressed()`/`rightPressed()` släckt (`mouseUp`s `pressed()`),
        // och en handler som tar bort widgeten lämnar ingen pekare efter sig
        // (vakten, tagen före appens handler).
        Widget* aux = nullptr;
        if (e.button == gfx::MouseButton::Middle ||
            e.button == gfx::MouseButton::Right) {
            ++auxSeq(e.button);   // trycket är slut (också ett som routas just nu)
            aux                   = auxWidget(e.button);
            auxWidget(e.button)   = nullptr;
        }
        const std::optional<Widget::HandlerGuard> auxVakt =
            aux ? std::optional<Widget::HandlerGuard>(Widget::HandlerGuard(*aux))
                : std::nullopt;
        AppUp släpp;
        if (appPressFor(e.button) &&
            !appMouseUp(e.button, e.mouseX, e.mouseY, släpp))
            return;
        if (aux) {
            if (auxVakt->ok()) {
                // Appen tog släppet från målet som höll trycket själv: det
                // slutar som ett AVBROTT. En förfader som tog trycket
                // genom bubblingen rörs inte.
                const bool körde =
                    släpp.handled && släpp.target == aux
                        ? aux->mouseCancel(e.button)
                        : e.button == gfx::MouseButton::Right
                              ? aux->rightUp(ctxFor(*aux), e.mouseX, e.mouseY)
                              : aux->middleUp(ctxFor(*aux), e.mouseX, e.mouseY);
                if (dött())
                    return;
                if (körde)
                    invalidate();   // en handler kördes — som efter ett klick
            }
        }
        if (e.button != gfx::MouseButton::Left) {
            pekareNu(e.mouseX, e.mouseY);
            break;
        }
        // DRAG-AND-DROP: ett aktivt drag SLUTAR här och ger inget
        // klick — trycket förbrukades när draget startade. Ett Pending-tryck
        // släpps bara, och klicket nedan kommer som vanligt.
        const bool varDrag = drag_.phase == DragPhase::Active;
        dragRelease(e.mouseX, e.mouseY);   // målets och källans handlers
        if (dött())
            return;
        if (varDrag)
            break;
        // SLÄPPET — `mouseUp` till den som håller EXPLICIT capture
        // (den överlever knappsläppet och släpper själv) och till den som
        // TOG EMOT trycket, en gång var. Före `clicked()`, med `pressed()`
        // släckt, och oavsett var pekaren står — kontraktet i widget.h.
        // Svarar kroken true har den kört en egen handler (skiktets
        // `onChange`), och då ritas allt om — samma garanti som efter ett
        // klick. Ett släpp som bara rörde widgetens eget tillstånd kostar
        // bara dess egen yta (regel 1).
        if (Widget* fångad = capturedWidget_;
            fångad && fångad != pressedWidget_) {
            const bool körde = fångad->mouseUp(ctxFor(*fångad), e.mouseX,
                                               e.mouseY);
            if (dött())
                return;
            if (körde)
                invalidate();
        }
        // `pressedWidget_` läses FÖRST NU och står kvar UNDER kroken: en
        // handler (i kroken ovan eller i den nedan) kan ta bort den tryckta
        // ur trädet, och då nollar `detachTree` fältet — det är så
        // borttagningen syns, aldrig genom en lokal kopia. Och efter varje
        // krok frågas fönstret först: en handler får ha rivit det.
        if (Widget* was = pressedWidget_) {
            was->pressed_ = false;
            was->invalidate();
            if (släpp.handled && släpp.target == was) {
                // APPEN TOG SLÄPPET: den tryckta får varken
                // `mouseUp` eller klick — trycket slutar för den som ett
                // avbrott: delarens skikt återgår, ett reglage committar inte.
                pressedWidget_   = nullptr;
                const bool körde = was->mouseCancel(gfx::MouseButton::Left);
                if (dött())
                    return;
                if (körde)
                    invalidate();
            } else {
                const bool körde = was->mouseUp(ctxFor(*was), e.mouseX, e.mouseY);
                if (dött())
                    return;
                if (körde)
                    invalidate();
                const bool kvar = pressedWidget_ == was;
                pressedWidget_  = nullptr;
                const Ctx c = makeCtx();
                if (kvar && widgetAt(e.mouseX, e.mouseY, c) == was) {
                    was->clicked();   // klick = ner och upp på samma widget
                    if (dött())
                        return;
                    invalidate();     // onClick/onChange kan ha ändrat allt
                }
            }
        }
        // Trycket är slut: pekaren tillhör widgeten den står över igen.
        pekareNu(e.mouseX, e.mouseY);
        break;
    }

    case gfx::EventType::MouseLeave:
        // Släcker BARA hovern. Ett pågående tryck/drag (pressedWidget_)
        // lever vidare: backenden håller mus-capture, så MouseButtonUp
        // kommer alltid — och drag-selektion i en textwidget ska kunna
        // fortsätta utanför fönsterkanten. (Att nolla trycket här skulle bryta
        // draget så fort musen lämnade fönstret.) Ett drag-and-drop lever
        // vidare av samma skäl, men utanför fönstret finns inget mål.
        dragOutside();   // målets onDragLeave
        if (dött())
            return;
        {
            std::vector<HoverByte> byten;
            forEachWidget(root_, [&](Widget& w) {
                if (w.hovered_) {
                    w.invalidate();
                    if (w.onHover)   // entered = false
                        byten.push_back({&w, Widget::HandlerGuard(w), false});
                }
                w.hovered_ = false;
                if (&w != pressedWidget_)
                    w.pressed_ = false;
            });
            if (!fyraHover(byten))
                return;
        }
        // MUSPEKAREN: ingen widget står under pekaren längre, så
        // formen går tillbaka till pilen — annars visade ett fönster som
        // blockerats av en modal dialog (som inte får några rörelser) den
        // form den senast hade, t.ex. ett textfälts I-balk, över hela ytan.
        // Ett pågående tryck — också ett mitten- eller högertryck — äger
        // pekaren och behåller sin form. Fönstrets VÄNTA-PEKARE gäller hela
        // fönstret, också utan en widget under pekaren.
        if (!pressedWidget_ && !capturedWidget_ && !middleWidget_ &&
            !rightWidget_ && !takenPressOwner())
            win_->setCursor(useWaitCursor_ ? gfx::PointerShape::Wait
                                           : gfx::PointerShape::Arrow);
        break;

    case gfx::EventType::KeyDown:
        // ETT PÅGÅENDE DRAG är modalt för tangentbordet — OLE:s
        // egen drag-loop gör likadant. Escape avbryter, allt annat sväljs:
        // en Tab som flyttade fokus eller en Delete som raderade i ett fält
        // mitt i ett drag hade ändrat något användaren inte tittar på.
        if (drag_.phase == DragPhase::Active) {
            if (e.key == gfx::Key::Escape)
                dragCancel();
            break;
        }
        // NUMPADENS NAVIGATIONSLÄGE VIKS HÄR: `KP_End` blir
        // `End` innan menyn, grab-toppen och den fokuserade widgeten får
        // tangenten — EN plats i stället för en rad i varje widgets
        // keyDown, så att varje widget, även nästa, navigerar med numpaden.
        // Appens event()-krok (ovan) och acceleratorerna (nedan) får den
        // OVIKTA posten: där ÄR skillnaden poängen ("bind både End och
        // KP_End", kontraktet vid `enum class Key`).
        // ÖPPEN MENY vinner över allt utom appens egen event()-hook: en
        // meny är modal för tangentbordet så länge den hänger. Kroken
        // sätts av MenuPresentation och nollas när kedjan stängs — den
        // finns för de plattformar där popupen är `noActivate` och
        // tangenterna därför landar HÄR i stället för i popupfönstret.
        // Kroken anropas genom en KOPIA: menyns aktivering i den får stänga
        // kedjan, och då nollas fältet medan kroken ännu kör.
        if (menuKeyHook_) {
            const std::function<bool(gfx::Key, std::uint32_t)> krok =
                menuKeyHook_;
            const bool tog = krok(gfx::navigationKey(e.key), e.mods);
            if (dött())
                return;
            if (tog) {
                invalidate();
                break;
            }
        }
        // Grab-toppen FÖRE fokuserad widget (menyns pilnavigering) — appens
        // event()-hook fick redan chansen överst (GlazeBuilders
        // Delete/Ctrl+S går före). Okonsumerad Tab
        // vandrar fokus i DFS-ordningen. Efter VARJE krok frågas fönstret:
        // en commit eller ett menyval får ha rivit det.
        if (!grabStack_.empty() && grabStack_.back()->isEnabled()) {
            const bool tog =
                grabStack_.back()->keyDown(gfx::navigationKey(e.key), e.mods);
            if (dött())
                return;
            if (tog) {
                invalidate();   // konsumerad tangent kan ha kört handlers
                break;
            }
        }
        // FOKUSENHETEN — `onKeyDown` innerst först, FÖRE
        // den fokuserade widgetens `keyDown`: den fokuserade och de närmaste
        // kompositerna i rad (`keyUnit`), eller roten när ingen har fokus.
        // MINNET får tryckets post FÖRE handlarna — en modal dialog som en
        // handler öppnar ger fönstret fokusförlust, och det släppet ska hitta
        // posten — och en kvarglömd post (släppet åts av `event()`) slängs när
        // samma tangent trycks igen. OS:ets UPPREPNING går till POSTENS kedja
        // och inte till fokus nu:
        // den som får upprepningens `onKeyDown` får också släppet, var
        // fokus än flyttat — tryckets handlers äger hela tangentslaget, medan
        // den inbyggda vägen nedan (`keyDown`, acceleratorerna) är fokusens.
        // En upprepning UTAN post (trycket åts eller gjordes i ett annat
        // fönster) skapar posten, så också den kedjan får sitt släpp.
        // `Key::Unknown` (modifierarna) paras aldrig. En öppen popup i enheten
        // tar tangenterna, och då hoppas handlarna över — vid släppet också om
        // den var öppen vid trycket. `handled` ger `keyTaken` och stoppar allt;
        // en handler som flyttar fokus utan `handled` stoppar också — den
        // inbyggda vägen fortsätter aldrig i en annan widget. En körd handler
        // ritar om hela fönstret.
        {
            const gfx::Key   vikt = gfx::navigationKey(e.key);
            const KeyTarget* slag = upprepning ? findKeyTarget(e.key) : nullptr;
            KeyChain         kedja;
            bool             popup = false;
            std::uint64_t    post  = 0;
            if (slag) {
                kedja = slag->chain;   // en KOPIA — minnet kan ändras under handlarna
                popup = slag->skipped;
                post  = slag->id;
            } else {
                kedja = currentKeyUnit();
            }
            for (const KeyLink& l : kedja)   // vakten först: posten kan vara gammal
                popup = popup || (l.guard.ok() && l.w->hasOpenPopup());
            if (!slag && e.key != gfx::Key::Unknown && !kedja.empty()) {
                takeKeyTarget(e.key);
                post = ++keyTargetIds_;
                keyTargets_.push_back(KeyTarget{e.key, vikt, kedja, 0, popup, post});
            }
            if (!popup) {
                Widget* const fokusFöre = focusedWidget_;
                bool          körde     = false;
                bool          tagen     = false;
                for (std::size_t i = 0; i < kedja.size(); ++i) {
                    const KeyLink& l = kedja[i];
                    if (!l.guard.ok() || !l.w->onKeyDown)
                        continue;
                    KeyArgs a;
                    a.key    = vikt;
                    a.mods   = e.mods;
                    a.repeat = upprepning;
                    a.text   = e.text;   // texten trycket skriver
                    l.w->noteHandlerRun();
                    l.w->onKeyDown(l.w, a);
                    körde = true;
                    if (dött())
                        return;
                    if (a.handled) {
                        // MOTTAGAREN: den som tog
                        // tangenten får släppets `keyUp` — posten letas upp
                        // igen, för en handler kan ha tömt minnet.
                        for (KeyTarget& t : keyTargets_)
                            if (post != 0 && t.id == post)
                                t.recipient = i;
                        if (l.guard.ok())
                            l.w->keyTaken(vikt);
                        tagen = true;
                        break;
                    }
                    if (focusedWidget_ != fokusFöre)
                        break;
                }
                if (körde)
                    invalidate();   // handlern kan ha ändrat vad som helst
                if (tagen || focusedWidget_ != fokusFöre)
                    break;
            }
        }
        if (focusedWidget_ && focusedWidget_->isVisible() &&
            focusedWidget_->isEnabled()) {
            const bool tog =
                focusedWidget_->keyDown(gfx::navigationKey(e.key), e.mods);
            if (dött())
                return;
            if (tog) {
                invalidate();   // konsumerad tangent kan ha kört handlers
                break;
            }
        }
        // FÖRFÄDERNA — en tangent den fokuserade widgeten tackade
        // nej till erbjuds dess förfäder, närmast först, genom den SKILDA
        // kroken `descendantKeyDown` (kontraktet i widget.h). Flikkontrollens
        // Ctrl+Tab bor där. Efter widgeten och FÖRE acceleratorerna: fokus
        // närmast vinner, samma kollisionsregel som nedan.
        // `break` direkt efter träffen och aldrig `f->parent()` efter
        // anropet: kroken kan ha kört en handler som tog förfadern ur
        // trädet.
        if (focusedWidget_) {
            bool tagen = false;
            for (Widget* f = focusedWidget_->parent(); f; f = f->parent()) {
                if (!f->isVisible() || !f->isEnabled())
                    continue;
                const Widget::HandlerGuard vakt(*f);
                tagen = f->descendantKeyDown(gfx::navigationKey(e.key), e.mods);
                if (dött())
                    return;
                if (tagen || !vakt.ok())
                    break;
            }
            if (tagen) {
                invalidate();   // kroken kan ha bytt sida och flyttat fokus
                break;
            }
        }
        // TEXT VINNER ÖVER GENVÄGEN (som Qt:s
        // `ShortcutOverride`) — kollisionsregeln nedan förlängd till tecknen:
        // ett tryck som SKRIVER (KeyDown bär texten) i en fokuserad, synlig
        // och aktiverad widget som tar text når varken fönstrets genvägar,
        // ägarens reserv eller standardknappen, och tecknet skrivs.
        // Ctrl+S, F5, Delete och Enter har ingen text och går vidare; AltGr+2
        // (`@`) i ett fält kör ingen Ctrl+Alt+2. Skriven gräns: datagridens
        // "börja skriva" och ett skrivskyddat fält tar inte text och släpper
        // fortfarande en bokstavsgenväg, liksom en backend utan text.
        if (e.text[0] != '\0' && focusedWidget_ &&
            focusedWidget_->isVisible() && focusedWidget_->isEnabled() &&
            focusedWidget_->wantsTextInput())
            break;
        // ACCELERATORERNA — EFTER att den fokuserade
        // widgeten tackat nej och FÖRE Tab-vandringen. Platsen är hela
        // kollisionsregeln: en `TextInput`s Ctrl+A/Ctrl+C går till
        // FÄLTET, inte till menyn, eftersom fältet får frågan först. En
        // permanent grab hade vänt på det och stulit både pilar och text.
        {
            const bool tog = tryShortcut(e.key, e.mods);   // onClick
            if (dött())
                return;
            if (tog) {
                invalidate();
                break;
            }
        }
        // RESERVVÄGEN — ett ägt verktygsfönster låter ägarens
        // acceleratorer svara (`acceleratorFallback`).
        {
            const bool tog = acceleratorFallback(e.key, e.mods);
            if (dött())
                return;
            if (tog) {
                invalidate();
                break;
            }
        }
        // STANDARD- OCH AVBRYTKNAPPEN — SIST före Tab, så
        // allt närmare fokus vinner: en fokuserad knapp har redan tagit sitt
        // Enter, `TextArea` sin radbrytning, en öppen vallista eller popup sitt
        // Escape och en accelerator bunden till tangenten sin. Enter med Ctrl,
        // Alt eller Cmd är inte vår (`TextArea`s mängd — EN definition av
        // "modifierad Enter", och Ctrl+Enter når appen), Shift spelar ingen
        // roll; Escape tas bara omodifierad (meddelanderutans och
        // filväljarens vakt, som den här vägen ersatte).
        // En knapp som inte är användbar tar inte tangenten, och
        // `performClick` kör användarkod: efter den frågas fönstrets liv.
        // FÖRST erbjuds en ÖPPEN POPUPS kontroll tangenten, också när den
        // saknar fokus (`openPopupKeyTarget`):
        // Escape stänger popupen och inte dialogen, Enter väljer i den.
        {
            const bool enter =
                e.key == gfx::Key::Return &&
                (e.mods & (gfx::kModCtrl | gfx::kModAlt | gfx::kModCmd)) == 0;
            const bool escape = e.key == gfx::Key::Escape && e.mods == 0;
            if (enter || escape) {
                Widget* const p = openPopupKeyTarget();
                if (p && p->isEnabled()) {
                    const bool tog =
                        p->keyDown(gfx::navigationKey(e.key), e.mods);
                    if (dött())
                        return;
                    if (tog) {
                        invalidate();   // kontrollen kan ha kört handlers
                        break;
                    }
                }
            }
            Button* const knapp =
                enter ? liveAccept() : escape ? liveCancel() : nullptr;
            if (knapp && usable(*knapp)) {
                knapp->performClick();
                if (dött())
                    return;
                invalidate();   // en handler kördes — som efter ett klick
                break;
            }
            // POPUPEN SOM TAR TANGENTBORDET stänger på ett Escape
            // som ingenting i den tog — avbrytknappens plats, så en kontroll
            // i popupen (en öppen vallista, en redigerad cell) och en egen
            // avbrytknapp vinner. Uppskjuten som alltid; ägaren får veta det
            // i popupens `onClose`.
            if (escape && popup_ && popupTakesKeys_) {
                close();
                break;
            }
        }
        if (e.key == gfx::Key::Tab)
            moveFocus((e.mods & gfx::kModShift) ? -1 : +1);
        break;

    // TANGENTSLÄPPET — till SAMMA kedja som tryckets
    // `onKeyDown`, var fokus än står nu; posten togs ut före appens krok (ett
    // släpp kroken åt har slängt den). Utan post: i en POPUP som inte såg
    // trycket går släppet till ägaren som minns det (Wayland);
    // under ett pågående drag-and-drop sväljs det; annars
    // går det till fokusenheten som gäller nu, med den vikta tangenten.
    case gfx::EventType::KeyUp: {
        if (!släppPost) {
            if (popup_ && !höllsHär && forwardKeyUp(e))
                break;   // ägarens handlers får ha rivit fönstret — inget mer här
            if (drag_.phase == DragPhase::Active)
                break;
        }
        KeyTarget mål;
        KeyArgs   a;
        a.mods = e.mods;
        if (släppPost) {
            mål   = std::move(*släppPost);
            a.key = mål.key;
        } else {
            mål.chain = currentKeyUnit();
            a.key     = gfx::navigationKey(e.key);
        }
        if (!deliverKeyUp(mål, a))
            return;
        break;
    }

    default:
        break;
    }
}

} // namespace glaze
