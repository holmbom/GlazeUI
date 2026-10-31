#pragma once

// Handkontrollerna och händerna som ingång: ett action set med en action för
// varje knapp, beröring och värde i `Button`-uppsättningen, grip-, aim- och
// handflatepose och en haptikaction, per hand. Föreslagna bindningar för sex
// kontrollerprofiler och handprofilen (`XR_EXT_hand_interaction`: nyp-,
// pek-, sikteaktiverings- och greppvärden och poser, utan haptik); den
// aktiva profilen per hand avgör vilka knappar som finns (`available`).
//
// KÄLLAN PER HAND (`Controller::source`): en kontroll när en kontrollprofil är
// aktiv och dess sikte giltigt, annars den spårade handen — sikte och
// knappar ur handprofilen, annars ur `XR_FB_hand_tracking_aim`, annars
// härledda ur lederna (hand_logic.h) — annars ingen. Knapparnas tillstånd
// löses ur de råa värdena (hysteres på analoga värden) och flanker blir
// `InputEvent`; byts källan släpps hållna knappar, `inputEpoch` ökar och det
// som fortfarande hålls spärras tills det släpps. Allt utom
// `vibrate`/`stopVibration` och `profileChanged` körs i UI-tråden, i
// bildrutejobbet.

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <glaze_openxr/session.h>
#include "hand_logic.h"
#include "openxr_context.h"
#include "session_state.h"

namespace glaze::openxr::detail {

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

    // Läser handkontrollernas och händernas lägen i referensrummet vid `time`
    // och, med fokus, knapparna. `hands` (lederna vid samma tid) och `head`
    // behövs för källvalet och det härledda siktet, så de ska vara lästa före
    // anropet. `focusEpoch` räknas upp varje gång fokus förlorats: knapparnas
    // tillstånd från före ett byte gäller inte efter det. Knapp-flankerna
    // sedan förra anropet läggs i `events` (vänster hand först, släppflanker
    // vid ett källbyte före handens övriga). `out[0]` är vänster.
    bool sync(XrTime time, double deltaSeconds, bool focused, std::uint64_t focusEpoch,
              const PoseSample& head, const std::array<HandSample, 2>& hands,
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
    // Är poseactionen aktiv för handen? Sant när det inte går att fråga
    // (inget synkat action set än).
    bool poseActive(std::size_t slot, std::size_t hand) const;

    const OpenXrContext& context_;
    Options              options_;

    std::unique_ptr<Impl> impl_;
    std::array<XrPath, 2> handPaths_{};
    std::array<std::array<XrSpace, 2>, 5> spaces_{};   // grip, aim, palm, pinch, poke — per hand
    XrActionSet actionSet_ = XR_NULL_HANDLE;

    ButtonTracker       tracker_;
    std::array<HandSource, 2>  sources_;     // källvalet per hand
    std::array<AimSmoother, 2> smoothers_;   // det härledda siktet per hand
    bool                synced_ = false;     // xrSyncActions har lyckats minst en gång
    std::uint64_t       epoch_ = 0;
    std::atomic<bool>   profileDirty_{true};
    std::atomic<bool>   focused_{false};
    std::string         skipped_;
};

} // namespace glaze::openxr::detail
