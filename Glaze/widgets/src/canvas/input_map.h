#pragma once
// INTERN — RITYTANS BINDNINGSTABELL: raderna
// (läge, källa, modifierare) → (handling, riktning, fart), vändningarna per
// källa och per (enhet, handling), och UPPSLAGET med arvet från appen. Ren std
// utan widget, kamera eller fönster — `Canvas` frågar, och `binding_gate`
// grindar den headless. Tabellen över vad en handling GÖR med kameran och hur
// mycket står i camera_math (`bindingAmount`).
//
// UPPSLAGET, i fyra regler:
//   1. MODIFIERARNA normaliseras (Cmd blir Ctrl, allt utom Ctrl/Shift/Alt
//      släpps). En rad MATCHAR när dess modifierare är en DELMÄNGD av de
//      tryckta, och den MEST SPECIFIKA vinner — flest bitar, vid lika Alt före
//      Shift före Ctrl.
//   2. ARVET är per rad: ritytans effektiva tabell är appens rader och
//      ritytans, där ritytans rad ersätter appens rad med SAMMA nyckel. Sedan
//      gäller specificiteten över båda.
//   3. ETT EGET LÄGE ("mäta") som saknar en träff för gruppen — dragets två
//      axlar, hjulets, pilarnas — faller tillbaka på det automatiska ("map"
//      eller "orbit"). Ramverkets tre lägen faller aldrig tillbaka.
//   4. `None` betyder OBUNDEN: raden skuggar de mindre specifika för sin
//      källa, men gesten gör ingenting och tas inte.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "glaze/input.h"

namespace glaze::detail {

inline constexpr int kInputSources =
    static_cast<int>(InputSource::PlusMinus) + 1;
inline constexpr int kInputActions = static_cast<int>(InputAction::Lift) + 1;
inline constexpr int kInputDevices =
    static_cast<int>(InputDevice::Keyboard) + 1;

// Ramverkets lägesnamn (strängar, en form).
inline constexpr const char* kModeMap         = "map";
inline constexpr const char* kModeOrbit       = "orbit";
inline constexpr const char* kModeFirstPerson = "firstPerson";

// Ctrl/Shift/Alt med Cmd som Ctrl — radernas och de tryckta modifierarnas
// gemensamma form.
std::uint32_t normalizeMods(std::uint32_t mods);
// Specificiteten: högre = mer specifik (bitarna först, sedan Alt > Shift >
// Ctrl). Två olika normaliserade masker har aldrig samma värde.
int modsRank(std::uint32_t normalizedMods);
// Är raden giltig? Annars ett skäl för loggen (tomt läge, fart ≤ 0 eller
// inte ändlig, en uppräkning utanför sina värden).
bool validBinding(const InputBinding& b, const char*& why);

// EN TABELL — appens (`App::setCanvasBinding`) eller en ritytas egen.
class InputMap {
public:
    // RAMVERKETS FÖRVAL: förvalets gester, skrivna som rader.
    static InputMap defaults();

    // Raden med nyckeln (läge, källa, normaliserade modifierare) ersätts
    // helt; modifierarna normaliseras. Ogiltig ⇒ false och ingenting ändras.
    bool set(InputBinding b);
    // false när nyckeln inte fanns.
    bool remove(const std::string& mode, InputSource s, std::uint32_t mods);
    void clearRows() { rows_.clear(); }
    const std::vector<InputBinding>& rows() const { return rows_; }
    // Raden med EXAKT nyckeln, eller nullptr.
    const InputBinding* find(const std::string& mode, InputSource s,
                             std::uint32_t mods) const;
    bool hasMode(const std::string& mode) const;

    // VÄNDNINGARNA: satt eller inte — en osatt ärvs.
    void setSourceInvert(InputSource s, bool on);
    void clearSourceInvert(InputSource s);
    std::optional<bool> sourceInvert(InputSource s) const;
    void setActionInvert(InputDevice d, InputAction a, bool on);
    void clearActionInvert(InputDevice d, InputAction a);
    std::optional<bool> actionInvert(InputDevice d, InputAction a) const;

private:
    std::vector<InputBinding> rows_;
    // 0 = osatt, 1 = av, 2 = på.
    std::array<std::uint8_t, kInputSources> sourceInv_{};
    std::array<std::uint8_t, kInputDevices * kInputActions> actionInv_{};
};

// Den effektiva raden för källan i läget med de tryckta modifierarna (regel
// 1–2, utan lägets reserv) — ritytans tabell `own` (nullptr = ingen) över
// appens. nullptr = ingen träff.
const InputBinding* lookupBinding(const InputMap* own, const InputMap& app,
                                  const std::string& mode, InputSource s,
                                  std::uint32_t mods);

// LÄGET SOM GÄLLER för källans GRUPP (regel 3) — en knapps två dragaxlar,
// hjulets två, styrplattans två, pilarnas två, och nypningen, vridningen och
// + och − var för sig: `mode` när det är ett av ramverkets eller har en träff
// för någon av gruppens källor med modifierarna, annars `autoMode`.
std::string groupMode(const InputMap* own, const InputMap& app,
                      const std::string& mode, const std::string& autoMode,
                      InputSource s, std::uint32_t mods);

// DRAGETS PLAN: det mest specifika modifierarparet bland raderna för
// källorna `x` och `y` där minst en axels handling är påslagen — varje axel
// tar sin mest specifika rad inom paret. `enabled` är en bitmask med en bit
// per `InputAction` (1 << handling). Är allt avstängt prövas nästa mindre
// specifika — men en axel som mött en `None`-rad förblir obunden (`None`
// skuggar). En axel utan påslagen handling är nullptr i svaret.
struct DragPlan {
    const InputBinding* x = nullptr;
    const InputBinding* y = nullptr;
    bool any() const { return x || y; }
};
DragPlan resolveDrag(const InputMap* own, const InputMap& app,
                     const std::string& mode, InputSource x, InputSource y,
                     std::uint32_t mods, std::uint32_t enabled);

// Den påslagna raden för en händelse (per händelse, den mest specifika
// matchande — avstängd eller `None` ⇒ nullptr, och händelsen bubblar).
const InputBinding* resolveEvent(const InputMap* own, const InputMap& app,
                                 const std::string& mode, InputSource s,
                                 std::uint32_t mods, std::uint32_t enabled);

// TECKNET OCH FARTEN för raden: fart × riktning × källans vändning ×
// handlingens vändning för källans enhet — ritytans satta vändningar före
// appens.
double bindingFactor(const InputMap* own, const InputMap& app,
                     const InputBinding& b);

} // namespace glaze::detail
