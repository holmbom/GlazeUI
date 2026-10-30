#pragma once

// Handkontrollerna: ett action set med en action för varje knapp, beröring och
// värde i `Button`-uppsättningen, grip-, aim- och handflatepose och en
// haptikaction, per hand. Föreslagna bindningar för sex kontrollerprofiler;
// den aktiva profilen per hand avgör vilka knappar som finns (`available`).
// Knapparnas tillstånd löses ur de råa värdena (hysteres på analoga värden)
// och flanker blir `InputEvent`. Allt utom `vibrate`/`stopVibration` och
// `profileChanged` körs i UI-tråden, i bildrutejobbet.

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <glaze_openxr/session.h>
#include "openxr_context.h"
#include "session_state.h"

namespace glaze::openxr::detail {

// ----------------------------------------------------------- INDATALOGIKEN
// Ren logik utan OpenXR-typer; provas utan runtime.

// Ett analogt värde räknas som nedtryckt över `kPressThreshold` och släppt
// under `kReleaseThreshold`; däremellan behålls föregående läge.
inline constexpr double kPressThreshold   = 0.55;
inline constexpr double kReleaseThreshold = 0.45;

// Vad actionerna gav för EN knapp i en bildruta. `has*` är actionens
// `isActive`.
struct RawButton {
    bool   hasValue = false;
    double value    = 0.0;
    bool   hasClick = false;
    bool   click    = false;
    bool   hasTouch = false;
    bool   touch    = false;
};

bool analogPressed(double value, bool wasPressed) noexcept;

// Knappens tillstånd: nedtryckt är klicket när profilen har ett, annars det
// analoga värdet med hysteres (`wasPressed` är föregående bildrutas råa
// läge). Värdet är det analoga, annars 0/1. Berörd bara där profilen känner
// beröring. En knapp som inte finns (`available == false`) är helt noll.
ButtonState resolveButton(const RawButton& raw, bool wasPressed, bool available) noexcept;

// Spaken efter en radiell dödzon: noll innanför, annars skalad så att kanten
// av dödzonen blir 0 och fullt utslag 1.
void applyDeadzone(double& x, double& y, double deadzone) noexcept;

// Knapparnas tillstånd över tid för båda händerna: löser de råa värdena,
// ger flankerna som händelser (ordning: berörd, nedtryckt, släppt, inte
// längre berörd, per knapp) och spärrar det som redan var nedtryckt eller
// berört när fokus kom tillbaka — det ger ingen flank och räknas som släppt
// tills det släpps på riktigt. Utan fokus är allt släppt.
class ButtonTracker {
public:
    void update(Hand hand, bool focused,
                const std::array<RawButton, kButtonCount>& raw,
                const std::array<bool, kButtonCount>& available,
                std::array<ButtonState, kButtonCount>& out,
                std::vector<InputEvent>& events);
    // Glöm allt (efter en fokusförlust): nästa fokuserade bildruta spärrar på nytt.
    void reset() noexcept;

private:
    struct Per {
        bool rawPressed = false, rawTouched = false;       // runtimens läge
        bool pressed = false, touched = false;             // det som rapporterats
        bool blockPressed = false, blockTouched = false;   // spärrat sedan fokus kom tillbaka
    };
    std::array<std::array<Per, kButtonCount>, 2> state_{};
    std::array<bool, 2> wasFocused_{};
};

// ------------------------------------------------------------------ ACTIONS

class Actions {
public:
    // Skapar action set, actions och rum och kopplar dem till sessionen. En
    // profil som runtimen avvisar hoppas över och nämns i `skippedProfiles()`;
    // bara ett fel i själva action-setet misslyckas (text i `error`).
    static std::unique_ptr<Actions> create(const OpenXrContext& context, const Options& options,
                                           std::string& error);
    ~Actions();

    Actions(const Actions&)            = delete;
    Actions& operator=(const Actions&) = delete;

    // Läser handkontrollernas lägen i referensrummet vid `time` och, med
    // fokus, knapparna. `focusEpoch` räknas upp varje gång fokus förlorats:
    // knapparnas tillstånd från före ett byte gäller inte efter det. Knapp-
    // flankerna sedan förra anropet läggs i `events` (vänster hand först).
    // `out[0]` är vänster.
    bool sync(XrTime time, bool focused, std::uint64_t focusEpoch,
              std::array<ControllerSample, 2>& out, std::vector<InputEvent>& events,
              std::string& error);

    // En hands aktiva profil har bytts. Får anropas från vilken tråd som helst.
    void profileChanged() noexcept { profileDirty_ = true; }

    // Vibration i en kontroll; false utan fokus eller om runtimen avvisar den.
    // Anropas i UI-tråden, mellan bildrutor eller i en callback.
    bool vibrate(Hand hand, double amplitude, double seconds, double frequencyHz);
    void stopVibration(Hand hand);

    // Profiler runtimen inte tog emot, för felsökning.
    const std::string& skippedProfiles() const noexcept { return skipped_; }

private:
    Actions(const OpenXrContext& context, const Options& options);

    struct ProfileInfo;
    struct Impl;

    bool locate(XrSpace space, XrTime time, PoseSample& out, ControllerSample* velocity) const;
    void refreshProfiles();

    const OpenXrContext& context_;
    Options              options_;

    std::unique_ptr<Impl> impl_;
    std::array<XrPath, 2> handPaths_{};
    std::array<std::array<XrSpace, 2>, 3> spaces_{};   // grip, aim, palm — per hand
    XrActionSet actionSet_ = XR_NULL_HANDLE;

    ButtonTracker       tracker_;
    std::uint64_t       epoch_ = 0;
    std::atomic<bool>   profileDirty_{true};
    std::atomic<bool>   focused_{false};
    std::string         skipped_;
};

} // namespace glaze::openxr::detail
