// Ritytans bindningstabell — se input_map.h.

#include "input_map.h"

#include <algorithm>
#include <bit>
#include <cmath>

#include <gfx/events.h>   // gfx::kMod*

namespace glaze::detail {

namespace {

using S = InputSource;
using A = InputAction;
using D = InputDirection;

// `groupMode` hittar en axels par på grannplatsen — X på jämn plats.
static_assert(static_cast<int>(S::LeftDragX) % 2 == 0 &&
              static_cast<int>(S::MiddleDragX) % 2 == 0 &&
              static_cast<int>(S::RightDragX) % 2 == 0 &&
              static_cast<int>(S::WheelX) % 2 == 0 &&
              static_cast<int>(S::ScrollX) % 2 == 0 &&
              static_cast<int>(S::ArrowsX) % 2 == 0);

constexpr std::uint32_t kNone  = 0;
constexpr std::uint32_t kCtrl  = gfx::kModCtrl;
constexpr std::uint32_t kShift = gfx::kModShift;
constexpr std::uint32_t kCS    = gfx::kModCtrl | gfx::kModShift;

// RAMVERKETS FÖRVAL — förvalets gester rad för rad. Pilarnas panorering och
// styrplattans vridning är `Inverted`: källans + (pilen åt höger, fingrarna
// medurs) är handlingens − (innehållet åt vänster, bäringen minskar — kartan
// vrids med fingrarna).
struct Förval {
    const char*   mode;
    S             source;
    std::uint32_t mods;
    A             action;
    D             direction;
};
constexpr Förval kFörval[] = {
    // KARTAN: vänster- och mittendrag panorerar, Ctrl vrider och tiltar.
    {kModeMap, S::LeftDragX, kNone, A::PanX, D::Normal},
    {kModeMap, S::LeftDragY, kNone, A::PanY, D::Normal},
    {kModeMap, S::LeftDragX, kCtrl, A::Rotate, D::Normal},
    {kModeMap, S::LeftDragY, kCtrl, A::Tilt, D::Normal},
    {kModeMap, S::MiddleDragX, kNone, A::PanX, D::Normal},
    {kModeMap, S::MiddleDragY, kNone, A::PanY, D::Normal},
    {kModeMap, S::MiddleDragX, kCtrl, A::Rotate, D::Normal},
    {kModeMap, S::MiddleDragY, kCtrl, A::Tilt, D::Normal},
    {kModeMap, S::WheelY, kNone, A::Zoom, D::Normal},
    {kModeMap, S::ScrollX, kNone, A::PanX, D::Normal},
    {kModeMap, S::ScrollY, kNone, A::PanY, D::Normal},
    {kModeMap, S::Pinch, kNone, A::Zoom, D::Normal},
    {kModeMap, S::Twist, kNone, A::Rotate, D::Inverted},
    {kModeMap, S::ArrowsX, kNone, A::PanX, D::Inverted},
    {kModeMap, S::ArrowsY, kNone, A::PanY, D::Inverted},
    {kModeMap, S::ArrowsX, kShift, A::Rotate, D::Normal},
    {kModeMap, S::ArrowsY, kShift, A::Tilt, D::Normal},
    {kModeMap, S::PlusMinus, kNone, A::Zoom, D::Normal},
    // ORBITEN: vänsterdraget orbitar, mitten panorerar, Shift flyger och
    // Ctrl orbitar; Ctrl+Shift har egna rader där Shift ska vinna (mittendrag)
    // respektive Ctrl (styrplattan) — vid lika antal bitar vann annars Shift.
    {kModeOrbit, S::LeftDragX, kNone, A::Rotate, D::Normal},
    {kModeOrbit, S::LeftDragY, kNone, A::Tilt, D::Normal},
    {kModeOrbit, S::LeftDragX, kShift, A::PanX, D::Normal},
    {kModeOrbit, S::LeftDragY, kShift, A::Fly, D::Normal},
    {kModeOrbit, S::MiddleDragX, kNone, A::PanX, D::Normal},
    {kModeOrbit, S::MiddleDragY, kNone, A::PanY, D::Normal},
    {kModeOrbit, S::MiddleDragX, kShift, A::PanX, D::Normal},
    {kModeOrbit, S::MiddleDragY, kShift, A::Fly, D::Normal},
    {kModeOrbit, S::MiddleDragX, kCtrl, A::Rotate, D::Normal},
    {kModeOrbit, S::MiddleDragY, kCtrl, A::Tilt, D::Normal},
    {kModeOrbit, S::MiddleDragX, kCS, A::PanX, D::Normal},
    {kModeOrbit, S::MiddleDragY, kCS, A::Fly, D::Normal},
    {kModeOrbit, S::WheelY, kNone, A::Zoom, D::Normal},
    {kModeOrbit, S::ScrollX, kNone, A::PanX, D::Normal},
    {kModeOrbit, S::ScrollY, kNone, A::PanY, D::Normal},
    {kModeOrbit, S::ScrollX, kShift, A::PanX, D::Normal},
    {kModeOrbit, S::ScrollY, kShift, A::Fly, D::Normal},
    {kModeOrbit, S::ScrollX, kCtrl, A::Rotate, D::Normal},
    {kModeOrbit, S::ScrollY, kCtrl, A::Tilt, D::Normal},
    {kModeOrbit, S::ScrollX, kCS, A::Rotate, D::Normal},
    {kModeOrbit, S::ScrollY, kCS, A::Tilt, D::Normal},
    {kModeOrbit, S::Pinch, kNone, A::Zoom, D::Normal},
    {kModeOrbit, S::Twist, kNone, A::Rotate, D::Inverted},
    {kModeOrbit, S::ArrowsX, kNone, A::PanX, D::Inverted},
    {kModeOrbit, S::ArrowsY, kNone, A::Fly, D::Inverted},
    {kModeOrbit, S::ArrowsX, kShift, A::Rotate, D::Normal},
    {kModeOrbit, S::ArrowsY, kShift, A::Tilt, D::Normal},
    {kModeOrbit, S::PlusMinus, kNone, A::Zoom, D::Normal},
    // FÖRSTAPERSONEN: vänsterdraget ser sig om kring ögat, mitten
    // förflyttar i sidled och höjd, hjulet och pilarna går framåt.
    {kModeFirstPerson, S::LeftDragX, kNone, A::Rotate, D::Normal},
    {kModeFirstPerson, S::LeftDragY, kNone, A::Tilt, D::Normal},
    {kModeFirstPerson, S::MiddleDragX, kNone, A::PanX, D::Normal},
    {kModeFirstPerson, S::MiddleDragY, kNone, A::Lift, D::Normal},
    {kModeFirstPerson, S::WheelY, kNone, A::Fly, D::Normal},
    {kModeFirstPerson, S::ScrollX, kNone, A::PanX, D::Normal},
    {kModeFirstPerson, S::ScrollY, kNone, A::Fly, D::Normal},
    {kModeFirstPerson, S::Pinch, kNone, A::Fly, D::Normal},
    {kModeFirstPerson, S::Twist, kNone, A::Rotate, D::Inverted},
    {kModeFirstPerson, S::ArrowsX, kNone, A::Rotate, D::Normal},
    {kModeFirstPerson, S::ArrowsY, kNone, A::Fly, D::Inverted},
    {kModeFirstPerson, S::ArrowsX, kShift, A::PanX, D::Inverted},
    {kModeFirstPerson, S::ArrowsY, kShift, A::Lift, D::Inverted},
    {kModeFirstPerson, S::PlusMinus, kNone, A::Fly, D::Normal},
};

bool ramverketsLäge(const std::string& mode)
{
    return mode == kModeMap || mode == kModeOrbit || mode == kModeFirstPerson;
}

// Matchar raden källan i läget med de tryckta (normaliserade) modifierarna?
bool matchar(const InputBinding& b, const std::string& mode, InputSource s,
             std::uint32_t tryckta)
{
    return b.source == s && (b.mods & ~tryckta) == 0 && b.mode == mode;
}

// En vändnings tre lägen (0 osatt, 1 av, 2 på).
std::optional<bool> läst(std::uint8_t v)
{
    if (v == 0)
        return std::nullopt;
    return v == 2;
}

int vändIndex(InputDevice d, InputAction a)
{
    return static_cast<int>(d) * kInputActions + static_cast<int>(a);
}

bool påslagen(const InputBinding* b, std::uint32_t enabled)
{
    return b && b->action != InputAction::None &&
           (enabled & (1u << static_cast<unsigned>(b->action))) != 0;
}

} // namespace

std::uint32_t normalizeMods(std::uint32_t mods)
{
    std::uint32_t m = mods & (gfx::kModCtrl | gfx::kModShift | gfx::kModAlt);
    if (mods & gfx::kModCmd)
        m |= gfx::kModCtrl;
    return m;
}

int modsRank(std::uint32_t m)
{
    // Bitarna väger mest; vid lika antal Alt (4) före Shift (2) före Ctrl (1).
    return std::popcount(m) * 8 + ((m & gfx::kModAlt) ? 4 : 0) +
           ((m & gfx::kModShift) ? 2 : 0) + ((m & gfx::kModCtrl) ? 1 : 0);
}

bool validBinding(const InputBinding& b, const char*& why)
{
    if (b.mode.empty()) {
        why = "läget är tomt";   // lang_gate:rad loggens skäl, ingen användartext
        return false;
    }
    if (!(b.speed > 0.0f) || !std::isfinite(b.speed)) {
        why = "farten är inte > 0 och ändlig";   // lang_gate:rad loggens skäl, ingen användartext
        return false;
    }
    if (static_cast<int>(b.source) >= kInputSources ||
        static_cast<int>(b.action) >= kInputActions ||
        static_cast<int>(b.direction) > static_cast<int>(InputDirection::Inverted)) {
        why = "en uppräkning utanför sina värden";   // lang_gate:rad loggens skäl, ingen användartext
        return false;
    }
    return true;
}

InputMap InputMap::defaults()
{
    InputMap m;
    m.rows_.reserve(std::size(kFörval));
    for (const Förval& f : kFörval)
        m.rows_.push_back(InputBinding{f.mode, f.source, f.mods, f.action,
                                       f.direction, 1.0f});
    return m;
}

bool InputMap::set(InputBinding b)
{
    const char* varför = nullptr;
    if (!validBinding(b, varför))
        return false;
    b.mods = normalizeMods(b.mods);
    for (InputBinding& r : rows_)
        if (r.mode == b.mode && r.source == b.source && r.mods == b.mods) {
            r = std::move(b);
            return true;
        }
    rows_.push_back(std::move(b));
    return true;
}

bool InputMap::remove(const std::string& mode, InputSource s, std::uint32_t mods)
{
    const std::uint32_t m = normalizeMods(mods);
    const auto it = std::find_if(rows_.begin(), rows_.end(), [&](const InputBinding& r) {
        return r.mode == mode && r.source == s && r.mods == m;
    });
    if (it == rows_.end())
        return false;
    rows_.erase(it);
    return true;
}

const InputBinding* InputMap::find(const std::string& mode, InputSource s,
                                   std::uint32_t mods) const
{
    const std::uint32_t m = normalizeMods(mods);
    for (const InputBinding& r : rows_)
        if (r.mode == mode && r.source == s && r.mods == m)
            return &r;
    return nullptr;
}

bool InputMap::hasMode(const std::string& mode) const
{
    return std::any_of(rows_.begin(), rows_.end(),
                       [&](const InputBinding& r) { return r.mode == mode; });
}

void InputMap::setSourceInvert(InputSource s, bool on)
{
    if (static_cast<int>(s) < kInputSources)
        sourceInv_[static_cast<int>(s)] = on ? 2 : 1;
}

void InputMap::clearSourceInvert(InputSource s)
{
    if (static_cast<int>(s) < kInputSources)
        sourceInv_[static_cast<int>(s)] = 0;
}

std::optional<bool> InputMap::sourceInvert(InputSource s) const
{
    if (static_cast<int>(s) >= kInputSources)
        return std::nullopt;
    return läst(sourceInv_[static_cast<int>(s)]);
}

void InputMap::setActionInvert(InputDevice d, InputAction a, bool on)
{
    if (static_cast<int>(d) < kInputDevices && static_cast<int>(a) < kInputActions)
        actionInv_[vändIndex(d, a)] = on ? 2 : 1;
}

void InputMap::clearActionInvert(InputDevice d, InputAction a)
{
    if (static_cast<int>(d) < kInputDevices && static_cast<int>(a) < kInputActions)
        actionInv_[vändIndex(d, a)] = 0;
}

std::optional<bool> InputMap::actionInvert(InputDevice d, InputAction a) const
{
    if (static_cast<int>(d) >= kInputDevices || static_cast<int>(a) >= kInputActions)
        return std::nullopt;
    return läst(actionInv_[vändIndex(d, a)]);
}

const InputBinding* lookupBinding(const InputMap* own, const InputMap& app,
                                  const std::string& mode, InputSource s,
                                  std::uint32_t mods)
{
    const std::uint32_t tryckta = normalizeMods(mods);
    const InputBinding* bäst    = nullptr;
    const auto pröva = [&](const InputBinding& r) {
        if (matchar(r, mode, s, tryckta) &&
            (!bäst || modsRank(r.mods) > modsRank(bäst->mods)))
            bäst = &r;
    };
    if (own)
        for (const InputBinding& r : own->rows())
            pröva(r);
    // Appens rad med samma nyckel som en av ritytans är ersatt (regel 2).
    for (const InputBinding& r : app.rows())
        if (!own || !own->find(r.mode, r.source, r.mods))
            pröva(r);
    return bäst;
}

std::string groupMode(const InputMap* own, const InputMap& app,
                      const std::string& mode, const std::string& autoMode,
                      InputSource s, std::uint32_t mods)
{
    if (ramverketsLäge(mode))
        return mode;
    // Gruppens par: källorna står parvis X, Y på jämn och udda plats i
    // uppräkningen (dragen, hjulet, styrplattan, pilarna); nypningen,
    // vridningen och + och − är ensamma.
    const bool  ensam = s == S::Pinch || s == S::Twist || s == S::PlusMinus;
    const InputSource par =
        ensam ? s : static_cast<InputSource>(static_cast<int>(s) ^ 1);
    if (lookupBinding(own, app, mode, s, mods) ||
        lookupBinding(own, app, mode, par, mods))
        return mode;
    return autoMode;
}

DragPlan resolveDrag(const InputMap* own, const InputMap& app,
                     const std::string& mode, InputSource x, InputSource y,
                     std::uint32_t mods, std::uint32_t enabled)
{
    // Kandidaterna: modifierarna på varje matchande rad för de två källorna,
    // mest specifika först.
    const std::uint32_t        tryckta = normalizeMods(mods);
    std::vector<std::uint32_t> par;
    const auto samla = [&](const InputMap& m) {
        for (const InputBinding& r : m.rows())
            if ((matchar(r, mode, x, tryckta) || matchar(r, mode, y, tryckta)) &&
                std::find(par.begin(), par.end(), r.mods) == par.end())
                par.push_back(r.mods);
    };
    if (own)
        samla(*own);
    samla(app);
    std::sort(par.begin(), par.end(), [](std::uint32_t a, std::uint32_t b) {
        return modsRank(a) > modsRank(b);
    });
    // En axel vars rad är `None` vid ett mer specifikt par förblir obunden när
    // uppslaget faller till ett mindre specifikt: `RightDragX` →
    // PanX utan modifierare och → None med Shift gör Shift+högerdraget obundet.
    // Bara en AVSTÄNGD handling (flaggorna) släpper fram den mindre specifika.
    bool skuggadX = false, skuggadY = false;
    for (const std::uint32_t m : par) {
        DragPlan p;
        const InputBinding* bx = skuggadX ? nullptr : lookupBinding(own, app, mode, x, m);
        const InputBinding* by = skuggadY ? nullptr : lookupBinding(own, app, mode, y, m);
        skuggadX = skuggadX || (bx && bx->action == InputAction::None);
        skuggadY = skuggadY || (by && by->action == InputAction::None);
        if (påslagen(bx, enabled))
            p.x = bx;
        if (påslagen(by, enabled))
            p.y = by;
        if (p.any())
            return p;
    }
    return {};
}

const InputBinding* resolveEvent(const InputMap* own, const InputMap& app,
                                 const std::string& mode, InputSource s,
                                 std::uint32_t mods, std::uint32_t enabled)
{
    const InputBinding* b = lookupBinding(own, app, mode, s, mods);
    return påslagen(b, enabled) ? b : nullptr;
}

double bindingFactor(const InputMap* own, const InputMap& app,
                     const InputBinding& b)
{
    const auto ärvd = [&](auto fråga) {
        std::optional<bool> v = own ? fråga(*own) : std::nullopt;
        if (!v)
            v = fråga(app);
        return v.value_or(false);
    };
    const bool källa = ärvd([&](const InputMap& m) { return m.sourceInvert(b.source); });
    const bool handling = ärvd([&](const InputMap& m) {
        return m.actionInvert(deviceOf(b.source), b.action);
    });
    double f = b.speed;
    if (b.direction == InputDirection::Inverted)
        f = -f;
    if (källa)
        f = -f;
    if (handling)
        f = -f;
    return f;
}

} // namespace glaze::detail
