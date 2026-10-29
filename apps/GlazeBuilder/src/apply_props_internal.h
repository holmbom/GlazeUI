#pragma once
#include <optional>
#include <string>
#include <vector>

#include <gfx/color.h>
#include <glaze/glaze.h>

#include "window_model.h"

// INTERN för builder_preview — apply_props.cpp:s literalparsrar för grenar
// som bor i ANDRA källfiler (apply_terminal.cpp, som bara
// kompileras när sidopaketet Terminal finns). Samma funktioner, aldrig
// kopior: definitionerna står i apply_props.cpp och lämnar bara över till
// dess egna parsrar.

namespace bld::detail {

// Argumentlistan delad på komma vid djup 0 (`bld::splitArgs`), varje argument
// trimmat — tom vid en felaktig lista (raden blir då opak).
std::vector<std::string> callArgs(const std::string& args);

// `gfx::Color{r, g, b, a}` (0–4 kanaler) — `parseColorLiteral`s regel.
bool parseColor(const std::string& v, gfx::Color& ut);

#ifdef GLAZE_PREVIEW_TERMINAL
// TERMINALEN (apply_terminal.cpp). Textstorleken går fältens typade väg,
// anropen med flera argument `applyTerminalCall`:
// std::nullopt = inte terminalens anrop (vägen fortsätter), annars utfört
// (true) eller opakt och loggat (false).
bool                isTerminal(const glaze::Widget& w);
void                setTerminalTextSize(glaze::Widget& w, const glaze::Length& l);
std::optional<bool> applyTerminalCall(glaze::Widget& w, const Call& call,
                                      const char* origin);
#endif

} // namespace bld::detail
