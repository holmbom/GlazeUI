#include "actions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace glaze::openxr::detail {

namespace {

constexpr double kTriggerPress   = 0.55;   // över: nedtryckt
constexpr double kTriggerRelease = 0.45;   // under: släppt

const char* const kHandName[2] = {"/user/hand/left", "/user/hand/right"};

enum class Input { Trigger, Select, Stick };

struct Binding {
    Input       input;
    const char* path;   // efter handen
};

// En kontrollerprofil: dess sökväg och reglagen utöver poserna.
struct Profile {
    const char*          path;
    bool                 needsBdExtension;
    std::vector<Binding> inputs;
};

std::vector<Profile> profiles() {
    return {
        {"/interaction_profiles/khr/simple_controller", false,
         {{Input::Select, "/input/select/click"}}},
        {"/interaction_profiles/oculus/touch_controller", false,
         {{Input::Trigger, "/input/trigger/value"}, {Input::Stick, "/input/thumbstick"}}},
        {"/interaction_profiles/valve/index_controller", false,
         {{Input::Trigger, "/input/trigger/value"}, {Input::Select, "/input/trigger/click"},
          {Input::Stick, "/input/thumbstick"}}},
        {"/interaction_profiles/bytedance/pico4_controller", true,
         {{Input::Trigger, "/input/trigger/value"}, {Input::Select, "/input/trigger/click"},
          {Input::Stick, "/input/thumbstick"}}},
    };
}

// Spaken efter en radiell dödzon: noll innanför, annars skalad så att kanten
// av dödzonen blir 0 och full utslag blir 1.
void applyDeadzone(double& x, double& y, double deadzone) {
    const double magnitude = std::hypot(x, y);
    if (magnitude <= deadzone || magnitude <= 0.0) {
        x = 0.0;
        y = 0.0;
        return;
    }
    const double span = std::max(1e-6, 1.0 - deadzone);
    const double scaled = std::min(1.0, (magnitude - deadzone) / span);
    x = x / magnitude * scaled;
    y = y / magnitude * scaled;
}

} // namespace

std::unique_ptr<Actions> Actions::create(const OpenXrContext& context, const Options& options,
                                         std::string& error) {
    std::unique_ptr<Actions> self(new Actions(context, options));
    const XrFunctions& fn = context.fn();
    const XrInstance instance = context.instance();
    const XrSession session = context.session();

    const auto fail = [&](const char* call, XrResult r) {
        error = context.describe(call, r);
        return nullptr;
    };
    const auto path = [&](const std::string& text, XrPath& out) {
        return XR_SUCCEEDED(fn.xrStringToPath(instance, text.c_str(), &out));
    };

    for (int hand = 0; hand < 2; ++hand) {
        if (!path(kHandName[hand], self->handPaths_[hand])) {
            error = std::string("xrStringToPath misslyckades för ") + kHandName[hand] + ".";
            return nullptr;
        }
    }

    auto setInfo = xrStruct<XrActionSetCreateInfo>(XR_TYPE_ACTION_SET_CREATE_INFO);
    std::snprintf(setInfo.actionSetName, sizeof(setInfo.actionSetName), "glaze_openxr");
    std::snprintf(setInfo.localizedActionSetName, sizeof(setInfo.localizedActionSetName), "GlazeUI");
    if (const XrResult r = fn.xrCreateActionSet(instance, &setInfo, &self->actionSet_); XR_FAILED(r)) {
        self->actionSet_ = XR_NULL_HANDLE;
        return fail("xrCreateActionSet", r);
    }

    const auto makeAction = [&](XrActionType type, const char* name, const char* localized, XrAction& out) {
        auto info = xrStruct<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
        info.actionType = type;
        std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
        std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", localized);
        info.countSubactionPaths = 2;
        info.subactionPaths = self->handPaths_.data();
        const XrResult r = fn.xrCreateAction(self->actionSet_, &info, &out);
        if (XR_FAILED(r)) {
            out = XR_NULL_HANDLE;
            error = context.describe("xrCreateAction", r);
            return false;
        }
        return true;
    };
    if (!makeAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip", self->gripPose_) ||
        !makeAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim", self->aimPose_) ||
        !makeAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", self->trigger_) ||
        !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "select", "Select", self->select_) ||
        !makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Stick", self->stick_)) {
        return nullptr;
    }

    // Föreslagna bindningar per profil. En avvisad profil stoppar inte sessionen.
    for (const Profile& profile : profiles()) {
        if (profile.needsBdExtension && !context.bdControllerExtension()) continue;
        XrPath profilePath = XR_NULL_PATH;
        if (!path(profile.path, profilePath)) {
            self->skipped_ += std::string(profile.path) + " (okänd sökväg); ";
            continue;
        }
        std::vector<XrActionSuggestedBinding> bindings;
        const auto add = [&](XrAction action, const std::string& text) {
            XrPath p = XR_NULL_PATH;
            if (path(text, p)) bindings.push_back(XrActionSuggestedBinding{action, p});
        };
        for (int hand = 0; hand < 2; ++hand) {
            const std::string prefix = kHandName[hand];
            add(self->gripPose_, prefix + "/input/grip/pose");
            add(self->aimPose_, prefix + "/input/aim/pose");
            for (const Binding& b : profile.inputs) {
                const XrAction action = b.input == Input::Trigger ? self->trigger_
                                      : b.input == Input::Select  ? self->select_
                                                                  : self->stick_;
                add(action, prefix + b.path);
            }
        }
        auto suggest = xrStruct<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
        suggest.interactionProfile = profilePath;
        suggest.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
        suggest.suggestedBindings = bindings.data();
        if (XR_FAILED(fn.xrSuggestInteractionProfileBindings(instance, &suggest))) {
            self->skipped_ += std::string(profile.path) + " (avvisad av runtimen); ";
        }
    }

    // Rummen för poserna, per hand.
    for (int hand = 0; hand < 2; ++hand) {
        for (int kind = 0; kind < 2; ++kind) {
            auto info = xrStruct<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
            info.action = kind == 0 ? self->gripPose_ : self->aimPose_;
            info.subactionPath = self->handPaths_[hand];
            info.poseInActionSpace.orientation.w = 1.0f;
            XrSpace& space = kind == 0 ? self->gripSpaces_[hand] : self->aimSpaces_[hand];
            if (const XrResult r = fn.xrCreateActionSpace(session, &info, &space); XR_FAILED(r)) {
                space = XR_NULL_HANDLE;
                return fail("xrCreateActionSpace", r);
            }
        }
    }

    auto attach = xrStruct<XrSessionActionSetsAttachInfo>(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);
    attach.countActionSets = 1;
    attach.actionSets = &self->actionSet_;
    if (const XrResult r = fn.xrAttachSessionActionSets(session, &attach); XR_FAILED(r)) {
        return fail("xrAttachSessionActionSets", r);
    }
    return self;
}

Actions::~Actions() {
    const XrFunctions& fn = context_.fn();
    for (XrSpace space : gripSpaces_) {
        if (space != XR_NULL_HANDLE) fn.xrDestroySpace(space);
    }
    for (XrSpace space : aimSpaces_) {
        if (space != XR_NULL_HANDLE) fn.xrDestroySpace(space);
    }
    for (XrAction action : {gripPose_, aimPose_, trigger_, select_, stick_}) {
        if (action != XR_NULL_HANDLE) fn.xrDestroyAction(action);
    }
    if (actionSet_ != XR_NULL_HANDLE) fn.xrDestroyActionSet(actionSet_);
}

bool Actions::locate(XrSpace space, XrTime time, PoseSample& out) const {
    out = PoseSample{};
    auto location = xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    if (XR_FAILED(context_.fn().xrLocateSpace(space, context_.localSpace(), time, &location))) {
        return true;   // ett läge som saknas är ingen felsituation
    }
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out.valid = (location.locationFlags & needed) == needed;
    out.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    out.pose = fromXr(location.pose);
    return true;
}

bool Actions::sync(XrTime time, bool focused, std::array<ControllerSample, 2>& out, std::string& error) {
    const XrFunctions& fn = context_.fn();
    const XrSession session = context_.session();

    if (focused) {
        XrActiveActionSet active{actionSet_, XR_NULL_PATH};
        auto syncInfo = xrStruct<XrActionsSyncInfo>(XR_TYPE_ACTIONS_SYNC_INFO);
        syncInfo.countActiveActionSets = 1;
        syncInfo.activeActionSets = &active;
        const XrResult r = fn.xrSyncActions(session, &syncInfo);
        if (XR_FAILED(r)) {
            error = context_.describe("xrSyncActions", r);
            return false;
        }
        // XR_SESSION_NOT_FOCUSED: fokus gick förlorat mellan tillståndsbytet
        // och anropet; reglagen läses som släppta.
        if (r == XR_SESSION_NOT_FOCUSED) focused = false;
    }

    for (std::size_t hand = 0; hand < 2; ++hand) {
        ControllerSample sample;
        locate(gripSpaces_[hand], time, sample.grip);
        locate(aimSpaces_[hand], time, sample.aim);

        if (!focused) {
            triggerDown_[hand] = false;
            out[hand] = sample;
            continue;
        }

        auto getInfo = xrStruct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
        getInfo.subactionPath = handPaths_[hand];

        double triggerValue = 0.0;
        getInfo.action = trigger_;
        auto triggerState = xrStruct<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
        if (XR_SUCCEEDED(fn.xrGetActionStateFloat(session, &getInfo, &triggerState)) && triggerState.isActive) {
            triggerValue = std::clamp(static_cast<double>(triggerState.currentState), 0.0, 1.0);
        }
        sample.trigger = triggerValue;
        if (triggerValue >= kTriggerPress) triggerDown_[hand] = true;
        else if (triggerValue <= kTriggerRelease) triggerDown_[hand] = false;

        bool selectValue = false;
        getInfo.action = select_;
        auto selectState = xrStruct<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
        if (XR_SUCCEEDED(fn.xrGetActionStateBoolean(session, &getInfo, &selectState)) && selectState.isActive) {
            selectValue = selectState.currentState == XR_TRUE;
        }
        sample.selectDown = triggerDown_[hand] || selectValue;

        getInfo.action = stick_;
        auto stickState = xrStruct<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
        if (XR_SUCCEEDED(fn.xrGetActionStateVector2f(session, &getInfo, &stickState)) && stickState.isActive) {
            sample.stickActive = true;
            double x = stickState.currentState.x;
            double y = stickState.currentState.y;
            applyDeadzone(x, y, options_.stickDeadzone);
            sample.stickX = x;
            sample.stickY = y;
        }
        out[hand] = sample;
    }
    return true;
}

} // namespace glaze::openxr::detail
