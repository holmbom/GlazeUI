#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "glaze/menu.h"

// INTERN header — ingår inte i publika API:t.
//
// DEN PLATTA MENYFORMENS STIGUPPLÖSNING (hård regel 8), delad av dem som
// bygger en meny med stigar: `MenuBar` (stigens FÖRSTA led är en rubrik,
// resten poster), `AppIcon` (programmenyn) och `TrayIcon` (systemfältets
// meny) — de två sista utan rubriknivå, där den TOMMA stigen är roten.
// Det finns EN matchningsregel för alla tre; en kopia per konsument skulle
// kunna glida isär.
//
// `who` är avsändarens namn i loggraderna ("MenuBar", "AppIcon",
// "TrayIcon"), så en okänd stig säger var den stod.
// De delar också menypostens TILLSTÅND (`setItemIcon`/`setItemEnabled`/
// `setItemChecked`) — längst ned.

namespace glaze::detail {

// EN matchning — id mot id, literal mot literal, och BLANDNING id/literal
// matchar ALDRIG (`menuTokenMatches`). LOGGNINGEN ÄR SKILD FRÅN
// MATCHNINGEN: uppslaget SCANNAR en lista, så de flesta kandidater matchar
// inte — och en separator bär en TOM LITERAL. En loggrad per kandidat gav
// därför en typfelsrad för varje separator i varje meny. Typfelet antecknas
// bara när det ser ut som ett MISSTAG (samma UPPLÖSTA text, olika sort) och
// loggas bara när hela scanningen missade.
struct SegSok {
    bool misstänktTypfel = false;
};

bool matchaSeg(const TrText& stigSeg, const TrText& menySeg, SegSok& sök);
void loggaTypfel(const char* who, const char* var, const TrText& stigSeg);

// Följer stigens led från och med `from` ned genom `items` och svarar med
// vektorn posterna ska in i — `items` själv när inget led återstår (roten).
// nullptr när ett led inte finns eller stigen är AVVISAD
// (`MenuPath::valid`); typfelet loggas här, den okända stigen av anroparen.
std::vector<MenuItem>* descendMenu(std::vector<MenuItem>& items,
                                   const MenuPath& path, std::size_t from,
                                   const char* who);

// Posten i `v` vars text matchar `seg`, eller nullptr (typfelet loggas).
MenuItem* findMenuItem(std::vector<MenuItem>& v, const TrText& seg,
                       const char* who);

// POSTEN PÅ EN STIG i en meny där den TOMMA stigen är roten (AppIcon,
// TrayIcon) — hela stigen stiger ned från `items`. nullptr för den tomma
// stigen (roten är menyn, ingen post) och för en okänd stig (typfelet
// loggas, den okända stigen av anroparen).
inline MenuItem* itemAtRootPath(std::vector<MenuItem>& items,
                                const MenuPath& path, const char* who)
{
    if (path.empty())
        return nullptr;
    std::vector<MenuItem>* v = descendMenu(items, path.parent(), 0, who);
    return v ? findMenuItem(*v, path[path.size() - 1], who) : nullptr;
}

// "okänd stig — ignorerad", med widgetens och metodens namn.
void logMissingMenuPath(const char* who, const char* fn, const MenuPath& path);

// ADD-KROPPARNA: lägger posten sist i `v` och svarar true — eller loggar
// den okända stigen och svarar false när `v` är nullptr (anroparens
// upplösning missade). Widgeten invaliderar vid true. Samma poster som
// `Menu::addItem` m.fl. bygger, så de två vägarna ger samma tillstånd.
bool addMenuItem(std::vector<MenuItem>* v, const char* who,
                 const MenuPath& path, TrText text, const std::string& shortcut);
bool addMenuCheck(std::vector<MenuItem>* v, const char* who,
                  const MenuPath& path, TrText text, bool checked);
bool addMenuRadio(std::vector<MenuItem>* v, const char* who,
                  const MenuPath& path, TrText text, bool checked);
bool addMenuSeparator(std::vector<MenuItem>* v, const char* who,
                      const MenuPath& path);
bool addMenuSubmenu(std::vector<MenuItem>* v, const char* who,
                    const MenuPath& path, TrText text);

// MENYPOSTENS TILLSTÅND — de tre
// platta mutatorerna `setItemIcon`/`setItemEnabled`/`setItemChecked`, EN
// implementation för MenuBar, AppIcon och TrayIcon: `it` är bärarens eget
// uppslag (`itemAtPath` — titelvägen för menyraden, `itemAtRootPath` för de
// två andra), nullptr loggar den okända stigen och svarar false. Fältet
// sätts rakt av som handkodens `itemAtPath(stig)->checked = på;` — ett kryss
// på en RADIOPOST avmarkerar inte dess syskon (det gör bara användarens val,
// `Menu::activatePath`), precis som i trädformen.
bool setMenuItemIcon(MenuItem* it, const char* who, const MenuPath& path,
                     ImageSource icon);
bool setMenuItemEnabled(MenuItem* it, const char* who, const MenuPath& path,
                        bool on);
bool setMenuItemChecked(MenuItem* it, const char* who, const MenuPath& path,
                        bool on);

} // namespace glaze::detail
