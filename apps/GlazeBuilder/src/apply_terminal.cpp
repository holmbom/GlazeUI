// apply_terminal — sidopaketet Terminals anrop i previewn.
// Kompileras BARA när `glaze_terminal_ui` finns
// (apps/GlazeBuilder/CMakeLists.txt, GLAZE_PREVIEW_TERMINAL).
//
// Previewn visar en TOM terminal och startar aldrig ett program: setter-
// fälten (program, arguments, workingDirectory, colorMode, colorScheme,
// scrollback, altAsMeta) går `setProp`-vägen i apply_props.cpp av sig själva,
// och här står det som har flera argument — miljön och de egna färgerna — och
// textstorleken. Handlingarna som når OS:et (`start()`, `write(…)`, `kill()`,
// `paste(…)`, `copy()`, `clear()`) utförs MEDVETET inte och bevaras opakt
// (Timers `start()`-prejudikat).
//
// ATOMISKT: allt tolkas till temporärer innan terminalen ändras; en rad som
// inte går att tolka loggas och blir opak.

#include <iterator>

#include <glaze_terminal/terminal.h>

#include "apply_props_internal.h"

namespace bld::detail {
namespace {

// `<prefix><Namn>` → namnet med gemener, bara med typens prefix.
bool enumArg(const std::string& v, const char* prefix, std::string& lower)
{
    return v.rfind(prefix, 0) == 0 && bld::parseEnumTail(v, lower);
}

bool parseRole(const std::string& v, glaze::TerminalColor& ut)
{
    static constexpr const char* kNamn[] = {
        "black",        "red",           "green",        "yellow",
        "blue",         "magenta",       "cyan",         "white",
        "brightblack",  "brightred",     "brightgreen",  "brightyellow",
        "brightblue",   "brightmagenta", "brightcyan",   "brightwhite",
        "foreground",   "background",    "cursor",       "cursortext",
        "selection",    "selectiontext",
    };
    static_assert(std::size(kNamn) == glaze::kTerminalColorCount);
    std::string namn;
    if (!enumArg(v, "glaze::TerminalColor::", namn))
        return false;
    for (std::size_t i = 0; i < std::size(kNamn); ++i)
        if (namn == kNamn[i]) {
            ut = static_cast<glaze::TerminalColor>(i);
            return true;
        }
    return false;
}

} // namespace

bool isTerminal(const glaze::Widget& w)
{
    return dynamic_cast<const glaze::Terminal*>(&w) != nullptr;
}

void setTerminalTextSize(glaze::Widget& w, const glaze::Length& l)
{
    if (auto* t = dynamic_cast<glaze::Terminal*>(&w))
        t->setTextSize(l);
}

std::optional<bool> applyTerminalCall(glaze::Widget& w, const Call& call,
                                      const char* origin)
{
    auto* t = dynamic_cast<glaze::Terminal*>(&w);
    if (!t)
        return std::nullopt;
    const std::string& m = call.method;
    if (m != "setEnvironmentVariable" && m != "unsetEnvironmentVariable" &&
        m != "setSchemeColor" && m != "clearSchemeColors")
        return std::nullopt;

    const std::vector<std::string> a = callArgs(call.args);
    if (m == "clearSchemeColors" && a.empty()) {
        t->clearSchemeColors();
        return true;
    }
    std::string namn, värde;
    if (m == "setEnvironmentVariable" && a.size() == 2 && bld::parseCppString(a[0], namn) &&
        bld::parseCppString(a[1], värde)) {
        t->setEnvironmentVariable(namn, std::move(värde));
        return true;
    }
    if (m == "unsetEnvironmentVariable" && a.size() == 1 && bld::parseCppString(a[0], namn)) {
        t->unsetEnvironmentVariable(namn);
        return true;
    }
    std::string          läge;
    glaze::TerminalColor roll{};
    gfx::Color           färg{};
    if (m == "setSchemeColor" && a.size() == 3 && enumArg(a[0], "glaze::ThemeMode::", läge) &&
        (läge == "light" || läge == "dark") && parseRole(a[1], roll) && parseColor(a[2], färg)) {
        t->setSchemeColor(läge == "dark" ? glaze::ThemeMode::Dark : glaze::ThemeMode::Light, roll,
                          färg);
        return true;
    }
    gfx::log("%s(%s): opakt %s-anrop (argumenten avkodas inte, utförs inte)", origin,
             w.name.c_str(), m.c_str());
    return false;
}

} // namespace bld::detail
