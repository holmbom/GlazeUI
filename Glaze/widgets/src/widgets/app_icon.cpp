// AppIcon — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3): draw()/
// contentSize() bor HÄR med switch (c.ui). Måtten är Styles
// (`appIconInset`/`appIconPlateInset`/`appIconRadius` och barernas höjder),
// plattans färger kapselns (`capsuleHover`/`capsulePress`) — ikonen är
// chrome bredvid fönsterknapparna och ska tändas som de.
//
// PLATSEN ÄR TILLSTÅNDSLÖS: den räknas ur namnlistens och menyradens
// FAKTISKA synlighet och höjd vid varje mätning, så `menuBar.show()`,
// ett stilbyte och `setTransparent()` slår igenom utan bokföring. Det som
// flyttar sig när platsen ändras — namnlistens motvikt, menyradens
// rubriker, namnlistens hårlinje — läser `reservedWidth`.
//
// BILDEN ritas skarpt: källan väljs ur valregeln (den lilla i sin storlek,
// den stora över den) och räknas om på CPU till exakt rutans fysiska px
// (`Draw::glyphResampled`), för en 256-bild samplad ner till 16 på GPU:n
// blir grynig.
//
// PROGRAMMENYN följer MenuBars mönster: data i en `Menu`, visad med
// `popupAtRect` under ikonen, och den platta formens stigupplösning delad i
// src/menu_path.h — där den TOMMA stigen är roten.

#include "glaze/widgets/app_icon.h"

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../menu_path.h"
#include "../prop_parse.h"
#include "../window_icon.h"   // bildparet som gäller (effectiveIcons)

namespace glaze {

// Loggens avsändare i menystigens hjälpare (src/menu_path.h).
constexpr const char* kVem = "AppIcon";   // lang_gate:rad loggens avsändare, ingen användartext

GLAZE_REGISTER_WIDGET("appicon", AppIcon)

AppIcon::AppIcon()
{
    // Kedjan stängdes (klick utanför, Escape, en aktivering): släck plattan.
    menu_.onChainClosed = [this] {
        if (open_) {
            open_ = false;
            invalidate();
        }
    };
}

AppIcon::~AppIcon() = default;

// ---------------------------------------------------------------- bilden

// Vakten är bildvärdets likhet (image_source.h): samma Bitmap i en NY
// version är en ändring — det är så ändrade pixlar når OS-ikonen.
void AppIcon::setIcon(ImageSource img)
{
    if (icon_ == img)
        return;
    icon_ = std::move(img);
    invalidate();
    pushWindowIcon();
}

void AppIcon::setSmallIcon(ImageSource img)
{
    if (small_ == img)
        return;
    small_ = std::move(img);
    invalidate();
    pushWindowIcon();
}

void AppIcon::setNoIcon(bool on)
{
    if (noIcon_ == on)
        return;
    noIcon_ = on;
    placeChanged();   // ingen ikon = ingen plats
    pushWindowIcon();
}

void AppIcon::setLarge(bool on)
{
    if (large_ == on)
        return;
    large_ = on;
    placeChanged();   // OS-ikonen är densamma — bara platsen ändras
}

void AppIcon::placeChanged()
{
    invalidate();
    // Platsen är namnlistens motvikt och menyradens början — de flyttar,
    // och de är inte dirty (Widget::relayoutSiblings ritar om fönstret för
    // en chrome-rot).
    relayoutSiblings();
}

void AppIcon::pushWindowIcon()
{
    // Bara fönstrets EGEN ikon styr OS-ikonen, och bara när OS-fönstret
    // finns — `showOwned` lägger den när fönstret öppnas.
    AppWindow* w = appWindow();
    if (w && &w->appIcon == this && w->isOpen())
        w->applyWindowIcon();
}

// ---------------------------------------------------------------- platsen

std::pair<ImageSource, ImageSource> AppIcon::effectiveIcons() const
{
    // FÖRVALET är ramverkets inbäddade GlazeUI-ikon — regeln står i
    // src/window_icon.h, där OS-ikonen (`osIcon`) läser samma.
    return detail::effectiveIcons(icon_, small_);
}

bool AppIcon::showsLarge() const
{
    const AppWindow* w = appWindow();
    return large_ && !noIcon_ && w && &w->appIcon == this && w->topBar_ &&
           w->topBar_->isVisible() && w->menuBar.isVisible();
}

float AppIcon::reservedWidth(const Ctx& c, Row row) const
{
    if (!isVisible())
        return 0.0f;
    if (row == Row::Menu && !showsLarge())
        return 0.0f;
    return contentSize(c).w;
}

gfx::Sizef AppIcon::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // FRISTÅENDE (registret, previewn, en ikon utanför fönstrets
        // chrome): den lilla platsen, namnlistens kvadrat.
        const AppWindow* w = appWindow();
        if (!w || &w->appIcon != this)
            return gfx::Sizef{c.style->barHeight, c.style->barHeight};
        // Ingen ikon eller ingen namnlist (Native/None, transparent
        // fönster) ⇒ ingen plats.
        const TopBar* tb = w->topBar_;
        if (noIcon_ || !tb || !tb->isVisible())
            return gfx::Sizef{0.0f, 0.0f};
        // AXELREGELN som chromets höjd (`chromeInsetH`): satt höjd vinner,
        // annars stilens.
        float sida = tb->height > 0.0f ? tb->height : c.style->barHeight;
        if (showsLarge())
            sida += w->menuBar.height > 0.0f ? w->menuBar.height
                                             : c.style->menuRowH;
        return gfx::Sizef{sida, sida};
    }
    }
}

// ---------------------------------------------------------------- ritning

void AppIcon::draw(const Ctx& c)
{
    const gfx::Rectf b = c.bounds();
    // Ingen plats (popuper, fönster utan namnlist, noIcon) ⇒ inget att
    // rita — och ingen bild avkodas bara för att mäta den.
    if (noIcon_ || b.w <= 0.0f || b.h <= 0.0f)
        return;
    const auto& m   = *c.style;
    const auto& col = c.theme->colors;
    const bool  på  = isEnabled();
    const auto [stor, liten] = effectiveIcons();

    switch (c.ui) {
    case UI::Slate:
    default: {
        // PLATTAN lovar ett klick — så den syns bara när det finns en meny
        // att öppna: tryckt medan den är öppen, tänd under pekaren.
        if (på && !menu_.items.empty() && (open_ || hovered()))
            Draw::rect(c, Draw::inset(b, m.appIconPlateInset), m.appIconRadius,
                       open_ ? col.capsulePress : col.capsuleHover);
        const gfx::Rectf r = Draw::inset(b, m.appIconInset);
        Draw::glyphResampled(c, r, Draw::pickIcon(c, r, {stor, liten}),
                             på ? 1.0f : 1.0f - m.iconFade);
        break;
    }
    }
}

// ---------------------------------------------------------------- beteende

void AppIcon::mouseDown(const Ctx&, float, float)
{
    // TOGGLE i menyradens ordning: `App::dispatch` har redan köat
    // popup-stängningen när trycket når oss, så `open_` är fortfarande satt
    // och vägen blir stängning — inte en omedelbar återöppning.
    if (open_)
        closeMenu();
    else
        openMenu();
}

void AppIcon::openMenu()
{
    if (menu_.items.empty() || open_)
        return;   // ingen meny ⇒ ingenting
    if (!appWindow() || !appWindow()->isOpen())
        return;
    // Ankaret är HELA ikonens rect (fysiska px i fönstrets klientyta):
    // backenden lägger menyn under den. `open_` sätts FÖRE öppningen av
    // MenuBar::openMenus skäl (den gamla kedjans `onChainClosed` körs
    // synkront i `showOwned`) och tas tillbaka om menyn inte kom upp.
    const Ctx c = behaviorCtx();
    open_       = true;
    menu_.popupAtRect(*this, resolve(c));
    if (!menu_.isOpen())
        open_ = false;
    invalidate();
}

void AppIcon::closeMenu()
{
    if (!open_)
        return;
    menu_.close();   // uppskjutet
    open_ = false;
    invalidate();
}

bool AppIcon::tryShortcut(gfx::Key k, std::uint32_t mods)
{
    return menu_.tryShortcut(k, mods, this);
}

// ------------------------------------------------------ DEN PLATTA FORMEN
// MenuBars vokabulär (hård regel 8) med den TOMMA stigen som roten — hela
// stigen stiger ned från menyns egna poster.

void AppIcon::addItem(const MenuPath& path, TrText text,
                      const std::string& shortcut)
{
    if (detail::addMenuItem(detail::descendMenu(menu_.items, path, 0, kVem),
                            kVem, path, std::move(text), shortcut))
        invalidate();   // plattan kan börja lova ett klick
}

void AppIcon::addCheck(const MenuPath& path, TrText text, bool checked)
{
    if (detail::addMenuCheck(detail::descendMenu(menu_.items, path, 0, kVem),
                             kVem, path, std::move(text), checked))
        invalidate();
}

void AppIcon::addRadio(const MenuPath& path, TrText text, bool checked)
{
    if (detail::addMenuRadio(detail::descendMenu(menu_.items, path, 0, kVem),
                             kVem, path, std::move(text), checked))
        invalidate();
}

void AppIcon::addSeparator(const MenuPath& path)
{
    if (detail::addMenuSeparator(
            detail::descendMenu(menu_.items, path, 0, kVem), kVem,
            path))
        invalidate();
}

void AppIcon::addSubmenu(const MenuPath& path, TrText text)
{
    if (detail::addMenuSubmenu(
            detail::descendMenu(menu_.items, path, 0, kVem), kVem,
            path, std::move(text)))
        invalidate();
}

MenuItem* AppIcon::itemAtPath(const MenuPath& path)
{
    return detail::itemAtRootPath(menu_.items, path, kVem);
}

// MENYPOSTENS TILLSTÅND — den delade implementationen i src/menu_path.h,
// med den tomma stigen som roten (`itemAtPath`).
void AppIcon::setItemIcon(const MenuPath& path, ImageSource icon)
{
    detail::setMenuItemIcon(itemAtPath(path), kVem, path, std::move(icon));
}

void AppIcon::setItemEnabled(const MenuPath& path, bool on)
{
    detail::setMenuItemEnabled(itemAtPath(path), kVem, path, on);
}

void AppIcon::setItemChecked(const MenuPath& path, bool on)
{
    detail::setMenuItemChecked(itemAtPath(path), kVem, path, on);
}

void AppIcon::logMissingPath(const char* fn, const MenuPath& path)
{
    detail::logMissingMenuPath(kVem, fn, path);
}

bool AppIcon::setProp(const std::string& key, const std::string& value)
{
    if (key == "icon" || key == "smallIcon") {
        // "lista/nyckel" — samma par som glaze::img. Miss ⇒ ogiltigt
        // handtag (loggas av img), alltså förvalet respektive ingen liten.
        AssetHandle h;
        if (!propparse::parseImg(value, h))
            return false;
        if (key == "icon")
            setIcon(h);
        else
            setSmallIcon(h);
        return true;
    }
    if (key == "large") {
        setLarge(propparse::parseBool(value));
        return true;
    }
    if (key == "noIcon") {
        setNoIcon(propparse::parseBool(value));
        return true;
    }
    return Widget::setProp(key, value);
}

} // namespace glaze
