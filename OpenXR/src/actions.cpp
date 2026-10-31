#include "actions.h"

#include "hands.h"
#include "vector_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace glaze::openxr::detail {

// --------------------------------------------------------------- BINDNINGAR

namespace {

const char* const kHandName[2] = {"/user/hand/left", "/user/hand/right"};

// Allt en kontroll kan ge, en action var. Namnen på knapparnas actions följer
// `Button`; ett värde (analogt), ett klick och en beröring per knapp där
// profilerna har dem.
enum class Slot : std::uint8_t {
    TriggerValue, TriggerClick, TriggerTouch,
    SqueezeValue, SqueezeClick,
    PrimaryClick, PrimaryTouch,
    SecondaryClick, SecondaryTouch,
    StickClick, StickTouch, StickAxes,
    MenuClick,
    ThumbrestTouch,
    PadForce, PadClick, PadTouch, PadAxes,
    GripPose, AimPose, PalmPose,
    PinchPose, PokePose,
    Haptic,
    // Handprofilen: värden och beredskap (ready) för nyp, sikteaktivering och grepp.
    PinchValue, PinchReady, AimActivateValue, AimActivateReady, GraspValue, GraspReady,
    Count
};
constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);

enum class Role : std::uint8_t { Value, Click, Touch, Axes, Pose, Haptic, HandValue, HandReady };

struct SlotDef {
    Slot           slot;
    XrActionType   type;
    const char*    name;
    const char*    localized;
    Button         button;   // knappen en Value/Click/Touch/Axes hör till
    Role           role;
};

constexpr XrActionType kFloat   = XR_ACTION_TYPE_FLOAT_INPUT;
constexpr XrActionType kBoolean = XR_ACTION_TYPE_BOOLEAN_INPUT;
constexpr XrActionType kVector  = XR_ACTION_TYPE_VECTOR2F_INPUT;
constexpr XrActionType kPose    = XR_ACTION_TYPE_POSE_INPUT;
constexpr XrActionType kOutput  = XR_ACTION_TYPE_VIBRATION_OUTPUT;

const SlotDef kSlots[kSlotCount] = {
    {Slot::TriggerValue,   kFloat,   "trigger_value",   "Trigger value",    Button::Trigger,    Role::Value},
    {Slot::TriggerClick,   kBoolean, "trigger_click",   "Trigger click",    Button::Trigger,    Role::Click},
    {Slot::TriggerTouch,   kBoolean, "trigger_touch",   "Trigger touch",    Button::Trigger,    Role::Touch},
    {Slot::SqueezeValue,   kFloat,   "squeeze_value",   "Squeeze value",    Button::Squeeze,    Role::Value},
    {Slot::SqueezeClick,   kBoolean, "squeeze_click",   "Squeeze click",    Button::Squeeze,    Role::Click},
    {Slot::PrimaryClick,   kBoolean, "primary_click",   "Primary click",    Button::Primary,    Role::Click},
    {Slot::PrimaryTouch,   kBoolean, "primary_touch",   "Primary touch",    Button::Primary,    Role::Touch},
    {Slot::SecondaryClick, kBoolean, "secondary_click", "Secondary click",  Button::Secondary,  Role::Click},
    {Slot::SecondaryTouch, kBoolean, "secondary_touch", "Secondary touch",  Button::Secondary,  Role::Touch},
    {Slot::StickClick,     kBoolean, "stick_click",     "Thumbstick click", Button::Thumbstick, Role::Click},
    {Slot::StickTouch,     kBoolean, "stick_touch",     "Thumbstick touch", Button::Thumbstick, Role::Touch},
    {Slot::StickAxes,      kVector,  "stick_axes",      "Thumbstick",       Button::Thumbstick, Role::Axes},
    {Slot::MenuClick,      kBoolean, "menu_click",      "Menu click",       Button::Menu,       Role::Click},
    {Slot::ThumbrestTouch, kBoolean, "thumbrest_touch", "Thumbrest touch",  Button::Thumbrest,  Role::Touch},
    {Slot::PadForce,       kFloat,   "pad_force",       "Trackpad force",   Button::Trackpad,   Role::Value},
    {Slot::PadClick,       kBoolean, "pad_click",       "Trackpad click",   Button::Trackpad,   Role::Click},
    {Slot::PadTouch,       kBoolean, "pad_touch",       "Trackpad touch",   Button::Trackpad,   Role::Touch},
    {Slot::PadAxes,        kVector,  "pad_axes",        "Trackpad",         Button::Trackpad,   Role::Axes},
    {Slot::GripPose,       kPose,    "grip_pose",       "Grip",             Button::Count,      Role::Pose},
    {Slot::AimPose,        kPose,    "aim_pose",        "Aim",              Button::Count,      Role::Pose},
    {Slot::PalmPose,       kPose,    "palm_pose",       "Palm",             Button::Count,      Role::Pose},
    {Slot::PinchPose,      kPose,    "pinch_pose",      "Pinch",            Button::Count,      Role::Pose},
    {Slot::PokePose,       kPose,    "poke_pose",       "Poke",             Button::Count,      Role::Pose},
    {Slot::Haptic,         kOutput,  "haptic",          "Haptic",           Button::Count,      Role::Haptic},
    {Slot::PinchValue,        kFloat,   "pinch_value",         "Pinch value",         Button::Count, Role::HandValue},
    {Slot::PinchReady,        kBoolean, "pinch_ready",         "Pinch ready",         Button::Count, Role::HandReady},
    {Slot::AimActivateValue,  kFloat,   "aim_activate_value",  "Aim activate value",  Button::Count, Role::HandValue},
    {Slot::AimActivateReady,  kBoolean, "aim_activate_ready",  "Aim activate ready",  Button::Count, Role::HandReady},
    {Slot::GraspValue,        kFloat,   "grasp_value",         "Grasp value",         Button::Count, Role::HandValue},
    {Slot::GraspReady,        kBoolean, "grasp_ready",         "Grasp ready",         Button::Count, Role::HandReady},
};

// Slottarna som bara hör till handprofilen (och skapas bara med tillägget).
bool handOnly(Slot slot) {
    switch (slot) {
    case Slot::PinchPose: case Slot::PokePose:
    case Slot::PinchValue: case Slot::PinchReady:
    case Slot::AimActivateValue: case Slot::AimActivateReady:
    case Slot::GraspValue: case Slot::GraspReady:
        return true;
    default:
        return false;
    }
}

// Poseslottarna i `Actions::spaces_`s ordning.
constexpr Slot kPoseSlots[] = {Slot::GripPose, Slot::AimPose, Slot::PalmPose, Slot::PinchPose, Slot::PokePose};
constexpr std::size_t kPoseKinds = std::size(kPoseSlots);

enum class Side : std::uint8_t { Both, Left, Right };

struct Binding {
    Slot        slot;
    Side        side;
    const char* path;   // efter handens sökväg
};

constexpr Side LR = Side::Both;
constexpr Side L  = Side::Left;
constexpr Side R  = Side::Right;

// Bara sökvägar som respektive profil definierar. Vänster och höger skiljer
// sig åt där profilen döper knapparna olika (x/y mot a/b) eller bara har
// menyknappen på ena handen.
constexpr Binding kSimple[] = {
    {Slot::TriggerClick, LR, "/input/select/click"},
    {Slot::MenuClick,    LR, "/input/menu/click"},
};

constexpr Binding kTouch[] = {
    {Slot::TriggerValue,    LR, "/input/trigger/value"},
    {Slot::TriggerTouch,    LR, "/input/trigger/touch"},
    {Slot::SqueezeValue,    LR, "/input/squeeze/value"},
    {Slot::StickAxes,       LR, "/input/thumbstick"},
    {Slot::StickClick,      LR, "/input/thumbstick/click"},
    {Slot::StickTouch,      LR, "/input/thumbstick/touch"},
    {Slot::ThumbrestTouch,  LR, "/input/thumbrest/touch"},
    {Slot::PrimaryClick,    L,  "/input/x/click"},
    {Slot::PrimaryClick,    R,  "/input/a/click"},
    {Slot::PrimaryTouch,    L,  "/input/x/touch"},
    {Slot::PrimaryTouch,    R,  "/input/a/touch"},
    {Slot::SecondaryClick,  L,  "/input/y/click"},
    {Slot::SecondaryClick,  R,  "/input/b/click"},
    {Slot::SecondaryTouch,  L,  "/input/y/touch"},
    {Slot::SecondaryTouch,  R,  "/input/b/touch"},
    {Slot::MenuClick,       L,  "/input/menu/click"},
};

constexpr Binding kIndex[] = {
    {Slot::TriggerValue,    LR, "/input/trigger/value"},
    {Slot::TriggerClick,    LR, "/input/trigger/click"},
    {Slot::TriggerTouch,    LR, "/input/trigger/touch"},
    {Slot::SqueezeValue,    LR, "/input/squeeze/value"},
    {Slot::PrimaryClick,    LR, "/input/a/click"},
    {Slot::PrimaryTouch,    LR, "/input/a/touch"},
    {Slot::SecondaryClick,  LR, "/input/b/click"},
    {Slot::SecondaryTouch,  LR, "/input/b/touch"},
    {Slot::StickAxes,       LR, "/input/thumbstick"},
    {Slot::StickClick,      LR, "/input/thumbstick/click"},
    {Slot::StickTouch,      LR, "/input/thumbstick/touch"},
    {Slot::PadAxes,         LR, "/input/trackpad"},
    {Slot::PadForce,        LR, "/input/trackpad/force"},
    {Slot::PadTouch,        LR, "/input/trackpad/touch"},
};

constexpr Binding kPico4[] = {
    {Slot::TriggerValue,    LR, "/input/trigger/value"},
    {Slot::TriggerClick,    LR, "/input/trigger/click"},
    {Slot::TriggerTouch,    LR, "/input/trigger/touch"},
    {Slot::SqueezeValue,    LR, "/input/squeeze/value"},
    {Slot::SqueezeClick,    LR, "/input/squeeze/click"},
    {Slot::StickAxes,       LR, "/input/thumbstick"},
    {Slot::StickClick,      LR, "/input/thumbstick/click"},
    {Slot::StickTouch,      LR, "/input/thumbstick/touch"},
    {Slot::ThumbrestTouch,  LR, "/input/thumbrest/touch"},
    {Slot::PrimaryClick,    L,  "/input/x/click"},
    {Slot::PrimaryClick,    R,  "/input/a/click"},
    {Slot::PrimaryTouch,    L,  "/input/x/touch"},
    {Slot::PrimaryTouch,    R,  "/input/a/touch"},
    {Slot::SecondaryClick,  L,  "/input/y/click"},
    {Slot::SecondaryClick,  R,  "/input/b/click"},
    {Slot::SecondaryTouch,  L,  "/input/y/touch"},
    {Slot::SecondaryTouch,  R,  "/input/b/touch"},
    {Slot::MenuClick,       L,  "/input/menu/click"},
};

constexpr Binding kVive[] = {
    {Slot::TriggerValue, LR, "/input/trigger/value"},
    {Slot::TriggerClick, LR, "/input/trigger/click"},
    {Slot::SqueezeClick, LR, "/input/squeeze/click"},
    {Slot::MenuClick,    LR, "/input/menu/click"},
    {Slot::PadAxes,      LR, "/input/trackpad"},
    {Slot::PadClick,     LR, "/input/trackpad/click"},
    {Slot::PadTouch,     LR, "/input/trackpad/touch"},
};

constexpr Binding kMotion[] = {
    {Slot::TriggerValue, LR, "/input/trigger/value"},
    {Slot::SqueezeClick, LR, "/input/squeeze/click"},
    {Slot::MenuClick,    LR, "/input/menu/click"},
    {Slot::StickAxes,    LR, "/input/thumbstick"},
    {Slot::StickClick,   LR, "/input/thumbstick/click"},
    {Slot::PadAxes,      LR, "/input/trackpad"},
    {Slot::PadClick,     LR, "/input/trackpad/click"},
    {Slot::PadTouch,     LR, "/input/trackpad/touch"},
};

// Posernas och haptikens sökvägar, gemensamma för alla profiler.
constexpr Binding kCommon[] = {
    {Slot::GripPose, LR, "/input/grip/pose"},
    {Slot::AimPose,  LR, "/input/aim/pose"},
    {Slot::Haptic,   LR, "/output/haptic"},
};
constexpr Binding kPalm = {Slot::PalmPose, LR, "/input/palm_ext/pose"};

// Handprofilen (XR_EXT_hand_interaction): nyp- och pekposerna, nypet,
// sikteaktiveringen och greppet med sina ready-flaggor. Ingen haptik.
constexpr Binding kHandExt[] = {
    {Slot::PinchPose,        LR, "/input/pinch_ext/pose"},
    {Slot::PokePose,         LR, "/input/poke_ext/pose"},
    {Slot::PinchValue,       LR, "/input/pinch_ext/value"},
    {Slot::PinchReady,       LR, "/input/pinch_ext/ready_ext"},
    {Slot::AimActivateValue, LR, "/input/aim_activate_ext/value"},
    {Slot::AimActivateReady, LR, "/input/aim_activate_ext/ready_ext"},
    {Slot::GraspValue,       LR, "/input/grasp_ext/value"},
    {Slot::GraspReady,       LR, "/input/grasp_ext/ready_ext"},
};

// Tillägget en profil kräver för att få föreslås.
enum class Needs : std::uint8_t { Nothing, BdController, HandInteraction };

struct ProfileDef {
    const char*    path;
    Needs          needs;
    const Binding* bindings;
    std::size_t    count;
};

template <std::size_t N>
constexpr ProfileDef profileDef(const char* path, Needs needs, const Binding (&bindings)[N]) {
    return ProfileDef{path, needs, bindings, N};
}

const ProfileDef kProfiles[] = {
    profileDef("/interaction_profiles/khr/simple_controller", Needs::Nothing, kSimple),
    profileDef("/interaction_profiles/oculus/touch_controller", Needs::Nothing, kTouch),
    profileDef("/interaction_profiles/valve/index_controller", Needs::Nothing, kIndex),
    profileDef("/interaction_profiles/bytedance/pico4_controller", Needs::BdController, kPico4),
    profileDef("/interaction_profiles/htc/vive_controller", Needs::Nothing, kVive),
    profileDef("/interaction_profiles/microsoft/motion_controller", Needs::Nothing, kMotion),
    profileDef("/interaction_profiles/ext/hand_interaction_ext", Needs::HandInteraction, kHandExt),
};
constexpr std::size_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

bool appliesTo(Side side, int hand) {
    return side == Side::Both || (side == Side::Left && hand == 0) || (side == Side::Right && hand == 1);
}

} // namespace

// Allt Actions håller som kräver typerna ovan.
struct Actions::ProfileInfo {
    std::string path;
    XrPath      xrPath = XR_NULL_PATH;
    bool        accepted = false;
    bool        hand = false;   // handprofilen: ingen kontroll
    // Knapparna profilen har per hand, ur bindningarna runtimen tog emot.
    std::array<std::array<bool, kButtonCount>, 2> available{};
};

struct Actions::Impl {
    std::array<XrAction, kSlotCount> actions{};
    std::vector<ProfileInfo>         profiles;
    std::array<int, 2>               current{-1, -1};   // index i `profiles`, −1 = okänd eller ingen
    std::array<std::string, 2>       currentPath;       // profilens sökväg, tom utan kontroll
};

// ---------------------------------------------------------------- SKAPANDET

Actions::Actions(const OpenXrContext& context, const Options& options)
    : context_(context), options_(options) {}

std::unique_ptr<Actions> Actions::create(const OpenXrContext& context, const Options& options,
                                         std::string& error) {
    std::unique_ptr<Actions> self(new Actions(context, options));
    self->impl_ = std::make_unique<Impl>();
    Impl& impl = *self->impl_;
    const XrFunctions& fn = context.fn();
    const XrInstance instance = context.instance();
    const XrSession session = context.session();
    const bool palm = context.extensions().palmPose;
    const bool handInteraction = context.extensions().handInteraction;

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

    for (const SlotDef& def : kSlots) {
        if (def.slot == Slot::PalmPose && !palm) continue;
        if (handOnly(def.slot) && !handInteraction) continue;
        auto info = xrStruct<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
        info.actionType = def.type;
        std::snprintf(info.actionName, sizeof(info.actionName), "%s", def.name);
        std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", def.localized);
        info.countSubactionPaths = 2;
        info.subactionPaths = self->handPaths_.data();
        XrAction& action = impl.actions[static_cast<std::size_t>(def.slot)];
        if (const XrResult r = fn.xrCreateAction(self->actionSet_, &info, &action); XR_FAILED(r)) {
            action = XR_NULL_HANDLE;
            return fail("xrCreateAction", r);
        }
    }

    // Föreslagna bindningar per profil. En avvisad profil stoppar inte
    // sessionen: tas alla bindningar emot på en gång är det klart; annars
    // provas de en och en ovanpå posernas, så att det runtimen godtar blir kvar
    // (en profil tar bara emot hela uppsättningar, och den senaste lyckade
    // gäller).
    struct Entry {
        Slot     slot;
        int      hand;
        XrAction action;
        XrPath   path;
    };
    for (const ProfileDef& def : kProfiles) {
        ProfileInfo info;
        info.path = def.path;
        info.hand = def.needs == Needs::HandInteraction;
        if ((def.needs == Needs::BdController && !context.extensions().bdController) ||
            (def.needs == Needs::HandInteraction && !handInteraction)) {
            impl.profiles.push_back(std::move(info));
            continue;
        }
        if (!path(def.path, info.xrPath)) {
            self->skipped_ += std::string(def.path) + " (okänd sökväg); ";
            impl.profiles.push_back(std::move(info));
            continue;
        }

        const auto makeEntries = [&](const Binding& b, std::vector<Entry>& into) {
            const XrAction action = impl.actions[static_cast<std::size_t>(b.slot)];
            if (action == XR_NULL_HANDLE) return;
            for (int hand = 0; hand < 2; ++hand) {
                if (!appliesTo(b.side, hand)) continue;
                XrPath xr = XR_NULL_PATH;
                if (path(std::string(kHandName[hand]) + b.path, xr)) into.push_back({b.slot, hand, action, xr});
            }
        };
        // Poserna är kärnan; resten kan avvisas av en enskild runtime.
        std::vector<Entry> core;
        std::vector<Entry> optional;
        for (const Binding& b : kCommon) {
            if (b.slot == Slot::Haptic && info.hand) continue;   // handprofilen har ingen haptik
            makeEntries(b, b.slot == Slot::Haptic ? optional : core);
        }
        if (palm) makeEntries(kPalm, optional);
        for (std::size_t i = 0; i < def.count; ++i) makeEntries(def.bindings[i], optional);

        const auto suggest = [&](const std::vector<Entry>& entries) {
            std::vector<XrActionSuggestedBinding> bindings;
            bindings.reserve(entries.size());
            for (const Entry& e : entries) bindings.push_back(XrActionSuggestedBinding{e.action, e.path});
            auto suggestion = xrStruct<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
            suggestion.interactionProfile = info.xrPath;
            suggestion.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
            suggestion.suggestedBindings = bindings.data();
            return XR_SUCCEEDED(fn.xrSuggestInteractionProfileBindings(instance, &suggestion));
        };

        std::vector<Entry> accepted = core;
        accepted.insert(accepted.end(), optional.begin(), optional.end());
        if (suggest(accepted)) {
            info.accepted = true;
        } else if (suggest(core)) {
            // Något i uppsättningen avvisades: behåll bara det som godtas.
            info.accepted = true;
            accepted = core;
            for (const Entry& e : optional) {
                std::vector<Entry> trial = accepted;
                trial.push_back(e);
                if (suggest(trial)) accepted = std::move(trial);
            }
        } else {
            self->skipped_ += std::string(def.path) + " (avvisad av runtimen); ";
            accepted.clear();
        }

        for (const Entry& e : accepted) {
            const SlotDef& slotDef = kSlots[static_cast<std::size_t>(e.slot)];
            if (slotDef.button != Button::Count) {
                info.available[static_cast<std::size_t>(e.hand)][static_cast<std::size_t>(slotDef.button)] = true;
            }
        }
        impl.profiles.push_back(std::move(info));
    }

    // Rummen för poserna, per hand.
    for (std::size_t kind = 0; kind < kPoseKinds; ++kind) {
        if (kPoseSlots[kind] == Slot::PalmPose && !palm) continue;
        if (handOnly(kPoseSlots[kind]) && !handInteraction) continue;
        for (std::size_t hand = 0; hand < 2; ++hand) {
            auto info = xrStruct<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
            info.action = impl.actions[static_cast<std::size_t>(kPoseSlots[kind])];
            info.subactionPath = self->handPaths_[hand];
            info.poseInActionSpace.orientation.w = 1.0f;
            XrSpace& space = self->spaces_[kind][hand];
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
    for (auto& kind : spaces_) {
        for (XrSpace space : kind) {
            if (space != XR_NULL_HANDLE) fn.xrDestroySpace(space);
        }
    }
    if (impl_) {
        for (XrAction action : impl_->actions) {
            if (action != XR_NULL_HANDLE) fn.xrDestroyAction(action);
        }
    }
    if (actionSet_ != XR_NULL_HANDLE) fn.xrDestroyActionSet(actionSet_);
}

// ------------------------------------------------------------------- LÄSNING

bool Actions::locate(XrSpace space, XrTime time, PoseSample& out, ControllerSample* velocity) const {
    out = PoseSample{};
    if (space == XR_NULL_HANDLE) return true;
    auto velocityState = xrStruct<XrSpaceVelocity>(XR_TYPE_SPACE_VELOCITY);
    auto location = xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    if (velocity) location.next = &velocityState;
    if (XR_FAILED(context_.fn().xrLocateSpace(space, context_.trackingSpace(), time, &location))) {
        return true;   // ett läge som saknas är ingen felsituation
    }
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out.valid = (location.locationFlags & needed) == needed;
    out.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    out.pose = fromXr(location.pose);
    if (velocity) {
        const XrSpaceVelocityFlags both = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
        velocity->velocityValid = out.valid && (velocityState.velocityFlags & both) == both;
        if (velocity->velocityValid) {
            velocity->linearVelocity = Vec3{velocityState.linearVelocity.x, velocityState.linearVelocity.y,
                                            velocityState.linearVelocity.z};
            velocity->angularVelocity = Vec3{velocityState.angularVelocity.x, velocityState.angularVelocity.y,
                                             velocityState.angularVelocity.z};
        }
    }
    return true;
}

void Actions::refreshProfiles() {
    const XrFunctions& fn = context_.fn();
    Impl& impl = *impl_;
    for (std::size_t hand = 0; hand < 2; ++hand) {
        auto state = xrStruct<XrInteractionProfileState>(XR_TYPE_INTERACTION_PROFILE_STATE);
        impl.current[hand] = -1;
        impl.currentPath[hand].clear();
        if (XR_FAILED(fn.xrGetCurrentInteractionProfile(context_.session(), handPaths_[hand], &state)) ||
            state.interactionProfile == XR_NULL_PATH) {
            continue;
        }
        for (std::size_t i = 0; i < impl.profiles.size(); ++i) {
            if (impl.profiles[i].xrPath == state.interactionProfile) {
                impl.current[hand] = static_cast<int>(i);
                impl.currentPath[hand] = impl.profiles[i].path;
                break;
            }
        }
        if (impl.current[hand] < 0) {
            // En profil vi inte föreslagit bindningar för (runtimen har valt
            // den ändå): bara namnet.
            std::uint32_t count = 0;
            char text[XR_MAX_PATH_LENGTH] = {};
            if (XR_SUCCEEDED(fn.xrPathToString(context_.instance(), state.interactionProfile,
                                               XR_MAX_PATH_LENGTH, &count, text))) {
                impl.currentPath[hand] = text;
            }
        }
    }
}

bool Actions::poseActive(std::size_t slot, std::size_t hand) const {
    if (!synced_ || !focused_) return true;
    const XrAction action = impl_->actions[slot];
    if (action == XR_NULL_HANDLE) return true;
    auto getInfo = xrStruct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
    getInfo.action = action;
    getInfo.subactionPath = handPaths_[hand];
    auto state = xrStruct<XrActionStatePose>(XR_TYPE_ACTION_STATE_POSE);
    if (XR_FAILED(context_.fn().xrGetActionStatePose(context_.session(), &getInfo, &state))) return true;
    return state.isActive == XR_TRUE;
}

namespace {

// En pose ur handspårningen som en spårad pose.
PoseSample trackedPose(const TrackingPose& pose) {
    PoseSample sample;
    sample.pose = pose;
    sample.valid = true;
    sample.positionTracked = true;
    sample.orientationTracked = true;
    return sample;
}

// Ledens pose, när den är giltig.
PoseSample jointPose(const HandSample& hand, int joint) {
    const JointSample& j = hand.joints[static_cast<std::size_t>(joint)];
    return hand.active && j.valid ? trackedPose(j.pose) : PoseSample{};
}

} // namespace

bool Actions::sync(XrTime time, double deltaSeconds, bool focused, std::uint64_t focusEpoch,
                   const PoseSample& head, const std::array<HandSample, 2>& hands,
                   std::array<ControllerSample, 2>& out, std::vector<InputEvent>& events,
                   std::string& error) {
    const XrFunctions& fn = context_.fn();
    const XrSession session = context_.session();
    Impl& impl = *impl_;
    events.clear();

    if (focusEpoch != epoch_) {
        epoch_ = focusEpoch;
        tracker_.reset();
    }

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
        // och anropet; knapparna läses som släppta.
        if (r == XR_SESSION_NOT_FOCUSED) focused = false;
        else synced_ = true;
    }
    focused_ = focused;

    // Profilerna frågas när runtimen sagt att de bytts, och tills varje hand
    // fått en (den kommer först när kontrollen börjat användas).
    if (profileDirty_.exchange(false) || impl.currentPath[0].empty() || impl.currentPath[1].empty()) {
        refreshProfiles();
    }

    const auto actionOf = [&](Slot slot) { return impl.actions[static_cast<std::size_t>(slot)]; };
    const TrackingPose* headPose = head.valid ? &head.pose : nullptr;

    for (std::size_t hand = 0; hand < 2; ++hand) {
        const Hand handId = hand == 0 ? Hand::Left : Hand::Right;
        const HandSample& tracked = hands[hand];
        ControllerSample sample;

        // Poserna som profilen ger, och bara de som är aktiva. Farten hör till
        // gripposen.
        static_assert(std::tuple_size_v<decltype(spaces_)> == kPoseKinds);
        std::array<PoseSample, kPoseKinds> poses;
        for (std::size_t kind = 0; kind < kPoseKinds; ++kind) {
            PoseSample& pose = poses[kind];
            locate(spaces_[kind][hand], time, pose, kind == 0 ? &sample : nullptr);
            if (pose.valid && !poseActive(static_cast<std::size_t>(kPoseSlots[kind]), hand)) pose = PoseSample{};
        }
        const PoseSample& grip = poses[0];
        const PoseSample& aim = poses[1];
        const PoseSample& palm = poses[2];
        const PoseSample& pinch = poses[3];
        const PoseSample& poke = poses[4];
        if (!grip.valid) sample.velocityValid = false;
        sample.profile = impl.currentPath[hand];

        const int profile = impl.current[hand];
        const bool handProfile = profile >= 0 && impl.profiles[static_cast<std::size_t>(profile)].hand;
        const bool controllerProfile = !impl.currentPath[hand].empty() && !handProfile;

        auto getInfo = xrStruct<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
        getInfo.subactionPath = handPaths_[hand];
        const auto readFloat = [&](Slot slot, double& value) {
            const XrAction action = actionOf(slot);
            if (action == XR_NULL_HANDLE) return false;
            getInfo.action = action;
            auto state = xrStruct<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
            if (XR_FAILED(fn.xrGetActionStateFloat(session, &getInfo, &state)) || !state.isActive) return false;
            value = state.currentState;
            return true;
        };
        const auto readBool = [&](Slot slot, bool& value) {
            const XrAction action = actionOf(slot);
            if (action == XR_NULL_HANDLE) return false;
            getInfo.action = action;
            auto state = xrStruct<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
            if (XR_FAILED(fn.xrGetActionStateBoolean(session, &getInfo, &state)) || !state.isActive) return false;
            value = state.currentState == XR_TRUE;
            return true;
        };

        // Handprofilens värden: nyp, sikteaktivering och grepp, nollade när
        // handen inte är redo för gesten.
        bool extValues = false;
        double extPinch = 0.0, extAim = 0.0, extGrasp = 0.0;
        if (focused && handProfile) {
            const auto gated = [&](Slot value, Slot ready, double& result) {
                double v = 0.0;
                if (!readFloat(value, v)) return;
                extValues = true;
                bool isReady = true;
                const bool hasReady = readBool(ready, isReady);
                result = gateByReady(unitValue(v), hasReady, isReady);
            };
            gated(Slot::PinchValue, Slot::PinchReady, extPinch);
            gated(Slot::AimActivateValue, Slot::AimActivateReady, extAim);
            gated(Slot::GraspValue, Slot::GraspReady, extGrasp);
        }

        // Handens sikte, i ordning: handprofilen, runtimens eget, härlett ur
        // lederna (utjämnat, och nollställt så fort ett annat gäller).
        const bool profileAim = handProfile && aim.valid;
        const bool runtimeAim = tracked.active && tracked.aimValid && !tracked.systemGesture;
        RawAim derived;
        if (!profileAim && !runtimeAim && tracked.active) {
            derived = smoothers_[hand].step(deriveAim(tracked, hand == 0, headPose), deltaSeconds);
        } else {
            smoothers_[hand].reset();
        }

        HandInputs inputs;
        inputs.controllerProfile = controllerProfile;
        inputs.controllerPose = aim.valid;
        inputs.handAim = profileAim || runtimeAim || derived.valid;
        inputs.suspended = tracked.active && tracked.systemGesture;
        if (extValues) {
            inputs.triggerAim = extAim;
            inputs.triggerPinch = extPinch;
            inputs.squeeze = extGrasp;
        } else if (tracked.active) {
            inputs.triggerPinch = pinchStrengthOf(tracked);
            inputs.squeeze = graspStrengthOf(tracked);
        }
        const HandOutput chosen = sources_[hand].update(deltaSeconds, inputs);
        if (chosen.cut) tracker_.cut(handId, events);
        sample.source = chosen.source;
        sample.inputEpoch = chosen.epoch;
        if (chosen.source != Controller::Source::Hand) smoothers_[hand].reset();

        // Poserna som källan ger; en annan källas poser visas aldrig. Farten
        // hör till gripposen som runtimen ger (kontrollens eller handprofilens).
        const auto dropVelocity = [&] {
            sample.velocityValid = false;
            sample.linearVelocity = Vec3{};
            sample.angularVelocity = Vec3{};
        };
        if (chosen.source == Controller::Source::Controller) {
            sample.grip = grip;
            sample.aim = aim;
            sample.palm = palm;
        } else if (chosen.source == Controller::Source::Hand) {
            if (profileAim) {
                sample.aim = aim;
            } else if (runtimeAim) {
                sample.aim = trackedPose(tracked.aimPose);
            } else if (const auto pose = aimPoseOf(derived)) {
                sample.aim = trackedPose(*pose);
            }
            const PoseSample palmJoint = jointPose(tracked, kJointPalm);
            if (handProfile && grip.valid) {
                sample.grip = grip;
            } else {
                sample.grip = palmJoint;
                dropVelocity();
            }
            sample.palm = palm.valid ? palm : palmJoint;
            const Quaternion facing = sample.aim.valid ? sample.aim.pose.orientation : Quaternion{};
            if (handProfile && pinch.valid) {
                sample.pinch = pinch;
            } else if (const auto point = pinchPointOf(tracked)) {
                sample.pinch = trackedPose(TrackingPose{*point, facing});
            }
            if (handProfile && poke.valid) {
                sample.poke = poke;
            } else if (const auto point = pokePointOf(tracked)) {
                sample.poke = trackedPose(TrackingPose{*point, facing});
            }
        } else {
            dropVelocity();
        }

        std::array<RawButton, kButtonCount> raw{};
        std::array<bool, kButtonCount> available{};
        if (chosen.source == Controller::Source::Controller) {
            if (profile >= 0) available = impl.profiles[static_cast<std::size_t>(profile)].available[hand];
            if (focused) {
                for (const SlotDef& def : kSlots) {
                    if (def.role == Role::Pose || def.role == Role::Haptic || def.role == Role::HandValue ||
                        def.role == Role::HandReady) {
                        continue;
                    }
                    const std::size_t b = static_cast<std::size_t>(def.button);
                    switch (def.type) {
                    case XR_ACTION_TYPE_FLOAT_INPUT:
                        if (readFloat(def.slot, raw[b].value)) raw[b].hasValue = available[b] = true;
                        break;
                    case XR_ACTION_TYPE_BOOLEAN_INPUT: {
                        bool value = false;
                        if (!readBool(def.slot, value)) break;
                        if (def.role == Role::Click) {
                            raw[b].hasClick = true;
                            raw[b].click = value;
                        } else {
                            raw[b].hasTouch = true;
                            raw[b].touch = value;
                        }
                        available[b] = true;
                        break;
                    }
                    case XR_ACTION_TYPE_VECTOR2F_INPUT: {
                        const XrAction action = actionOf(def.slot);
                        if (action == XR_NULL_HANDLE) break;
                        getInfo.action = action;
                        auto state = xrStruct<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
                        if (XR_SUCCEEDED(fn.xrGetActionStateVector2f(session, &getInfo, &state)) && state.isActive) {
                            double x = state.currentState.x;
                            double y = state.currentState.y;
                            if (def.slot == Slot::StickAxes) {
                                applyDeadzone(x, y, options_.stickDeadzone);
                                sample.stickX = x;
                                sample.stickY = y;
                            } else {
                                sample.padX = std::clamp(x, -1.0, 1.0);
                                sample.padY = std::clamp(y, -1.0, 1.0);
                            }
                            available[b] = true;
                        }
                        break;
                    }
                    default:
                        break;
                    }
                }
            }
        } else if (chosen.source == Controller::Source::Hand) {
            // Handen: avtryckaren är sikteaktiveringen eller nypet, greppet
            // greppet, med samma hysteres och flanker som kontrollens knappar.
            const auto trigger = static_cast<std::size_t>(Button::Trigger);
            const auto squeeze = static_cast<std::size_t>(Button::Squeeze);
            available[trigger] = available[squeeze] = true;
            raw[trigger].hasValue = true;
            raw[trigger].value = chosen.trigger;
            raw[squeeze].hasValue = true;
            raw[squeeze].value = chosen.squeeze;
        }

        tracker_.update(handId, focused, raw, available, sample.buttons, events);
        out[hand] = std::move(sample);
    }
    return true;
}

// ----------------------------------------------------------------- VIBRATION

bool Actions::vibrate(Hand hand, double amplitude, double seconds, double frequencyHz) {
    if (!focused_) return false;
    if (!std::isfinite(amplitude) || !std::isfinite(seconds) || !std::isfinite(frequencyHz)) return false;
    auto vibration = xrStruct<XrHapticVibration>(XR_TYPE_HAPTIC_VIBRATION);
    vibration.amplitude = static_cast<float>(std::clamp(amplitude, 0.0, 1.0));
    vibration.duration = seconds > 0.0 ? static_cast<XrDuration>(seconds * 1e9) : XR_MIN_HAPTIC_DURATION;
    vibration.frequency = frequencyHz > 0.0 ? static_cast<float>(frequencyHz) : XR_FREQUENCY_UNSPECIFIED;
    auto info = xrStruct<XrHapticActionInfo>(XR_TYPE_HAPTIC_ACTION_INFO);
    info.action = impl_->actions[static_cast<std::size_t>(Slot::Haptic)];
    info.subactionPath = handPaths_[handIndex(hand)];
    return context_.fn().xrApplyHapticFeedback(context_.session(), &info,
                                               reinterpret_cast<const XrHapticBaseHeader*>(&vibration)) == XR_SUCCESS;
}

void Actions::stopVibration(Hand hand) {
    auto info = xrStruct<XrHapticActionInfo>(XR_TYPE_HAPTIC_ACTION_INFO);
    info.action = impl_->actions[static_cast<std::size_t>(Slot::Haptic)];
    info.subactionPath = handPaths_[handIndex(hand)];
    context_.fn().xrStopHapticFeedback(context_.session(), &info);
}

} // namespace glaze::openxr::detail
