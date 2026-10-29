// KOMPONENTREGISTRET — C++-typnamn → fabrik och
// previewform. Samma form som widgetregistret (gui_loader.cpp), och samma
// konsumenter: verktygens preview (`bld::buildComponents`) och grindarna.
// Events kopplas aldrig här — det gör genererad kod.

#include "glaze/component.h"

#include <algorithm>
#include <unordered_map>

#include <gfx/platform.h>

namespace glaze {
namespace {

struct Registrering {
    ComponentFactory make    = nullptr;
    PreviewForm      preview = PreviewForm::StandIn;
};

// Meyers-singleton så registret garanterat finns när komponenternas
// statiska GLAZE_REGISTER_COMPONENT-objekt körs — oavsett TU-ordning.
std::unordered_map<std::string, Registrering>& registry()
{
    static std::unordered_map<std::string, Registrering> r;
    return r;
}

} // namespace

bool registerComponentType(const char* cppType, ComponentFactory make,
                           PreviewForm preview)
{
    if (!registry().emplace(cppType, Registrering{make, preview}).second)
        gfx::log("registerComponentType: '%s' är redan registrerad "
                 "(ignoreras)",
                 cppType);
    return true;
}

std::unique_ptr<Component> makeComponent(const std::string& cppType)
{
    const auto it = registry().find(cppType);
    return it == registry().end() ? nullptr : it->second.make();
}

std::optional<PreviewForm> componentPreviewForm(const std::string& cppType)
{
    const auto it = registry().find(cppType);
    if (it == registry().end())
        return std::nullopt;
    return it->second.preview;
}

// SORTERAD utdata — kartan är en `unordered_map` (registeredWidgetTypes skäl).
std::vector<std::string> registeredComponentTypes()
{
    std::vector<std::string> out;
    out.reserve(registry().size());
    for (const auto& [type, reg] : registry()) {
        (void)reg;
        out.push_back(type);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace glaze
