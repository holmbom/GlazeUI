// Widget-REGISTRET — typnamn → factory.
// Konsumenter: GlazeBuilders palett/preview,
// egna widgets (GLAZE_REGISTER_WIDGET i widgetens egen cpp) och headless-
// tester. Events kopplas aldrig här — det gör genererad kod (typsäkert,
// kompileringsfel om en handler saknas).

#include "glaze/widget.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>   // abi::__cxa_demangle — GCC och Clang (libstdc++, libc++abi)
#define GLAZE_HAS_CXXABI 1
#endif

#include <gfx/platform.h>

namespace glaze {
namespace {

// En registrering: fabriken och KLASSEN. Klassens namn räknas ut
// LATT — avmanglingen kostar en allokering per typ, och en vanlig app frågar
// aldrig; bara verktygen (glaze-render, GlazeBuilder) och grindarna gör det.
// Pekaren till `type_info` lever lika länge som fabrikspekaren bredvid den:
// båda pekar in i modulen som registrerade typen.
struct Registrering {
    WidgetFactory         make = nullptr;
    const std::type_info* cls  = nullptr;
    mutable std::string   klass;   // tom tills någon frågar
};

// Meyers-singleton så registret garanterat finns när widgetarnas statiska
// GLAZE_REGISTER_WIDGET-objekt körs — oavsett TU-initieringsordning.
std::unordered_map<std::string, Registrering>& registry()
{
    static std::unordered_map<std::string, Registrering> r;
    return r;
}

// Klassens FULLA namn, så som en medlemsrad skriver det: "glaze::Button".
// GCC och Clang ger det manglade namnet ur `type_info::name()` ("N5glaze6ButtonE"),
// och ABI:ts egen avmanglare gör det läsbart; MSVC ger "class glaze::Button"
// och prefixet skalas. Lyckas avmanglingen inte blir svaret det råa namnet —
// det matchar då ingen medlemsrad, och typen blir en stand-in i stället för
// en gissning.
std::string klassnamn(const std::type_info& t)
{
#ifdef GLAZE_HAS_CXXABI
    int   status = 0;
    char* namn   = abi::__cxa_demangle(t.name(), nullptr, nullptr, &status);
    std::string ut = (status == 0 && namn) ? std::string(namn) : t.name();
    std::free(namn);
    return ut;
#else
    std::string ut = t.name();
    for (std::string_view pre : {std::string_view("class "),
                                 std::string_view("struct ")})
        if (ut.rfind(pre, 0) == 0)
            return ut.substr(pre.size());
    return ut;
#endif
}

const std::string& klassFör(const Registrering& r)
{
    if (r.klass.empty() && r.cls)
        r.klass = klassnamn(*r.cls);
    return r.klass;
}

} // namespace

bool registerWidgetType(const char* type, WidgetFactory make,
                        const std::type_info& cls)
{
    if (!registry().emplace(type, Registrering{make, &cls, {}}).second)
        gfx::log("registerWidgetType: '%s' är redan registrerad (ignoreras)",
                 type);
    return true;
}

std::unique_ptr<Widget> makeWidget(const std::string& type)
{
    // Okänd typ = nullptr — anroparen (GlazeBuilder) väljer stand-in eller
    // fel; egenskaper sätts efteråt via setProp (okänd egenskap loggas +
    // ignoreras där, hård regel 4:s arvtagare).
    const auto it = registry().find(type);
    return it == registry().end() ? nullptr : it->second.make();
}

// KLASSUPPSLAGET — en linjär sökning med de cachade namnen: registret
// har ett femtiotal typer och frågas en gång per modellnod när ett verktyg
// bygger sitt träd, så en andra karta (som måste hållas i takt med en
// registrering ur en plugin som laddas senare) hade inte betalat sig. EXAKT
// jämförelse, se widget.h.
std::unique_ptr<Widget> makeWidgetOfClass(const std::string& cppClass)
{
    if (cppClass.empty())
        return nullptr;
    for (const auto& [type, reg] : registry()) {
        (void)type;
        if (klassFör(reg) == cppClass)
            return reg.make();
    }
    return nullptr;
}

std::string widgetClassOf(const std::string& type)
{
    const auto it = registry().find(type);
    return it == registry().end() ? std::string() : klassFör(it->second);
}

// UPPRÄKNINGEN. SORTERAD utdata med flit: kartan är
// en `unordered_map`, så iterationsordningen är en implementationsdetalj
// och en grind som skriver ut vad den mätte hade gett olika rader mellan
// körningar. Kopian är billig (tjugotalet strängar, anropas av verktyg och
// grindar — aldrig i en ritväg).
std::vector<std::string> registeredWidgetTypes()
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
