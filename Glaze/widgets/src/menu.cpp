// MENYN — datastrukturen, aktiveringen och VISNINGEN.
//
// Tre saker bor här, och gränsen mellan dem är hela poängen:
//
//   `Menu`               REN DATA som appen äger. Muteras fritt.
//   `MenuPresentation`   VISNINGEN: en SNAPSHOT + en kedja popup-fönster.
//                        Skapas av `popupAt`, ägs av modulen tills kedjan
//                        stängts, och når aldrig data via pekare — bara
//                        via INDEX-STIGAR.
//   `MenuWindow`/`MenuRows`  fönstret och raderna. `MenuRows` är en
//                        `glaze::ListBox`-SUBKLASS: radloopen, scrollen,
//                        hit-testen och markeringen är listans, och det
//                        enda menyn lägger till är radens INNEHÅLL
//                        (`drawRow`) och dess HÖJD (`rowH` — separatorer
//                        är tunna).
//
// KEDJEKONTRAKTET, skrivet här eftersom det är protokoll och
// inte stil:
//
//   ÖPPNA undermeny C till rad i popup P:
//       C.setPopupAnchor(radens rect i P:s klient-FYSISKA px) → C.show(*P)
//       showOwned → closePopupsNow(P) stänger ev. SYSKON nu, aldrig P
//       eller dess förfäder (urvalet behåller target och target:s ägarkedja)
//   HOVER på annan rad:  stäng C (uppskjutet); en ny undermeny i samma varv
//       tar den pending-stängda via closePopupsNow
//   COMMIT var som helst: fyr itemets onClick EN gång, stäng ROTEN →
//       kaskaden river barnen först (executeClose tar owned_.back() först)
//   ESCAPE:  stänger BARA översta nivån; på roten stänger den allt
//   KLICK UTANFÖR:  App::dispatch → closePopups stänger hela kedjan
//   ALDRIG: en nivå stänger sig själv SYNKRONT mitt i en hover-dispatch
//
// FOKUS — och `grabInput()` ÄR INTE FOKUS: det lägger widgeten i
// fönstrets lokala grab-stack, som får KeyDown FÖRE den fokuserade
// widgeten. Menyn stjäl alltså aldrig fokus: den tidigare fokuserade
// widgeten behåller sin fokusring, och en `TextInput`s caret och
// text-input-session ligger kvar. Vid stängning återställs ingenting,
// eftersom ingenting togs.
//
// GRABB I BÅDA ÄNDAR, och skälet är ett PLATTFORMSDELTA: på Wayland
// flyttar `xdg_popup_grab` tangentbordsfokus till POPUP-ytan, så
// `e.windowId` blir popupens; på Windows/macOS/X11 är popupen `noActivate`
// och tangenterna landar hos ÄGAREN. Därför griper `MenuRows` i sitt eget
// fönster OCH presentationen hänger en tangentkrok på ägarfönstret — båda
// ropar SAMMA `navigate()`. Dubbel dispatch kan inte uppstå: ett OS-event
// bär ETT `windowId`.

#include "glaze/menu.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "glaze/app.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/listbox.h"

#include "menu_path.h"
#include "menu_query.h"

namespace glaze {

// ============================================================== genvägen

namespace {

// Tangentens VISNINGSNAMN — se `keyName` längre ner. Den här är den
// KANONISKA formen: engelsk, STABIL, och den enda som får SPARAS.
// lang_gate:av kanoniska namn — serialiseringen; `keyName` visar dem översatta
const char* canonicalKeyNameImpl(gfx::Key k)
{
    using K = gfx::Key;
    switch (k) {
    case K::Return:       return "Enter";
    case K::Escape:       return "Esc";
    case K::Tab:          return "Tab";
    case K::Backspace:    return "Backspace";
    case K::Space:        return "Space";
    case K::Delete:       return "Del";
    case K::Left:         return "Left";
    case K::Right:        return "Right";
    case K::Up:           return "Up";
    case K::Down:         return "Down";
    case K::Home:         return "Home";
    case K::End:          return "End";
    case K::A: return "A"; case K::B: return "B"; case K::C: return "C";
    case K::D: return "D"; case K::E: return "E"; case K::F: return "F";
    case K::G: return "G"; case K::H: return "H"; case K::I: return "I";
    case K::J: return "J"; case K::K: return "K"; case K::L: return "L";
    case K::M: return "M"; case K::N: return "N"; case K::O: return "O";
    case K::P: return "P"; case K::Q: return "Q"; case K::R: return "R";
    case K::S: return "S"; case K::T: return "T"; case K::U: return "U";
    case K::V: return "V"; case K::W: return "W"; case K::X: return "X";
    case K::Y: return "Y"; case K::Z: return "Z";
    case K::N0: return "0"; case K::N1: return "1"; case K::N2: return "2";
    case K::N3: return "3"; case K::N4: return "4"; case K::N5: return "5";
    case K::N6: return "6"; case K::N7: return "7"; case K::N8: return "8";
    case K::N9: return "9";
    case K::F1: return "F1";   case K::F2: return "F2";
    case K::F3: return "F3";   case K::F4: return "F4";
    case K::F5: return "F5";   case K::F6: return "F6";
    case K::F7: return "F7";   case K::F8: return "F8";
    case K::F9: return "F9";   case K::F10: return "F10";
    case K::F11: return "F11"; case K::F12: return "F12";
    case K::Minus:        return "-";
    case K::Plus:         return "+";
    case K::Equals:       return "=";
    case K::Comma:        return ",";
    case K::Period:       return ".";
    case K::Slash:        return "/";
    case K::Backslash:    return "\\";
    case K::Semicolon:    return ";";
    case K::Apostrophe:   return "'";
    case K::LeftBracket:  return "[";
    case K::RightBracket: return "]";
    case K::Grave:        return "`";
    case K::Percent:      return "%";
    case K::Asterisk:     return "*";
    // NUMPADEN får ett EGET prefix: den är skild från sifferraden med
    // flit i fasaden, och en genväg bunden till `KP_N1` träffar inte `N1`.
    case K::KP_N0: return "Num 0"; case K::KP_N1: return "Num 1";
    case K::KP_N2: return "Num 2"; case K::KP_N3: return "Num 3";
    case K::KP_N4: return "Num 4"; case K::KP_N5: return "Num 5";
    case K::KP_N6: return "Num 6"; case K::KP_N7: return "Num 7";
    case K::KP_N8: return "Num 8"; case K::KP_N9: return "Num 9";
    case K::KP_Add:      return "Num +";
    case K::KP_Subtract: return "Num -";
    case K::KP_Multiply: return "Num *";
    case K::KP_Divide:   return "Num /";
    case K::KP_Decimal:  return "Num ,";
    // Navigationsklustret och numpadens navigationsläge.
    // Samma regel som siffrorna: numpaden får prefixet, för `KP_End` är
    // en annan post än `End` och en genväg bunden till den ena träffar
    // inte den andra.
    case K::PageUp:      return "PgUp";
    case K::PageDown:    return "PgDn";
    case K::Insert:      return "Ins";
    case K::KP_Home:     return "Num Home";
    case K::KP_End:      return "Num End";
    case K::KP_Left:     return "Num Left";
    case K::KP_Right:    return "Num Right";
    case K::KP_Up:       return "Num Up";
    case K::KP_Down:     return "Num Down";
    case K::KP_PageUp:   return "Num PgUp";
    case K::KP_PageDown: return "Num PgDn";
    case K::KP_Insert:   return "Num Ins";
    case K::KP_Delete:   return "Num Del";
    case K::KP_Begin:    return "Num Begin";
    case K::Unknown:
    default:
        // RESERVTEXT och inte tomhet: en felbunden genväg ska SYNAS i
        // menyn i stället för att tyst se ut som ingen genväg alls.
        return "?";
    }
}
// lang_gate:på

// Tangentens VISNINGSNAMN — ÖVERSATT. De tolv
// namngivna tangenterna och numpadens prefix hämtas ur språkbutiken;
// allt annat (bokstäver, siffror, F-tangenter, skiljetecken) ÄR sin egen
// symbol och behöver ingen översättning.
//
// KLYVNINGEN ÄR HELA POÄNGEN: den här funktionen får ALDRIG användas för
// att spara en genväg, och `canonicalKeyNameImpl` får aldrig visas för en
// användare — de två formerna får inte delas, för en översättning
// skulle då ändra det som sparas.
std::string keyName(gfx::Key k)
{
    using K = gfx::Key;
    auto t = [](std::uint32_t id) {
        const TrText x = tr(id);
        return x.str();
    };
    switch (k) {
    case K::Return:    return t(str::KeyEnter);
    case K::Escape:    return t(str::KeyEscape);
    case K::Tab:       return t(str::KeyTab);
    case K::Backspace: return t(str::KeyBackspace);
    case K::Space:     return t(str::KeySpace);
    case K::Delete:    return t(str::KeyDelete);
    case K::Left:      return t(str::KeyLeft);
    case K::Right:     return t(str::KeyRight);
    case K::Up:        return t(str::KeyUp);
    case K::Down:      return t(str::KeyDown);
    case K::Home:      return t(str::KeyHome);
    case K::End:       return t(str::KeyEnd);
    case K::PageUp:    return t(str::KeyPageUp);
    case K::PageDown:  return t(str::KeyPageDown);
    case K::Insert:    return t(str::KeyInsert);
    // Numpadens navigationsläge: prefixet + tvillingens
    // ÖVERSATTA namn ("Num Vänster"), och mitttangenten, som saknar
    // tvilling, sitt eget.
    case K::KP_Begin:  return t(str::KeyNumPrefix) + t(str::KeyBegin);
    default:
        if (gfx::navigationKey(k) != k)
            return t(str::KeyNumPrefix) + keyName(gfx::navigationKey(k));
        break;
    }
    // NUMPADEN: prefixet översätts, symbolen efter det gör det inte.
    // Prefixet bär sitt avslutande blanksteg i översättningen — en
    // separator är en del av språkets form, inte en konstant här.
    if (k >= K::KP_N0 && k <= K::KP_Decimal) {
        const char* kanon = canonicalKeyNameImpl(k);   // "Num 7"
        const std::string p(kanon);
        const std::size_t mel = p.find(' ');
        if (mel != std::string::npos)
            return t(str::KeyNumPrefix) + p.substr(mel + 1);
    }
    return canonicalKeyNameImpl(k);
}

} // namespace

const char* canonicalKeyName(gfx::Key k)
{
    return canonicalKeyNameImpl(k);
}

gfx::Key parseCanonicalKey(const std::string& name)
{
    // Spegeln av `canonicalKeyName`, byggd genom att FRÅGA den för varje
    // symbol i stället för att duplicera tabellen — de två kan därför
    // inte glida isär. `gfx::Key` har medvetet ingen `Count`-post (den
    // hade blivit ett giltigt värde i varje switch), så spannet slutar
    // vid sista posten — `gfx::kLastKey`, som flyttas med enumet.
    for (int i = 1; i <= int(gfx::kLastKey); ++i) {
        const gfx::Key k = gfx::Key(i);
        if (name == canonicalKeyNameImpl(k))
            return k;
    }
    return gfx::Key::Unknown;
}

// TEXT -> GENVÄG. Motsatsen till `shortcutText`s SERIALISERINGSform, och
// byggd som dess spegel via `parseCanonicalKey`.
//
// Formen är den som `.glazeui` bär: "Ctrl+N", "Ctrl+Shift+S", "Esc" —
// KANONISK och engelsk oavsett aktivt språk. Modifierarna får stå i
// valfri ordning; okänd text ger en OSATT genväg (och loggas), aldrig en
// gissning — en felskriven genväg ska synas som ingen genväg, inte som
// fel genväg.
Shortcut parseShortcut(const std::string& text)
{
    Shortcut ut;
    if (text.empty())
        return ut;

    std::string rest = text;
    for (;;) {
        const std::size_t p = rest.find('+');
        if (p == std::string::npos || p + 1 >= rest.size())
            break;
        const std::string mod = rest.substr(0, p);
        std::uint32_t     bit = 0;
        if (mod == "Ctrl")  bit = gfx::kModCtrl;
        else if (mod == "Alt")   bit = gfx::kModAlt;
        else if (mod == "Shift") bit = gfx::kModShift;
        else if (mod == "Super" || mod == "Cmd") bit = gfx::kModCmd;
        if (!bit)
            break;   // inte en modifierare — resten är tangentnamnet
        ut.mods |= bit;
        rest = rest.substr(p + 1);
    }

    // KANONISKT namn — aldrig det översatta. En fil sparad på finska ska
    // parsas identiskt på svenska.
    if (const gfx::Key k = parseCanonicalKey(rest); k != gfx::Key::Unknown) {
        ut.key = k;
        return ut;
    }
    gfx::log("parseShortcut: okänd tangent '%s' i \"%s\" — genvägen blir OSATT",
             rest.c_str(), text.c_str());
    ut.mods = 0;
    return ut;
}

std::string shortcutText(const Shortcut& s)
{
    if (!s.set())
        return std::string();
    // BÅDA GRENARNA går via det ÖVERSATTA `keyName` —
    // Mac-grenen med, för annars hade ⌘+Backsteg visats oöversatt medan
    // resten av menyn följde språket.
#ifdef __APPLE__
    // macOS visar SYMBOLER utan plustecken, i systemets egen ordning.
    // Symbolerna är UNIVERSELLA och översätts inte: ⌘ heter ⌘ på finska.
    std::string ut;
    if (s.mods & gfx::kModCtrl)  ut += "\xE2\x8C\x83";   // ⌃
    if (s.mods & gfx::kModAlt)   ut += "\xE2\x8C\xA5";   // ⌥
    if (s.mods & gfx::kModShift) ut += "\xE2\x87\xA7";   // ⇧
    if (s.mods & gfx::kModCmd)   ut += "\xE2\x8C\x98";   // ⌘
    ut += keyName(s.key);
    return ut;
#else
    // Ordningen är Ctrl · Alt · Shift · Super — skriven, så två menyer
    // aldrig visar samma genväg olika. Modifierarnas TEXT översätts och
    // bär sitt plustecken i översättningen: separatorn är en del av
    // språkets form, inte en konstant här.
    auto t = [](std::uint32_t id) {
        const TrText x = tr(id);
        return x.str();
    };
    std::string ut;
    if (s.mods & gfx::kModCtrl)  ut += t(str::ModCtrl);
    if (s.mods & gfx::kModAlt)   ut += t(str::ModAlt);
    if (s.mods & gfx::kModShift) ut += t(str::ModShift);
    if (s.mods & gfx::kModCmd)   ut += t(str::ModSuper);
    ut += keyName(s.key);
    return ut;
#endif
}

// ================================================================ STIGEN

// DEN PLATTA FORMENS STIGUPPLÖSNING (src/menu_path.h) — delad av
// menyraden och programmenyn.
namespace detail {

bool matchaSeg(const TrText& stigSeg, const TrText& menySeg, SegSok& sök)
{
    bool typfel = false;
    if (menuTokenMatches(stigSeg, menySeg, typfel))
        return true;
    if (typfel && stigSeg.str() == menySeg.str())
        sök.misstänktTypfel = true;
    return false;
}

void loggaTypfel(const char* who, const char* var, const TrText& stigSeg)
{
    gfx::log("%s::%s: TYPFEL i stigen — '%s' finns i menyn men som "
             "%s, medan stigen bär %s. Blanda inte id och literal i samma "
             "led; stigen matchar aldrig.",
             who, var, stigSeg.str().c_str(),
             stigSeg.id() ? "literal" : "ett id",
             stigSeg.id() ? "ett id" : "en literal");
}

std::vector<MenuItem>* descendMenu(std::vector<MenuItem>& items,
                                   const MenuPath& path, std::size_t from,
                                   const char* who)
{
    if (!path.valid())
        return nullptr;
    std::vector<MenuItem>* v = &items;
    for (std::size_t d = from; d < path.size(); ++d) {
        MenuItem* träff = nullptr;
        SegSok    sök;
        for (auto& it : *v)
            if (matchaSeg(path[d], it.text, sök))
                träff = &it;
        if (!träff) {
            if (sök.misstänktTypfel)
                loggaTypfel(who, "resolvePath", path[d]);
            return nullptr;
        }
        v = &träff->submenu;
    }
    return v;
}

MenuItem* findMenuItem(std::vector<MenuItem>& v, const TrText& seg,
                       const char* who)
{
    SegSok sök;
    for (auto& it : v)
        if (matchaSeg(seg, it.text, sök))
            return &it;
    if (sök.misstänktTypfel)
        loggaTypfel(who, "itemAtPath", seg);
    return nullptr;
}

void logMissingMenuPath(const char* who, const char* fn, const MenuPath& path)
{
    gfx::log("%s::%s: okänd stig '%s' — ignorerad", who, fn,
             path.debugText().c_str());
}

bool addMenuItem(std::vector<MenuItem>* v, const char* who,
                 const MenuPath& path, TrText text, const std::string& shortcut)
{
    if (!v) {
        logMissingMenuPath(who, "addItem", path);
        return false;
    }
    MenuItem it;
    it.text     = std::move(text);
    it.shortcut = parseShortcut(shortcut);
    v->push_back(std::move(it));
    return true;
}

bool addMenuCheck(std::vector<MenuItem>* v, const char* who,
                  const MenuPath& path, TrText text, bool checked)
{
    if (!v) {
        logMissingMenuPath(who, "addCheck", path);
        return false;
    }
    MenuItem it;
    it.text      = std::move(text);
    it.checkable = true;
    it.checked   = checked;
    v->push_back(std::move(it));
    return true;
}

bool addMenuRadio(std::vector<MenuItem>* v, const char* who,
                  const MenuPath& path, TrText text, bool checked)
{
    if (!v) {
        logMissingMenuPath(who, "addRadio", path);
        return false;
    }
    MenuItem it;
    it.text      = std::move(text);
    it.radio     = true;
    it.checkable = true;
    it.checked   = checked;
    v->push_back(std::move(it));
    return true;
}

bool addMenuSeparator(std::vector<MenuItem>* v, const char* who,
                      const MenuPath& path)
{
    if (!v) {
        logMissingMenuPath(who, "addSeparator", path);
        return false;
    }
    MenuItem it;
    it.separator = true;
    it.enabled   = false;
    v->push_back(std::move(it));
    return true;
}

bool addMenuSubmenu(std::vector<MenuItem>* v, const char* who,
                    const MenuPath& path, TrText text)
{
    if (!v) {
        logMissingMenuPath(who, "addSubmenu", path);
        return false;
    }
    MenuItem it;
    it.text = std::move(text);
    v->push_back(std::move(it));   // undermenyn fylls av senare add-satser
    return true;
}

// MENYPOSTENS TILLSTÅND — fältet sätts exakt som
// handkodens `itemAtPath(stig)->fält = värde`, så de två vägarna ger samma
// tillstånd (regel 8). En öppen meny visar sin snapshot tills den öppnas igen
// (menu.h, regel 2).
bool setMenuItemIcon(MenuItem* it, const char* who, const MenuPath& path,
                     ImageSource icon)
{
    if (!it) {
        logMissingMenuPath(who, "setItemIcon", path);
        return false;
    }
    it->icon = std::move(icon);
    return true;
}

bool setMenuItemEnabled(MenuItem* it, const char* who, const MenuPath& path,
                        bool on)
{
    if (!it) {
        logMissingMenuPath(who, "setItemEnabled", path);
        return false;
    }
    it->enabled = on;
    return true;
}

bool setMenuItemChecked(MenuItem* it, const char* who, const MenuPath& path,
                        bool on)
{
    if (!it) {
        logMissingMenuPath(who, "setItemChecked", path);
        return false;
    }
    it->checked = on;
    return true;
}

} // namespace detail

bool menuTokenMatches(const TrText& a, const TrText& b, bool& typeError)
{
    const bool aId = a.id() != 0;
    const bool bId = b.id() != 0;
    if (aId != bId) {
        typeError = true;
        return false;
    }
    typeError = false;
    return a == b;   // TrText::operator== är ren tokensemantik
}

MenuPath::MenuPath(const char* s) : segments_{TrText(s ? s : "")}
{
    validate();
}

MenuPath::MenuPath(std::string s) : segments_{TrText(std::move(s))}
{
    validate();
}

MenuPath::MenuPath(TrText t) : segments_{std::move(t)}
{
    validate();
}

MenuPath::MenuPath(std::initializer_list<TrText> segs)
    : segments_(segs.begin(), segs.end())   // KOPIERAS — listan äger inte
{                                           // sina element efter uttrycket
    validate();
}

MenuPath::MenuPath(std::vector<TrText> segs) : segments_(std::move(segs))
{
    validate();
}

// EN validator som ALLA fem segment-ctorer går genom (den förvalda bär
// inga segment att pröva). Listinitialisering
// prioriterar `initializer_list`, så `MenuPath{"a/b"}` går INTE genom
// `const char*`-ctorn — låg kontrollen bara där kunde klammerformen
// kringgå den.
//
// En TOM segmentlista är ROTEN och loggas inte här — det
// är konsumenten som vet om roten betyder något (`AppIcon`) eller är en
// okänd stig (`MenuBar`, som loggar själv). Verktygsvägen (`bld::applyCall`)
// bygger `appIcon.addItem({}, …)` som en tom vektor och ska inte få en
// loggrad per sats.
void MenuPath::validate()
{
    for (const TrText& s : segments_) {
        if (s.id() != 0)
            continue;   // ett id kan varken vara tomt eller bära '/'
        if (s.str().empty()) {
            gfx::log("MenuPath: TOMT segment — stigen förkastas");
            segments_.clear();
            rejected_ = true;
            return;
        }
        if (s.str().find('/') != std::string::npos) {
            gfx::log("MenuPath: '%s' innehåller '/' — stigar är "
                     "SEGMENTERADE, använd "
                     "{\"Utseende\", \"Läge\"}",
                     s.str().c_str());
            segments_.clear();
            rejected_ = true;
            return;
        }
    }
}

MenuPath MenuPath::parent() const
{
    if (rejected_)
        return *this;   // en avvisad stig har ingen förälder — och ingen rot
    if (segments_.size() <= 1)
        return MenuPath{};
    return MenuPath(std::vector<TrText>(segments_.begin(),
                                        segments_.end() - 1));
}

std::string MenuPath::debugText() const
{
    std::string ut;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        if (i)
            ut += '/';
        ut += segments_[i].id()
                  ? "⟦" + std::to_string(segments_[i].id()) + "⟧"
                  : segments_[i].str();
    }
    return rejected_ ? std::string("<ogiltig>")
           : ut.empty() ? std::string("<tom>")
                        : ut;
}

// =========================================================== SNAPSHOTTEN

namespace {

// EN rad som den RITAS. Allt som behövs för att teckna raden ligger här —
// presentationen läser aldrig `MenuItem` under ritning, och
// `index` är sista steget i INDEX-STIGEN tillbaka till posten.
struct SnapRow {
    std::string text;
    std::string shortcut;
    ImageSource icon{};   // ett HANDTAG: en Bitmap ritas med sina levande pixlar
    int  index     = 0;
    bool enabled   = true;
    bool separator = false;
    bool checkable = false;
    bool checked   = false;
    bool radio     = false;
    bool hasSub    = false;
};

std::vector<SnapRow> snapshotOf(const std::vector<MenuItem>& items)
{
    std::vector<SnapRow> ut;
    ut.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const MenuItem& it = items[i];
        SnapRow r;
        r.index     = static_cast<int>(i);
        r.text      = it.text.str();   // SNAPSHOTTEN löser upp texten
                                       // EN gång, vid öppningen
        r.shortcut  = shortcutText(it.shortcut);
        r.icon      = it.icon;
        r.enabled   = it.enabled;
        r.separator = it.separator;
        r.checkable = it.checkable;
        r.checked   = it.checked;
        r.radio     = it.radio;
        r.hasSub    = !it.submenu.empty();
        ut.push_back(std::move(r));
    }
    return ut;
}

} // namespace

// ============================================================== MenuRows

class MenuPresentation;

// Menyns rader — en `ListBox`-SUBKLASS. Basen bär radloopen (O(synliga)),
// scrollen, klippet, hit-testen och MARKERINGEN; menyn lägger till radens
// INNEHÅLL och dess HÖJD. Recepten är därmed ListBoxens, oförändrade:
// Slate fyller den armerade raden med `accent` och texten blir `windowBg`,
// Glaze använder det GRINDADE paret `accentSurface`/`onAccent`,
// Minimalistic ritar `rowMarkerW`-strecket och accent-text. INGA nya
// temaroller behövdes.
class MenuRows final : public ListBox {
public:
    MenuRows(MenuPresentation& pres, int level) : pres_(&pres), level_(level)
    {
        // Popupen ÄR fönstret: raderna ligger kant i kant, ramen ritas av
        // ListBox ovanpå. Remsorna är av — kapningen vid `menuMaxRows`
        // gör listan kort, och en synlig tumme i en meny vore nytt.
        setBorder(false);
        showScrollbars(false);
    }

    void setRows(std::vector<SnapRow> r)
    {
        rows_    = std::move(r);
        anySub_  = false;
        anySep_  = false;
        std::vector<std::string> etiketter;
        etiketter.reserve(rows_.size());
        for (const SnapRow& s : rows_) {
            etiketter.push_back(s.text);
            anySub_ = anySub_ || s.hasSub;
            anySep_ = anySep_ || s.separator;
        }
        // Basen äger RADANTALET (rowAt/rowTop/contentExtent räknar ur
        // `items()`), så etiketterna måste dit — även om `drawRow` ritar
        // texten själv.
        setItems(std::move(etiketter));
    }

    const std::vector<SnapRow>& rows() const { return rows_; }
    int  rowCount() const { return static_cast<int>(rows_.size()); }

    // ARMERINGEN är ListBoxens PROVISORISKA val (aldrig `selectFromUser`):
    // menyn har inget committat val, och den programmatiska vägen fyrar
    // per konstruktion inget `onChange` på fel widget.
    void arm(int row) { setProvisionalIndex(row); }
    int  armed() const { return provisionalIndex(); }

    void takeGrab() { grabInput(); }

    // Radens rect i FÖNSTRETS klient-FYSISKA px — undermenyns ankare.
    gfx::Rectf rowPhysRect(int row) const
    {
        if (!appWindow() || !appWindow()->isOpen())
            return gfx::Rectf{};
        const Ctx c = behaviorCtx();
        return Draw::project(c, resolve(c), rowRect(c, row));
    }

    // MÄTNINGEN bor här och inte i presentationen — det är samma geometri
    // som `drawRow` ritar, och två uträkningar hade kunnat glida isär
    // (regel 3). Svaret är LOGISKA px.
    float measureWidth(const Ctx& c) const
    {
        const auto& m       = *c.style;
        float       maxText = 0.0f;
        float       maxKort = 0.0f;
        for (const SnapRow& r : rows_) {
            if (r.separator)
                continue;
            maxText = std::max(maxText, Draw::measure(c, r.text).w);
            if (!r.shortcut.empty())
                maxKort = std::max(maxKort, Draw::measure(c, r.shortcut).w);
        }
        float w = textLeft(c) + maxText;
        if (maxKort > 0.0f)
            w += m.menuShortcutGap + maxKort;
        w += arrowCol(c) + m.menuPadX;
        return std::max(w, m.menuMinW);
    }

    // Höjden i LOGISKA px, KAPAD vid `menuMaxRows` rader. Kapningen sker
    // FÖRE `show()` med FLIT: Wayland-positioneraren sätter bara
    // `FLIP_Y | SLIDE_X` — varken `RESIZE_Y` eller `SLIDE_Y` — så en för
    // hög popup blir ALDRIG en mindre viewport, och utan kapning hade
    // scrollintervallet blivit noll och innehållet oåtkomligt.
    float measureHeight(const Ctx& c) const
    {
        const float tak = float(c.style->menuMaxRows) * c.style->menuRowH;
        return std::min(totalRowsH(c), tak);
    }

protected:
    // VARIABEL RADHÖJD — separatorn är tunn, och det är hela skälet till
    // att radgeometrin byggdes om i BASEN.
    float rowH(const Ctx& c, int row) const override
    {
        return (row >= 0 && std::size_t(row) < rows_.size() &&
                rows_[std::size_t(row)].separator)
                   ? c.style->menuSepH
                   : c.style->menuRowH;
    }

    // Utan separatorer är höjden konstant — då behåller basen sina
    // O(1)-vägar och menyn betalar ingenting för möjligheten.
    bool uniformRows() const override { return !anySep_; }

    // Popupen ÄR ett eget OS-fönster och har ingenting bakom sig att lysa
    // igenom — därför fyller även Minimalistic sin yta här.
    bool ownsSurface() const override { return true; }

    // Dock::Fill styr storleken — måttet används aldrig, och menyns höjd
    // räknas ur `measureHeight()` → `setClientSize()` en annan väg.
    //
    // `{0,0}` ÄR ENHETSNEUTRALT, och det är hela skälet att den här raden
    // får stå utan en dynamisk grind: `measure_gate` når bara REGISTRERADE
    // typer och den här klassen är TU-lokal. KONTRAKTET är därför att den
    // FÖRBLIR `{0,0}` — ger någon den ett verkligt mått måste den ändringen
    // ta med sig en egen grind. Radhöjdstesterna i `menu_gate` mäter
    // POPUPGEOMETRI och bevisar ingenting om den här funktionen.
    gfx::Sizef contentSize(const Ctx&) const override
    {
        return gfx::Sizef{0.0f, 0.0f};
    }

    void drawRow(const Ctx& c, int row, gfx::Color fg,
                 gfx::Color boxPen) const override;

    void mouseMove(const Ctx& c, float mx, float my) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    // INGET HÖGERTRYCKSVAL: listans högertryck
    // väljer genom användarvägen, och menyns armering är ett PROVISORISKT val
    // som aldrig får gå den vägen (`arm` ovan).
    bool rightDown(const Ctx&, float, float) override { return false; }
    bool mouseWheel(const Ctx& c, float x, float y, float wx, float wy) override
    {
        ListBox::mouseWheel(c, x, y, wx, wy);
        return true;   // popupen bubblar aldrig
    }
    bool keyDown(gfx::Key k, std::uint32_t mods) override;

    // Trycket landade UTANFÖR menyns delträd i DET HÄR fönstret. Kroken
    // är idempotent — `App::closePopups` har normalt redan köat samma
    // stängning, och en dubbel `close()` är en no-op.
    void mouseDownOutside(MouseDownOutsideArgs&) override;

private:
    float textLeft(const Ctx& c) const
    {
        // GLYFKOLUMNEN reserveras ALLTID, även i en meny utan bockar:
        // etiketterna ska ligga i linje mellan menyer, och en kolumn som
        // kommer och går flyttar texten när man kryssar i något.
        return c.style->menuPadX + c.style->checkSize + c.style->checkGap;
    }
    float arrowCol(const Ctx& c) const
    {
        return anySub_ ? c.style->menuShortcutGap + 2.0f * c.style->menuArrowW
                       : 0.0f;
    }

    MenuPresentation*    pres_;
    int                  level_;
    std::vector<SnapRow> rows_;
    bool                 anySub_ = false;
    bool                 anySep_ = false;
};

// =========================================================== MenuWindow

// Popup-fönstret. `Chrome::None` (ramlöst — ingen drag/resize) + popup-
// attributet (noActivate + toolWindow + stäng-vid-klick-utanför via
// `App::closePopups`). Prejudikatet är `Dropdown::ListWindow`.
class MenuWindow final : public AppWindow {
public:
    MenuWindow(MenuPresentation& pres, int level)
        : AppWindow("", 100, 40, gfx::Chrome::None)
    {
        setPopup(true);
        rows_       = add<MenuRows>(pres, level);
        rows_->dock = Dock::Fill;
    }

    MenuRows* rows() { return rows_; }

private:
    MenuRows* rows_ = nullptr;
};

// ====================================================== MenuPresentation

// EN öppen menykedja. Äger sina fönster och sin snapshot; når `Menu` bara
// via INDEX-STIGAR, aldrig via itempekare.
class MenuPresentation {
public:
    MenuPresentation(Menu& m, Widget& host)
        : menu_(&m), host_(&host), ownerWin_(host.appWindow())
    {
    }

    ~MenuPresentation()
    {
        // Sista utvägen — normalt är kroken redan borta via `clearHook()`.
        clearHook();
    }

    bool openRoot(const gfx::Rectf& anchor);

    // Öppna undermenyn på `path` (stigen till FÖRÄLDER-posten).
    void openSubmenuPath(const std::vector<int>& path);

    void closeDeeperThan(int level);
    void closeChain();

    bool navigate(gfx::Key k, std::uint32_t mods);
    void rowActivated(int level, int row);
    void rowHovered(int level, int row);
    void windowClosed(MenuWindow* w);

    // `Menu` förstörs medan vi visas: släpp datapekaren och stäng.
    void detachMenu();

    Menu*   menu() const { return menu_; }
    Widget* host() const { return host_; }

    // Är kedjan redan avslutad? `finish()` stänger popupen, släpper kroken,
    // kör appens `onClose` och tar bort sig ur `Menu::presentations_` — men
    // POSTAR sin egen destruktion, så objektet ligger kvar i modullistan ett
    // pumpvarv till. Utan den här frågan hade `menuPresentationHostedIn`
    // räknat en STÄNGD meny som levande, och borttagningen krävt TVÅ varv
    // där dropdownen klarar sig på ett.
    bool finished() const { return finished_; }

private:
    struct Level {
        std::vector<int>            path;   // stig till den VISADE vektorn
        std::unique_ptr<MenuWindow> win;
    };

    // ÄGARFÖNSTRET cachas vid konstruktionen och läses aldrig ur `host_`
    // igen: värden kan dö före oss (en `MenuBar` som rivs med sitt fönster),
    // och en dinglande läsning i städvägen är precis den sortens fel som
    // bara syns vid avslut.
    AppWindow* ownerWindow() const { return ownerWin_; }

    // TANGENTKROKEN får bara nollas av den som SATTE den. Utan vakten
    // kunde en gammal presentations städning slå ut en NY menys krok: en
    // item-handler får öppna en ny meny, och den gamlas `onClose` kommer
    // efter det.
    void clearHook()
    {
        if (!hookSet_)
            return;
        hookSet_ = false;
        if (ownerWin_)
            ownerWin_->setMenuKeyHook(nullptr);
    }

    // Kedjan är slut: släpp kroken, koppla loss menyn, fyr krokarna och
    // begär självdestruktion (POSTAD — vi står mitt i App:s stängningsrunda
    // och får inte radera oss själva här).
    void finish();

    // Nivån vars fönster är `w`, annars −1.
    int levelOf(const MenuWindow* w) const
    {
        for (std::size_t i = 0; i < levels_.size(); ++i)
            if (levels_[i].win.get() == w)
                return static_cast<int>(i);
        return -1;
    }

    bool openLevel(const std::vector<int>& path, AppWindow& parent,
                   const gfx::Rectf& anchor);

    // Nästa VALBARA rad (hoppar över separatorer och inaktiverade).
    static int stepRow(const MenuRows& r, int from, int dir);

    Menu*      menu_     = nullptr;
    Widget*    host_     = nullptr;
    AppWindow* ownerWin_ = nullptr;
    bool       hookSet_  = false;
    // Nivåerna, roten först. Fönstren ligger kvar i `graveyard_` när en
    // nivå stängts: stängningen är UPPSKJUTEN till pump-svansen, så
    // objektet måste överleva tills App faktiskt rivit OS-fönstret.
    std::vector<Level>                       levels_;
    std::vector<std::unique_ptr<MenuWindow>> graveyard_;
    bool                                     closing_  = false;
    bool                                     finished_ = false;
};

// --------------------------------------------------------- REGISTRET
//
// Presentationen ÄGS av modulen (App äger den tills kedjan
// stängts) och inte av `Menu` — `popupAt` returnerar direkt medan
// fönstren lever vidare. Självdestruktionen är POSTAD: en presentation får
// aldrig raderas inifrån sin egen `onClose`, som körs mitt i App:s
// stängningsrunda.

namespace {

std::vector<std::unique_ptr<MenuPresentation>>& presentations()
{
    static std::vector<std::unique_ptr<MenuPresentation>> lista;
    return lista;
}

void dropPresentation(MenuPresentation* p)
{
    auto& v = presentations();
    v.erase(std::remove_if(v.begin(), v.end(),
                           [p](const std::unique_ptr<MenuPresentation>& u) {
                               return u.get() == p;
                           }),
            v.end());
}

} // namespace

// ---------------------------------------------------------------- öppna

bool MenuPresentation::openLevel(const std::vector<int>& path,
                                 AppWindow& parent, const gfx::Rectf& anchor)
{
    if (!menu_ || !host_)
        return false;
    // SNAPSHOTTEN ÄR PER NIVÅ, inte per kedja — och det är ett SKRIVET val,
    // inte en lucka. En nivå läser `Menu` när den ÖPPNAS och aldrig mer, så
    // en rad kan inte ändras under fingret. Men en undermeny som ännu inte
    // öppnats läser AKTUELL data: muterar appen den innan användaren går dit
    // ska ändringen synas — allt annat vore att visa inaktuellt innehåll.
    // Säkerheten kommer inte av snapshotten utan av INDEX-STIGARNA nedan,
    // som bounds-kollas vid varje steg; en `push_back` kan därför aldrig ge
    // en dinglande referens.
    const std::vector<MenuItem>* v = &menu_->items;
    for (int i : path) {
        if (i < 0 || std::size_t(i) >= v->size())
            return false;   // stigen finns inte längre — menyn muterades
        v = &(*v)[std::size_t(i)].submenu;
    }
    if (v->empty())
        return false;   // TOM MENY öppnar ingenting (Dropdown-vakten)

    auto win = std::make_unique<MenuWindow>(*this, int(levels_.size()));
    MenuWindow* raw = win.get();
    raw->rows()->setRows(snapshotOf(*v));
    raw->onClose = [this, raw](Widget*, CloseArgs&) { windowClosed(raw); };

    // MÅTTEN räknas ur VÄRDENS Ctx (font, stil och skala är app-globala),
    // och de räknas FÖRE `show()` — en xdg-popup får sin storlek vid
    // mappningen och kan inte växa efteråt.
    const Ctx c = host_->behaviorCtx();
    raw->setPopupAnchor(anchor);
    raw->setClientSize(int(std::lround(raw->rows()->measureWidth(c))),
                       int(std::lround(raw->rows()->measureHeight(c))));

    levels_.push_back(Level{path, std::move(win)});
    raw->show(parent);

    // FAIL-SAFE per NIVÅ (`Dropdown::openAt`-mönstret): misslyckas
    // backendens fönsterinit får ingen nivå bli hängande utan fönster —
    // det är just den init-felvägen som kraschar Mutter
    if (!raw->isOpen()) {
        gfx::log("Menu: popup-fönstret kom aldrig upp — stänger av nivån");
        graveyard_.push_back(std::move(levels_.back().win));
        levels_.pop_back();
        return false;
    }

    // GRABBEN i popupfönstret (Wayland-sidan). Den är INTE fokus —
    // den lägger bara raderna i det HÄR fönstrets grab-stack.
    raw->rows()->takeGrab();
    return true;
}

bool MenuPresentation::openRoot(const gfx::Rectf& anchor)
{
    AppWindow* owner = ownerWindow();
    if (!owner || !owner->isOpen())
        return false;
    if (!openLevel({}, *owner, anchor))
        return false;

    // ÄGARSIDAN av grabben: på Windows/macOS/X11 är popupen
    // `noActivate`, så tangenterna landar i ÄGARFÖNSTRET. Kroken går FÖRE
    // både grab-stacken och den fokuserade widgeten — en öppen meny är
    // modal för tangentbordet så länge den är uppe.
    owner->setMenuKeyHook([this](gfx::Key k, std::uint32_t m) {
        return navigate(k, m);
    });
    hookSet_ = true;
    return true;
}

void MenuPresentation::openSubmenuPath(const std::vector<int>& path)
{
    if (path.empty())
        return;
    const std::vector<int> parent(path.begin(), path.end() - 1);
    // Nivån som VISAR föräldervektorn.
    int lvl = -1;
    for (std::size_t i = 0; i < levels_.size(); ++i)
        if (levels_[i].path == parent)
            lvl = static_cast<int>(i);
    if (lvl < 0 || !levels_[std::size_t(lvl)].win->isOpen())
        return;

    // Är den redan öppen? Då är det en no-op — annars hade varje
    // musrörelse över raden rivit och byggt om undermenyn.
    if (std::size_t(lvl) + 1 < levels_.size() &&
        levels_[std::size_t(lvl) + 1].path == path) {
        closeDeeperThan(lvl + 1);
        return;
    }
    closeDeeperThan(lvl);

    MenuRows* r = levels_[std::size_t(lvl)].win->rows();
    int       radInNivå = -1;
    for (int i = 0; i < r->rowCount(); ++i)
        if (r->rows()[std::size_t(i)].index == path.back())
            radInNivå = i;
    if (radInNivå < 0)
        return;

    // DEGENERERAT ANKARRECT ger höger-om-raden utan fasadtillägg.
    // Positioner-vägen är hårdkodad "under ankarrectet" med ankare
    // BOTTOM_LEFT / gravity BOTTOM_RIGHT, så en 1×1-rect vid radens
    // högerkant lägger undermenyn ~1 fysisk px under radens topp.
    // PRISET, skrivet: vid skärmkant SLIDE:ar undermenyn över föräldern i
    // stället för att flippa — `WindowDesc::popupGravity` vore ett
    // fasadtillägg med ett tillägg i varje backend och är uppskjutet.
    const gfx::Rectf rr = r->rowPhysRect(radInNivå);
    openLevel(path, *levels_[std::size_t(lvl)].win,
              gfx::Rectf{rr.x + rr.w - 1.0f, rr.y, 1.0f, 1.0f});
}

// --------------------------------------------------------------- stänga

void MenuPresentation::closeDeeperThan(int level)
{
    while (int(levels_.size()) > level + 1) {
        Level& l = levels_.back();
        if (l.win->isOpen())
            l.win->close();   // UPPSKJUTET — aldrig synkront i en dispatch
        graveyard_.push_back(std::move(l.win));
        levels_.pop_back();
    }
}

void MenuPresentation::closeChain()
{
    if (closing_)
        return;
    closing_ = true;
    clearHook();
    // ROTEN stängs; kaskaden river barnen FÖRST (`executeClose` tar
    // `owned_.back()` rekursivt), vilket är xdg-korrekt av sig självt.
    if (!levels_.empty() && levels_.front().win->isOpen())
        levels_.front().win->close();
    else
        finish();   // inget att stänga — städa direkt
}

// STÄNGNINGSBESKEDET, och den här funktionen har en livstidsfälla:
// en nivå som redan flyttats till `graveyard_` får sitt
// `onClose` FÖRST i nästa pumpvarv, och `levelOf` svarar då −1. Utan den
// tredje grenen nedan tolkas det som "roten stängdes" — och då hade en
// enkel hover BORT från en undermeny rivit hela menykedjan.
void MenuPresentation::windowClosed(MenuWindow* w)
{
    const int lvl = levelOf(w);
    if (lvl < 0)
        return;   // redan avfärdad nivå (graveyard) — beskedet är gammalt
    if (lvl > 0) {
        // En undermeny stängdes (t.ex. av kaskaden) — riv bara nivåerna
        // under den.
        closeDeeperThan(lvl - 1);
        return;
    }
    finish();   // lvl == 0: roten är stängd ⇒ kedjan är slut
}

void MenuPresentation::finish()
{
    if (finished_)
        return;
    finished_ = true;
    closing_  = true;
    clearHook();
    if (menu_) {
        Menu* m = menu_;
        m->presentations_.erase(std::remove(m->presentations_.begin(),
                                            m->presentations_.end(), this),
                                m->presentations_.end());
        menu_ = nullptr;
        // BÅDA krokarna KOPIERAS innan någon av dem körs. Den interna får
        // stänga menyraden — och en menyrad som töms förstör sina `Menu`,
        // så en läsning av `m->onClose` EFTER `onChainClosed()` vore
        // use-after-free. Kopiorna äger sina egna målobjekt och överlever
        // att `m` dör under oss.
        auto internKrok = m->onChainClosed;
        auto appKrok    = m->onClose;
        // INTERNA kroken först (menyradens bokföring), sedan APPENS —
        // appens handler får stänga fönster och öppna nya menyer, och då
        // ska raden redan ha släppt sin rubrik.
        if (internKrok)
            internKrok();
        if (appKrok) {
            EventArgs e;
            appKrok(host_, e);
        }
    }
    // POSTAD självdestruktion: vi står mitt i App:s stängningsrunda.
    MenuPresentation* self = this;
    glaze::app().post([self] { dropPresentation(self); });
}

void MenuPresentation::detachMenu()
{
    menu_ = nullptr;
    closeChain();
}

// ---------------------------------------------------------- navigeringen

int MenuPresentation::stepRow(const MenuRows& r, int from, int dir)
{
    const int n = r.rowCount();
    if (n == 0)
        return -1;
    int i = from;
    for (int steg = 0; steg < n; ++steg) {
        i = (i + dir + n) % n;
        if (i < 0)
            i += n;
        const SnapRow& s = r.rows()[std::size_t(i)];
        if (!s.separator && s.enabled)
            return i;
    }
    return -1;   // inget valbart alls
}

// EN commitpunkt, `handled` som svar, och idempotent mot samma logiska
// tangentsekvens — kravet på "grabb i båda ändar".
bool MenuPresentation::navigate(gfx::Key k, std::uint32_t mods)
{
    if (levels_.empty())
        return false;
    // Modifierade tangenter lämnas till acceleratorvägen och till appen.
    if (mods & (gfx::kModCtrl | gfx::kModAlt | gfx::kModCmd))
        return false;

    const int L  = int(levels_.size()) - 1;
    MenuRows* r  = levels_[std::size_t(L)].win->rows();
    const int nu = r->armed();

    switch (k) {
    case gfx::Key::Down:
    case gfx::Key::Up: {
        const int ny = stepRow(*r, nu < 0 ? (k == gfx::Key::Down ? -1 : 0) : nu,
                               k == gfx::Key::Down ? +1 : -1);
        if (ny >= 0) {
            r->arm(ny);
            r->ensureVisible(ny);
        }
        return true;
    }
    case gfx::Key::Home: {
        const int ny = stepRow(*r, -1, +1);
        if (ny >= 0) { r->arm(ny); r->ensureVisible(ny); }
        return true;
    }
    case gfx::Key::End: {
        const int ny = stepRow(*r, 0, -1);
        if (ny >= 0) { r->arm(ny); r->ensureVisible(ny); }
        return true;
    }
    case gfx::Key::Right:
        if (nu >= 0 && r->rows()[std::size_t(nu)].hasSub) {
            rowActivated(L, nu);   // ÖPPNAR undermenyn — samma väg som klick
            // Armera första valbara raden i den nya nivån.
            if (int(levels_.size()) > L + 1) {
                MenuRows* sub = levels_[std::size_t(L) + 1].win->rows();
                const int f   = stepRow(*sub, -1, +1);
                if (f >= 0)
                    sub->arm(f);
            }
            return true;
        }
        if (L == 0 && menu_ && menu_->stepSibling)
            menu_->stepSibling(+1);
        return true;   // menyn är modal för tangentbordet medan den är uppe
    case gfx::Key::Left:
        if (L > 0) {
            closeDeeperThan(L - 1);
            return true;
        }
        if (menu_ && menu_->stepSibling)
            menu_->stepSibling(-1);
        return true;
    case gfx::Key::Return:
    case gfx::Key::Space:
        if (nu >= 0)
            rowActivated(L, nu);
        return true;
    case gfx::Key::Escape:
        // ESCAPE stänger BARA översta nivån; på roten stänger den allt.
        if (L > 0)
            closeDeeperThan(L - 1);
        else
            closeChain();
        return true;
    default:
        return false;
    }
}

// ------------------------------------------------------------ aktivering

void MenuPresentation::rowActivated(int level, int row)
{
    if (!menu_ || level < 0 || std::size_t(level) >= levels_.size())
        return;
    MenuRows* r = levels_[std::size_t(level)].win->rows();
    if (row < 0 || row >= r->rowCount())
        return;
    std::vector<int> stig = levels_[std::size_t(level)].path;
    stig.push_back(r->rows()[std::size_t(row)].index);
    // ALLT går genom `activatePath` — klick, Enter och accelerator.
    // EFTER anropet får ingenting här röra vare sig itemet eller
    // kedjan: handlern kan ha stängt fönstret, muterat menyn eller öppnat
    // en ny meny.
    menu_->activatePath(stig, host_, this);
}

void MenuPresentation::rowHovered(int level, int row)
{
    if (level < 0 || std::size_t(level) >= levels_.size())
        return;
    MenuRows* r = levels_[std::size_t(level)].win->rows();
    if (row < 0 || row >= r->rowCount()) {
        r->arm(-1);
        closeDeeperThan(level);
        return;
    }
    const SnapRow& s = r->rows()[std::size_t(row)];
    if (s.separator || !s.enabled) {
        // En separator eller en inaktiv rad ARMERAS inte — annars hade
        // markeringen fyllt ett streck eller lovat något som inte händer.
        r->arm(-1);
        closeDeeperThan(level);
        return;
    }
    r->arm(row);
    if (s.hasSub) {
        std::vector<int> stig = levels_[std::size_t(level)].path;
        stig.push_back(s.index);
        openSubmenuPath(stig);   // ingen fördröjningstimer i v1
    } else {
        closeDeeperThan(level);
    }
}

// ================================================= MenuRows-implementation

void MenuRows::drawRow(const Ctx& c, int row, gfx::Color fg,
                       gfx::Color boxPen) const
{
    if (row < 0 || std::size_t(row) >= rows_.size())
        return;
    const SnapRow&   s   = rows_[std::size_t(row)];
    const gfx::Rectf r   = rowRect(c, row);
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    (void)boxPen;

    // SEPARATORN är ett streck och inte en rad: ingen text, ingen glyf,
    // ingen markering (den armeras aldrig — se `rowHovered`/`stepRow`).
    if (s.separator) {
        const float cy = r.y + r.h / 2.0f;
        Draw::line(c, r.x + m.menuPadX, cy, r.x + r.w - m.menuPadX, cy,
                   Pen{col.border, 0.0f});
        return;
    }

    // INAKTIV rad dimmas här och inte i basen: basens `fg` följer
    // WIDGETENS tillstånd, medan `enabled` i en meny är per RAD.
    const bool       armerad = row == selectedIndex();
    const gfx::Color text    = s.enabled ? fg : col.textDim;

    // GLYFKOLUMNEN: bock, radioprick eller bild — alltid i radens egen
    // textfärg, så kontrasten är gratis på den armerade raden.
    const float gy = r.y + (r.h - m.checkSize) / 2.0f;
    const gfx::Rectf glyf{r.x + m.menuPadX, gy, m.checkSize, m.checkSize};
    if (s.checked && s.radio)
        Draw::circle(c, Draw::inset(glyf, m.checkSize / 4.0f), text);
    else if (s.checked)
        Draw::check(c, glyf, Pen{text, m.checkStroke});
    else if (s.icon.valid())
        Draw::glyph(c, glyf, s.icon);

    const float tx  = textLeft(c);
    const float ac  = arrowCol(c);
    const float kol = s.shortcut.empty() ? 0.0f
                                         : Draw::measure(c, s.shortcut).w;
    const float slut = r.x + r.w - m.menuPadX - ac;
    Draw::text(c,
               {r.x + tx, r.y,
                std::max(0.0f, slut - (r.x + tx) -
                                   (kol > 0.0f ? kol + m.menuShortcutGap : 0.0f)),
                r.h},
               s.text, text);

    if (!s.shortcut.empty())
        Draw::text(c, {slut - kol, r.y, kol, r.h}, s.shortcut,
                   armerad ? text : col.textDim, Align::Right);

    if (s.hasSub) {
        const gfx::Rectf pil{r.x + r.w - m.menuPadX - 2.0f * m.menuArrowW,
                             r.y + r.h / 2.0f - m.menuArrowW,
                             2.0f * m.menuArrowW, 2.0f * m.menuArrowW};
        Draw::chevron(c, pil, Dir::Right, Pen{text, 0.0f});
    }
}

void MenuRows::mouseMove(const Ctx& c, float mx, float my)
{
    if (handleStripFirst(c, mx, my, false))
        return;
    pres_->rowHovered(level_, rowAtY(c, my));
}

void MenuRows::mouseDown(const Ctx& c, float mx, float my)
{
    if (handleStripFirst(c, mx, my, true))
        return;
    const int row = rowAtY(c, my);
    if (row < 0)
        return;
    // MARKÖREN FÖRST, sedan aktivering — samma invariant som dropdownens
    // popup: det armerade ÄR det som ritas starkt, och ett klick utan
    // föregående `mouseMove` (touch, syntetiska events) får inte aktivera
    // en annan rad än den synliga.
    const SnapRow& s = rows_[std::size_t(row)];
    if (s.separator || !s.enabled)
        return;
    arm(row);
    pres_->rowActivated(level_, row);
}

bool MenuRows::keyDown(gfx::Key k, std::uint32_t mods)
{
    // TANGENTERNA går ALDRIG till ListBoxens egen tabell: den hade gått
    // via `selectFromUser` och fyrat `onChange` på fel widget.
    return pres_->navigate(k, mods);
}

void MenuRows::mouseDownOutside(MouseDownOutsideArgs&)
{
    pres_->closeChain();
}

// ================================================================== Menu

Menu::~Menu()
{
    // En `Menu` som förstörs medan den visas stänger sina presentationer
    //. Kopian är nödvändig: `detachMenu` skriver i listan.
    std::vector<MenuPresentation*> kopia = presentations_;
    presentations_.clear();
    for (MenuPresentation* p : kopia)
        p->detachMenu();
}

MenuItem& Menu::addItem(TrText text)
{
    MenuItem it;
    it.text = std::move(text);
    items.push_back(std::move(it));
    return items.back();
}

MenuItem& Menu::addCheck(TrText text, bool checked)
{
    MenuItem& it  = addItem(std::move(text));
    it.checkable  = true;
    it.checked    = checked;
    return it;
}

MenuItem& Menu::addRadio(TrText text, bool checked)
{
    MenuItem& it = addItem(std::move(text));
    it.radio     = true;
    it.checkable = true;
    it.checked   = checked;
    return it;
}

MenuItem& Menu::addSeparator()
{
    MenuItem it;
    it.separator = true;
    it.enabled   = false;   // aldrig valbar, aldrig armerad
    items.push_back(std::move(it));
    return items.back();
}

MenuItem& Menu::addSubmenu(TrText text)
{
    return addItem(std::move(text));   // fyll `.submenu` på returvärdet
}

MenuItem* Menu::itemAt(const std::vector<int>& path)
{
    std::vector<MenuItem>* v = &items;
    MenuItem*              it = nullptr;
    for (int i : path) {
        if (i < 0 || std::size_t(i) >= v->size())
            return nullptr;   // stigen finns inte längre — menyn muterades
        it = &(*v)[std::size_t(i)];
        v  = &it->submenu;
    }
    return it;
}

namespace {

// RADIOGRUPPEN: de SAMMANHÄNGANDE radio-posterna runt `idx` — avgränsade
// av separatorer eller icke-radiorader. Definitionen matchar wxWidgets
// `AppendRadioItem` och behöver inget grupp-id.
void applyRadio(std::vector<MenuItem>& v, int idx)
{
    int a = idx, b = idx;
    while (a - 1 >= 0 && v[std::size_t(a - 1)].radio) --a;
    while (b + 1 < int(v.size()) && v[std::size_t(b + 1)].radio) ++b;
    for (int i = a; i <= b; ++i)
        v[std::size_t(i)].checked = (i == idx);
}

} // namespace

// AKTIVERINGEN ÄR EN VÄG — klick, Enter och accelerator går alla
// hit. Ordningen är BINDANDE:
//
//   separator / inaktiv   no-op
//   undermeny             öppna — INTE onClick
//   checkable             toggla EXAKT en gång  -> onClick en gång
//   radio                 markera denna, avmarkera gruppsyskon -> onClick
//   vanlig                onClick EXAKT en gång
//
// TILLSTÅNDET ändras FÖRE handlern, STÄNGNINGEN köas FÖRE handlern (annars
// stänger den gamla kedjans svans en meny handlern just öppnat), och
// handlern körs genom en KOPIA av handtaget, som delar det anropsbara med
// posten — den får mutera menyn under sig.
bool Menu::activatePath(const std::vector<int>& path, Widget* sender,
                        MenuPresentation* pres)
{
    MenuItem* it = itemAt(path);
    if (!it || it->separator || !it->enabled)
        return false;

    if (!it->submenu.empty()) {
        if (pres)
            pres->openSubmenuPath(path);
        return true;   // ÖPPNAR — fyrar aldrig onClick
    }

    if (it->radio) {
        // Föräldervektorn = stigen utan sista steget.
        std::vector<MenuItem>* v = &items;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
            v = &(*v)[std::size_t(path[i])].submenu;
        applyRadio(*v, path.back());
    } else if (it->checkable) {
        it->checked = !it->checked;
    }
    if (sender)
        sender->invalidate();

    if (pres)
        pres->closeChain();

    // KOPIA av handtaget — itemet kan dö i handlern. Kopian DELAR det
    // anropsbara (widget.h, `Handler`): en muterbar lambdas tillstånd är
    // postens, och anropet håller det vid liv själv.
    Handler<ClickArgs> fn = it->onClick;
    if (fn) {
        ClickArgs e;
        fn(sender, e);
    }
    return true;
}

namespace {

bool shortcutIn(std::vector<MenuItem>& items, std::vector<int>& path,
                gfx::Key k, std::uint32_t mods, std::vector<int>& ut)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        MenuItem& it = items[i];
        if (it.separator || !it.enabled)
            continue;
        path.push_back(static_cast<int>(i));
        if (!it.submenu.empty()) {
            if (shortcutIn(it.submenu, path, k, mods, ut))
                return true;
        } else if (it.shortcut.matches(k, mods)) {
            ut = path;
            return true;
        }
        path.pop_back();
    }
    return false;
}

} // namespace

bool Menu::tryShortcut(gfx::Key k, std::uint32_t mods, Widget* sender)
{
    std::vector<int> stig, träff;
    if (!shortcutIn(items, stig, k, mods, träff))
        return false;
    return activatePath(träff, sender, nullptr);
}

void Menu::popupAt(Widget& host, float x, float y)
{
    // DEGENERERAT ANKARRECT vid pekaren: backenden placerar popupen under
    // rectet, alltså precis under-höger om klickpunkten.
    popupAtRect(host, gfx::Rectf{x, y, 1.0f, 1.0f});
}

void Menu::popupAtRect(Widget& host, const gfx::Rectf& anchor)
{
    if (items.empty())
        return;   // TOM MENY öppnar ingenting (Dropdowns `items_`-vakt)
    AppWindow* w = host.appWindow();
    if (!w || !w->isOpen()) {
        gfx::log("Menu::popupAt: värden sitter inte i ett öppet fönster");
        return;
    }
    // VARJE popupAt skapar en EGEN presentation: `showOwned`
    // höjer bara ett redan öppet fönster utan att byta ägare eller ankare,
    // så återanvändning hade varit odefinierad.
    auto pres = std::make_unique<MenuPresentation>(*this, host);
    MenuPresentation* raw = pres.get();
    presentations().push_back(std::move(pres));
    presentations_.push_back(raw);

    if (!raw->openRoot(anchor)) {
        presentations_.erase(std::remove(presentations_.begin(),
                                         presentations_.end(), raw),
                             presentations_.end());
        dropPresentation(raw);
    }
}

void Menu::close()
{
    std::vector<MenuPresentation*> kopia = presentations_;
    for (MenuPresentation* p : kopia)
        p->closeChain();
}

// ------------------------------------------------- FRÅGAN UTIFRÅN
//
// Kontraktet står i `menu_query.h`. Implementationen bor HÄR eftersom
// `presentations()` är modulens egen statiska lista — ingen andra sanning
// om vilka menyer som lever.
//
// Vandringen går UPPÅT från varje värd i stället för nedåt från
// delträdsroten: en presentation vet sin värd, men delträdet vet ingenting
// om presentationer, och `parent()`-kedjan slutar ändå vid fönsterroten.
bool menuPresentationHostedIn(const Widget& subtreeRoot)
{
    for (const std::unique_ptr<MenuPresentation>& p : presentations()) {
        // AVSLUTADE hoppas över: efter `finish()` är popupkedjan stängd,
        // kroken släppt, callbacks körda och `Menu::isOpen()` false — den
        // uppskjutna destruktionen är LIVSTIDSHANTERING och ska inte hålla
        // värden upptagen. Utan detta nekades en borttagning i samma
        // pumpvarv som menyns `onClose` redan körts i, alltså ett varv
        // längre än dropdownens deny-fönster.
        if (!p || p->finished())
            continue;
        for (const Widget* w = p->host(); w; w = w->parent())
            if (w == &subtreeRoot)
                return true;
    }
    return false;
}

} // namespace glaze
