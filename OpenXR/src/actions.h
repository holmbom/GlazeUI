#pragma once

// Handkontrollerna: ett action set med grip- och aim-pose, avtryckare (float),
// select (bool) och spak (x/y) för vänster och höger hand, med föreslagna
// bindningar för de vanliga kontrollerprofilerna. Körs i XR-tråden.

#include <array>
#include <memory>
#include <string>

#include <glaze_openxr/session.h>
#include "openxr_context.h"
#include "session_state.h"

namespace glaze::openxr::detail {

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

    // Läser handkontrollernas lägen i LOCAL vid `time` och, med fokus,
    // reglagen (spakarna efter dödzonen, avtryckaren med hysteres). Utan
    // fokus ger reglagen noll och hysteresen nollställs. `out[0]` är vänster.
    bool sync(XrTime time, bool focused, std::array<ControllerSample, 2>& out, std::string& error);

    // Släpper avtryckarnas hysteres (efter en fokusförlust).
    void release() noexcept { triggerDown_ = {}; }

    // Profiler runtimen inte tog emot, för felsökning.
    const std::string& skippedProfiles() const noexcept { return skipped_; }

private:
    explicit Actions(const OpenXrContext& context, const Options& options)
        : context_(context), options_(options) {}

    bool locate(XrSpace space, XrTime time, PoseSample& out) const;

    const OpenXrContext& context_;
    Options              options_;

    XrActionSet actionSet_ = XR_NULL_HANDLE;
    XrAction    gripPose_  = XR_NULL_HANDLE;
    XrAction    aimPose_   = XR_NULL_HANDLE;
    XrAction    trigger_   = XR_NULL_HANDLE;
    XrAction    select_    = XR_NULL_HANDLE;
    XrAction    stick_     = XR_NULL_HANDLE;
    std::array<XrPath, 2>  handPaths_{};
    std::array<XrSpace, 2> gripSpaces_{};
    std::array<XrSpace, 2> aimSpaces_{};
    std::array<bool, 2>    triggerDown_{};   // hysteresen
    std::string            skipped_;
};

} // namespace glaze::openxr::detail
