// MenuBar — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3). Ytan är
// Bars (`fillBody`/`innerEdge` via `Bar::draw`), rubrikerna ritas här.
//
// TITLARNA ÄR DATA och inte barn, och det är en NÖDVÄNDIGHET:
// `AppWindow::collectFocusable` rekurserar OVILLKORLIGT i barnen, så en
// rubrik som widget hade blivit ett tabbstopp per meny. Samma val som
// ListBox och PropertyGrid gjorde för sina rader — och samma konsekvens:
// geometrin (`titleRect`) är EN sanning som ritningen OCH hit-testen läser.
//
// MENYRADEN LIGGER I KLIENTYTAN. TopBar är chrome och dess barn får
// hit-roller i `AppWindow::pushHitRegions`; menyraden hör inte dit, får
// ingen Caption-roll och kräver ingen ändring i hit-test-kartan.
//
// INTERAKTIONEN: klick på rubrik öppnar, klick igen stänger (App har då
// redan köat popup-stängningen — samma toggle-ordning som Dropdown), och
// HOVER på en annan rubrik medan en meny är öppen BYTER meny.

#include "glaze/widgets/menu_bar.h"

#include <algorithm>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../menu_path.h"

namespace glaze {

// Loggens avsändare i menystigens hjälpare (src/menu_path.h).
constexpr const char* kVem = "MenuBar";   // lang_gate:rad loggens avsändare, ingen användartext

GLAZE_REGISTER_WIDGET("menubar", MenuBar)

MenuBar::MenuBar()
{
    dock = Dock::Top;
}

MenuBar::~MenuBar() = default;

// --------------------------------------------------------------- menyer

Menu& MenuBar::addMenu(TrText title)
{
    Entry e;
    e.title = std::move(title);
    e.menu  = std::make_unique<Menu>();
    entries_.push_back(std::move(e));
    Menu& m = *entries_.back().menu;

    const int idx = static_cast<int>(entries_.size()) - 1;
    // NAVIGERINGEN mellan rubriker (Höger/Vänster på ROTNIVÅN) ägs av
    // raden, inte av menyn — menyn vet inget om sina syskon.
    m.stepSibling = [this, idx](int dir) {
        const int n = menuCount();
        if (n <= 1)
            return false;
        openMenu(((idx + dir) % n + n) % n);
        return true;
    };
    // Kedjan stängdes (klick utanför, Escape, en aktivering): släpp
    // rubriken. `open_` jämförs mot `idx` så en STÄNGNING som kommer efter
    // att en ANNAN rubrik redan öppnats inte släcker den nya.
    m.onChainClosed = [this, idx] {
        if (open_ == idx) {
            open_ = -1;
            invalidate();
        }
    };
    invalidate();
    return m;
}

const std::string& MenuBar::menuTitle(int i) const
{
    static const std::string tom;
    return (i >= 0 && std::size_t(i) < entries_.size())
               ? entries_[std::size_t(i)].title.str()
               : tom;
}

const TrText& MenuBar::menuTitleToken(int i) const
{
    static const TrText tom;
    return (i >= 0 && std::size_t(i) < entries_.size())
               ? entries_[std::size_t(i)].title
               : tom;
}

Menu* MenuBar::menuAt(int i)
{
    return (i >= 0 && std::size_t(i) < entries_.size())
               ? entries_[std::size_t(i)].menu.get()
               : nullptr;
}

void MenuBar::clearMenus()
{
    // REGEL 1: en tom menyrad som töms igen har inte ändrats, och då ska
    // ingen frame begäras. `closeMenu()` invaliderar själv om något var
    // öppet, så no-op-fallet blir helt tyst.
    if (entries_.empty() && open_ < 0 && hover_ < 0)
        return;
    closeMenu();
    entries_.clear();   // `Menu`s destruktor stänger sina presentationer
    open_  = -1;
    hover_ = -1;
    invalidate();
}

bool MenuBar::tryShortcut(gfx::Key k, std::uint32_t mods)
{
    for (Entry& e : entries_)
        if (e.menu->tryShortcut(k, mods, this))
            return true;
    return false;
}

void MenuBar::openMenu(int i)
{
    if (i < 0 || std::size_t(i) >= entries_.size())
        return;
    if (!appWindow() || !appWindow()->isOpen())
        return;
    if (open_ == i)
        return;   // redan uppe — ingen ny presentation

    const Ctx        c = behaviorCtx();
    const gfx::Rectf r = Draw::project(c, resolve(c), titleRect(c, i));
    // Ankaret är HELA rubrikrectet: backenden lägger popupen UNDER det,
    // alltså vänsterjusterad med rubriken och direkt under raden.
    // `popupAtRect` skapar en ny presentation; den gamla kedjan rivs av
    // `showOwned` → `closePopupsNow`, som lämnar target och target:s
    // ägarkedja i fred.
    // `open_` sätts FÖRE öppningen med flit: `popupAtRect` → `showOwned` →
    // `closePopupsNow` river den GAMLA kedjan SYNKRONT, och dess
    // `onChainClosed` jämför mot `open_` — hade fältet stått kvar på den
    // gamla rubriken hade den nya släckts direkt igen.
    open_ = i;
    entries_[std::size_t(i)].menu->popupAtRect(*this, r);
    // …och tas tillbaka om menyn inte kom upp (fönsterinit kan misslyckas),
    // så `openIndex()` aldrig ljuger.
    if (!entries_[std::size_t(i)].menu->isOpen())
        open_ = -1;
    invalidate();
}

void MenuBar::closeMenu()
{
    if (open_ < 0 || std::size_t(open_) >= entries_.size())
        return;
    entries_[std::size_t(open_)].menu->close();   // uppskjutet
    open_ = -1;
    invalidate();
}

// -------------------------------------------------------------- geometri
// EN sanning i widget-lokala LOGISKA px: ritningen läser den rakt av,
// hit-testen projicerar den med Draw::project.

gfx::Rectf MenuBar::titleRect(const Ctx& c, int i) const
{
    const gfx::Rectf b = Draw::bounds(c, resolve(c));
    float            x = titleStart(c);
    for (std::size_t k = 0; k < entries_.size(); ++k) {
        const float w =
            Draw::measure(c, entries_[k].title.str()).w + 2.0f * c.style->menuBarPadX;
        if (int(k) == i)
            return gfx::Rectf{x, b.y, w, b.h};
        x += w;
    }
    return gfx::Rectf{};
}

// Den STORA programikonen täcker namnlist + menyrad i
// VÄNSTRA hörnet på Windows och Linux, och fönstrets egen menyrad börjar då
// bredvid den. På macOS sitter ikonen till höger och rubrikerna börjar vid
// kanten. En menyrad som appen lagt någon annanstans har ingen ikon bredvid
// sig.
float MenuBar::titleStart(const Ctx& c) const
{
    AppWindow* w = appWindow();
    if (!w || &w->menuBar != this || w->appIcon.onRightSide())
        return 0.0f;
    return w->appIcon.reservedWidth(c, AppIcon::Row::Menu);
}

int MenuBar::titleAt(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf base = resolve(c);
    for (int i = 0; i < menuCount(); ++i)
        if (Draw::project(c, base, titleRect(c, i)).contains(mx, my))
            return i;
    return -1;
}

void MenuBar::setHover(int i)
{
    if (hover_ == i)
        return;
    hover_ = i;
    invalidate();   // hård regel 1: bara vid ÄNDRING
}

// -------------------------------------------------------------- ritning

gfx::Sizef MenuBar::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN med menyradens eget default-mått: satt höjd vinner,
        // osatt blir `menuRowH` (Bar skulle annars ge `barHeight`, som är
        // namnlistens mått — en menyrad är en rad MENYRADER hög).
        // Ingen auto-BREDD, precis som Bar: en menyrad är alltid dockad.
        gfx::Sizef s = Bar::contentSize(c);
        if (height <= 0.0f)
            s.h = c.style->menuRowH;
        return s;
    }
    }
}

void MenuBar::draw(const Ctx& c)
{
    // YTAN och hårlinjen mot innehållet är Bars — menyraden är samma
    // sorts remsa som topplisten och statusraden, och ska se ut så.
    Bar::draw(c);

    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    const bool  på  = isEnabled();

    for (int i = 0; i < menuCount(); ++i) {
        const gfx::Rectf r     = titleRect(c, i);
        const bool       öppen = i == open_;
        const bool       het   = på && (öppen || (hovered() && i == hover_));

        switch (c.ui) {
        // GLAZE och SLATE markerar rubriken med en YTA — samma
        // stark/svag-par som en listrad: öppen meny är den starka
        // markeringen, hovern den svaga.
        case UI::Glaze:
            if (öppen && på)
                Draw::rect(c, r, m.cornerRadius, col.accentSurface);
            else if (het)
                Draw::rect(c, r, m.cornerRadius, col.buttonHover);
            break;

        // MINIMALISTIC fyller inte — den markerar med ett STRECK i
        // underkanten, familjens rad-markör (`rowMarkerW`) lagd på tvären.
        case UI::Minimalistic:
            if (het)
                Draw::rect(c, {r.x, r.y + r.h - m.rowMarkerW, r.w,
                               m.rowMarkerW},
                           0.0f, col.accent);
            break;

        case UI::Slate:
        default:
            if (öppen && på)
                Draw::rect(c, r, m.cornerRadius, col.accent);
            else if (het)
                Draw::rect(c, r, m.cornerRadius, col.buttonHover);
            break;
        }

        const gfx::Color fg =
            !på                              ? col.textDim
            : c.ui == UI::Minimalistic       ? (het ? col.accent : col.text)
            : (öppen ? (c.ui == UI::Glaze ? col.onAccent : col.windowBg)
                     : col.text);
        Draw::text(c, r, entries_[std::size_t(i)].title.str(), fg, Align::Center);
    }
}

// ------------------------------------------------------------- beteende

void MenuBar::mouseDown(const Ctx& c, float mx, float my)
{
    const int i = titleAt(c, mx, my);
    if (i < 0)
        return;
    // TOGGLE. Ordningen är Dropdowns: `App::dispatch` har redan köat
    // popup-stängningen (uppskjutet) när trycket når oss, så `open_` är
    // fortfarande satt — vägen blir stängning, inte en omedelbar
    // återöppning.
    if (open_ == i)
        closeMenu();
    else
        openMenu(i);
}

void MenuBar::mouseMove(const Ctx& c, float mx, float my)
{
    const int i = titleAt(c, mx, my);
    setHover(i);
    // HOVER BYTER MENY, men bara när en redan är öppen — annars hade
    // menyerna öppnat sig av att musen passerade raden.
    if (open_ >= 0 && i >= 0 && i != open_)
        openMenu(i);
}

// ------------------------------------------------------ DEN PLATTA FORMEN
//
// HÅRD REGEL 8: designer och kod är likvärdiga vägar. Stigen är
// TITELVÄGEN, upplöst mot de faktiska titlarna — den läser som det den
// är, till skillnad från ett index. Stigen är
// SEGMENTERAD (`MenuPath`) och matchas TOKEN mot TOKEN. Matchningen,
// nedstigningen och add-kropparna är DELADE med programmenyn (`AppIcon`)
// och bor i src/menu_path.h — här är bara det som är
// menyradens eget: stigens FÖRSTA led är en RUBRIK.

// Menyn som stigens FÖRSTA led pekar ut, plus den vektor posterna ska in
// i. Returnerar nullptr när stigen inte finns — anroparen loggar.
std::vector<MenuItem>* MenuBar::resolvePath(const MenuPath& path)
{
    if (path.empty())
        return nullptr;   // toppnivån är menyer, inte poster; tom = ogiltig

    Menu*          m = nullptr;
    detail::SegSok sök;
    for (auto& e : entries_)
        if (detail::matchaSeg(path[0], e.title, sök))
            m = e.menu.get();
    if (!m) {
        if (sök.misstänktTypfel)
            detail::loggaTypfel(kVem, "resolvePath", path[0]);
        return nullptr;
    }
    return detail::descendMenu(m->items, path, 1, kVem);
}

MenuItem* MenuBar::itemAtPath(const MenuPath& path)
{
    if (path.size() < 2)
        return nullptr;
    std::vector<MenuItem>* v = resolvePath(path.parent());
    return v ? detail::findMenuItem(*v, path[path.size() - 1], kVem)
             : nullptr;
}

void MenuBar::addItem(const MenuPath& path, TrText text,
                      const std::string& shortcut)
{
    if (detail::addMenuItem(resolvePath(path), kVem, path,
                            std::move(text), shortcut))
        invalidate();
}

void MenuBar::addCheck(const MenuPath& path, TrText text, bool checked)
{
    if (detail::addMenuCheck(resolvePath(path), kVem, path,
                             std::move(text), checked))
        invalidate();
}

void MenuBar::addRadio(const MenuPath& path, TrText text, bool checked)
{
    if (detail::addMenuRadio(resolvePath(path), kVem, path,
                             std::move(text), checked))
        invalidate();
}

void MenuBar::addSeparator(const MenuPath& path)
{
    if (detail::addMenuSeparator(resolvePath(path), kVem, path))
        invalidate();
}

void MenuBar::addSubmenu(const MenuPath& path, TrText text)
{
    if (detail::addMenuSubmenu(resolvePath(path), kVem, path,
                               std::move(text)))
        invalidate();
}

// MENYPOSTENS TILLSTÅND — den delade implementationen i src/menu_path.h,
// med titelvägen som stig (`itemAtPath`: rubrik först, därefter posterna).
void MenuBar::setItemIcon(const MenuPath& path, ImageSource icon)
{
    detail::setMenuItemIcon(itemAtPath(path), kVem, path, std::move(icon));
}

void MenuBar::setItemEnabled(const MenuPath& path, bool on)
{
    detail::setMenuItemEnabled(itemAtPath(path), kVem, path, on);
}

void MenuBar::setItemChecked(const MenuPath& path, bool on)
{
    detail::setMenuItemChecked(itemAtPath(path), kVem, path, on);
}

void MenuBar::logMissingPath(const char* fn, const MenuPath& path)
{
    detail::logMissingMenuPath(kVem, fn, path);
}

bool MenuBar::setProp(const std::string& key, const std::string& value)
{
    // Menyernas INNEHÅLL byggs med de platta add-satserna ovan (regel 8),
    // inte med en propnyckel: en meny är en SEKVENS och ordningen bär
    // betydelse, medan en prop är ett värde. Höjden och basens nycklar
    // tar Bar.
    return Bar::setProp(key, value);
}

} // namespace glaze
